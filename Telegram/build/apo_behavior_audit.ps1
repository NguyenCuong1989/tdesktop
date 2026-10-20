# Ω-Telegram native behavioral evidence audit.
# Reads ONLY structural fields: no phone, message body, tokens, or account identifiers.
param(
    [string]$WorkingDir = '',
    [int]$MaxEvents = 200000,
    [string]$LedgerPath = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($WorkingDir)) {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    $WorkingDir = Join-Path $repo 'out\omega-native-authorized'
}
$ledger = if ([string]::IsNullOrWhiteSpace($LedgerPath)) {
    Join-Path $WorkingDir 'apo\raw_behavior.jsonl'
} else {
    $LedgerPath
}
if (!(Test-Path -LiteralPath $ledger)) {
    throw 'This native Telegram profile has no APΩ ledger; runtime not evidenced'
}
$reader = $null
$file = $null
$types = @{}
$actors = @{}
$results = @{}
$actions = @{}
$rpcPending = @{}
$rpcMethods = @{}
$rpcDurations = New-Object 'System.Collections.Generic.List[double]'
$rpcEpoch = 0
$nativeActionTrace = @{}
$lastUnreadValue = $null
$lastRuntimeUnreadEvents = 0
$lastRuntimeRepeatedUnreadValues = 0
$lastRuntimeLatestUnread = $null
$lastNativeUnreadRanking = $null
$lastRuntimeRankingEvents = 0
$rankingShapeErrors = 0
$nativeBodyProjectionEvents = 0
$completeNativeBodyProjectionEvents = 0
$nativeBodyShapeErrors = 0
$lastNativeBodyProjection = $null
$recoveryPendingEvents = 0
$totals = [ordered]@{
    eventCount=0
    parseErrors=0
    sequenceErrors=0
    prevHashLinkErrors=0
    sha256Errors=0
    hashVersion1Events=0
    hashVersion2Events=0
    unsupportedHashVersions=0
    authenticatedStateObservations=0
    positiveUnreadComputations=0
    plannedActions=0
    nativeDispatchBlocked=0
    sentPhases=0
    ackPhases=0
    observedPhases=0
    closedPhases=0
    nativeRpcRequests=0
    nativeRpcResponses=0
    correlatedRpcResponses=0
    orphanRpcResponses=0
    failedRpcResponses=0
    duplicatePendingRequestIds=0
}
$lastSequence = [long]0
$lastHash = '0' * 64
$sha256 = [Security.Cryptography.SHA256]::Create()
$reachedEndOfLedger = $false
try {
    $file = [IO.FileStream]::new(
        $ledger, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite)
    $reader = [IO.StreamReader]::new($file, [Text.Encoding]::UTF8)
    while (!$reader.EndOfStream -and $totals.eventCount -lt $MaxEvents) {
        $line = $reader.ReadLine()
        if (!$line) { continue }
        try {
            $event = $line | ConvertFrom-Json -ErrorAction Stop
            $type = [string]$event.event_type
            $actor = [string]$event.actor
            $sequence = [long]$event.sequence
            $prev = [string]$event.prev_hash
            $current = [string]$event.current_hash
            $hashVersion = if ($null -eq $event.PSObject.Properties['hash_version']) {
                [double]1
            } else {
                [double]$event.hash_version
            }
            if ($hashVersion -eq 1) { $totals.hashVersion1Events++ }
            elseif ($hashVersion -eq 2) { $totals.hashVersion2Events++ }
            else { $totals.unsupportedHashVersions++ }
            if ($sequence -ne ($lastSequence + 1)) { $totals.sequenceErrors++ }
            if ($prev -ne $lastHash) { $totals.prevHashLinkErrors++ }
            $payloadStart = $line.IndexOf('"payload":') + 10
            $payloadEnd = $line.LastIndexOf(',"prev_hash":')
            if ($payloadStart -lt 10 -or $payloadEnd -lt $payloadStart) {
                throw 'Ledger payload bounds are invalid'
            }
            $payloadJson = $line.Substring($payloadStart, $payloadEnd - $payloadStart)
            $canonical = if ($hashVersion -eq 1) {
                '{0}:{1}:{2}:{3}:{4}:{5}' -f $sequence,
                    [string]$event.trace_id, $type, $actor, $prev, $payloadJson
            } elseif ($hashVersion -eq 2) {
                '{0}:{1}:{2}:{3}:{4}:{5}:{6}:{7}:{8}' -f $hashVersion,
                    $sequence, [string]$event.trace_id,
                    [string]$event.parent_trace_id, $type, $actor,
                    [long]$event.timestamp_ms, $prev, $payloadJson
            } else {
                ''
            }
            $canonicalBytes = [Text.Encoding]::UTF8.GetBytes($canonical)
            $expectedHash = [Convert]::ToHexString(
                $sha256.ComputeHash($canonicalBytes)).ToLowerInvariant()
            if ($current -ne $expectedHash) { $totals.sha256Errors++ }
            $lastSequence = $sequence
            $lastHash = $current
            $totals.eventCount++
            if (!$types.ContainsKey($type)) { $types[$type] = 0 }
            $types[$type]++
            if (!$actors.ContainsKey($actor)) { $actors[$actor] = 0 }
            $actors[$actor]++
            $payload = $event.payload
            if ($null -ne $payload) {
                $props = @{}
                foreach ($prop in $payload.PSObject.Properties) {
                    $props[$prop.Name] = $prop.Value
                }
                if ($actor -eq 'apo.runtime' -and
                    $props.ContainsKey('status') -and
                    $props['status'] -eq 'APO_RUNTIME_STARTED') {
                    $rpcEpoch++
                    $rpcPending.Clear()
                    $lastUnreadValue = $null
                    $lastRuntimeUnreadEvents = 0
                    $lastRuntimeRepeatedUnreadValues = 0
                    $lastRuntimeLatestUnread = $null
                    $lastRuntimeRankingEvents = 0
                    $lastNativeUnreadRanking = $null
                }
                # Follow the causal chain of a native APΩ read-only
                # operation. Telegram's ordinary MTProto telemetry does
                # not count as an APΩ action unless action_id links it.
                $actionId = if ($props.ContainsKey('action_id')) {
                    [string]$props['action_id']
                } else { '' }
                if ($actor -eq 'apo.agent_core' -and
                    $props.ContainsKey('action_type') -and
                    $props['action_type'] -eq 'dialogs.inspect_server' -and
                    $actionId -ne '' -and
                    !$nativeActionTrace.ContainsKey($actionId)) {
                    $nativeActionTrace[$actionId] = [ordered]@{
                        actionId=$actionId
                        startSequence=$sequence
                        phases=[System.Collections.ArrayList]::new()
                        requestId=$null
                        mismatchedRequestIds=0
                        serverDialogCount=$null
                        localProjectionMatches=$null
                        lastResult=''
                        lastSequence=$sequence
                    }
                }
                if ($actionId -ne '' -and
                    $nativeActionTrace.ContainsKey($actionId)) {
                    $trace = $nativeActionTrace[$actionId]
                    if ($props.ContainsKey('new_phase')) {
                        [void]$trace.phases.Add([string]$props['new_phase'])
                    } elseif ($props.ContainsKey('phase')) {
                        [void]$trace.phases.Add([string]$props['phase'])
                    }
                    if ($props.ContainsKey('request_id') -and
                        $null -ne $props['request_id']) {
                        $reqId = [long]$props['request_id']
                        if ($null -eq $trace.requestId) {
                            $trace.requestId = $reqId
                        } elseif ($trace.requestId -ne $reqId) {
                            $trace.mismatchedRequestIds++
                        }
                    }
                    if ($props.ContainsKey('server_dialogs')) {
                        $trace.serverDialogCount = [int]$props['server_dialogs']
                    }
                    if ($props.ContainsKey('native_projection_matches')) {
                        $trace.localProjectionMatches =
                            [int]$props['native_projection_matches']
                    }
                    if ($props.ContainsKey('result')) {
                        $trace.lastResult = [string]$props['result']
                    }
                    $trace.lastSequence = $sequence
                }
                if ($actor -eq 'apo.state_bridge.unread' -and
                    $props.ContainsKey('unread_total')) {
                    $nextUnread = [int]$props['unread_total']
                    $lastRuntimeUnreadEvents++
                    if ($null -ne $lastUnreadValue -and $lastUnreadValue -eq $nextUnread) {
                        $lastRuntimeRepeatedUnreadValues++
                    }
                    $lastUnreadValue = $nextUnread
                    $lastRuntimeLatestUnread = $nextUnread
                }
                if ($actor -eq 'apo.cognitive.unread_ranking') {
                    $lastRuntimeRankingEvents++
                    $top = @()
                    if ($props.ContainsKey('top')) { $top = @($props['top']) }
                    $seenRows = [int]$props['inspected_rows']
                    $loadedRows = [int]$props['total_loaded_rows']
                    $knownRows = [int]$props['known_unread_rows']
                    if ($props['kind'] -ne 'NATIVE_UNREAD_PRIORITY_OBSERVATION' -or
                        $seenRows -gt $loadedRows -or $knownRows -gt $seenRows -or
                        $top.Count -gt 5 -or $props['native_dispatch'] -ne $false) {
                        $rankingShapeErrors++
                    }
                    $lastNativeUnreadRanking = [ordered]@{
                        nativeResult=$props['result']
                        scannedRows=$seenRows
                        loadedRows=$loadedRows
                        knownRows=$knownRows
                        rankedRows=[int]$props['unread_ranked_rows']
                        coveredLoadedRows=$props['full_loaded_list_covered']
                        topUnreadCounts=@($top | ForEach-Object {[int]$_.unread_messages})
                    }
                }
                if ($actor -eq 'apo.state_bridge.native_body') {
                    $nativeBodyProjectionEvents++
                    $requiredBodyFields = @(
                        'account_scope', 'source_trace_id', 'measured_at_ms',
                        'total_loaded_rows', 'inspected_rows', 'history_rows',
                        'user_dialogs', 'basic_groups', 'supergroups',
                        'broadcast_channels', 'unclassified_peers',
                        'known_unread_rows', 'unread_dialogs', 'mentioned_dialogs',
                        'source_loaded', 'complete'
                    )
                    $bodyFieldsPresent = @($requiredBodyFields | Where-Object {
                        $props.ContainsKey($_)
                    }).Count -eq $requiredBodyFields.Count
                    $historyRows = [int]$props['history_rows']
                    $userDialogs = [int]$props['user_dialogs']
                    $basicGroups = [int]$props['basic_groups']
                    $supergroups = [int]$props['supergroups']
                    $broadcastChannels = [int]$props['broadcast_channels']
                    $unclassifiedPeers = [int]$props['unclassified_peers']
                    $knownUnreadRows = [int]$props['known_unread_rows']
                    $unreadDialogs = [int]$props['unread_dialogs']
                    $mentionedDialogs = [int]$props['mentioned_dialogs']
                    $classifiedRows = $userDialogs + $basicGroups + $supergroups +
                        $broadcastChannels + $unclassifiedPeers
                    $bodyComplete = ($props['complete'] -eq $true)
                    $shapeValid = ($bodyFieldsPresent -and
                        $props['kind'] -eq 'NATIVE_BODY_PROJECTION' -and
                        $props['source'] -eq 'Data::Session.chatsList/History::peer' -and
                        ![string]::IsNullOrWhiteSpace([string]$props['account_scope']) -and
                        ![string]::IsNullOrWhiteSpace([string]$props['source_trace_id']) -and
                        [long]$props['measured_at_ms'] -gt 0 -and
                        [int]$props['inspected_rows'] -ge 0 -and
                        [int]$props['inspected_rows'] -le [int]$props['total_loaded_rows'] -and
                        $historyRows -ge 0 -and
                        $historyRows -le [int]$props['inspected_rows'] -and
                        $userDialogs -ge 0 -and $basicGroups -ge 0 -and
                        $supergroups -ge 0 -and $broadcastChannels -ge 0 -and
                        $unclassifiedPeers -ge 0 -and
                        $classifiedRows -eq $historyRows -and
                        $knownUnreadRows -ge 0 -and $knownUnreadRows -le $historyRows -and
                        $unreadDialogs -ge 0 -and $unreadDialogs -le $knownUnreadRows -and
                        $mentionedDialogs -ge 0 -and
                        $mentionedDialogs -le $knownUnreadRows -and
                        (!$bodyComplete -or ($props['source_loaded'] -eq $true -and
                            [int]$props['inspected_rows'] -eq
                                [int]$props['total_loaded_rows'] -and
                            $unclassifiedPeers -eq 0)))
                    if (!$shapeValid) { $nativeBodyShapeErrors++ }
                    if ($bodyComplete) { $completeNativeBodyProjectionEvents++ }
                    $lastNativeBodyProjection = [ordered]@{
                        sequence=$sequence
                        measuredAtMs=[long]$props['measured_at_ms']
                        loadedRows=[int]$props['total_loaded_rows']
                        inspectedRows=[int]$props['inspected_rows']
                        historyRows=$historyRows
                        userDialogs=$userDialogs
                        basicGroups=$basicGroups
                        supergroups=$supergroups
                        broadcastChannels=$broadcastChannels
                        unclassifiedPeers=$unclassifiedPeers
                        knownUnreadRows=$knownUnreadRows
                        unreadDialogs=$unreadDialogs
                        mentionedDialogs=$mentionedDialogs
                        sourceLoaded=($props['source_loaded'] -eq $true)
                        complete=$bodyComplete
                        shapeValid=$shapeValid
                    }
                }
                if ($actor -eq 'apo.cognitive.recovery' -and
                    $props['kind'] -eq 'NATIVE_READ_RECOVERY_PENDING' -and
                    $props['result'] -eq 'OUTCOME_UNRESOLVED' -and
                    $props['new_phase'] -eq 'RECONCILING') {
                    $recoveryPendingEvents++
                }
                if ($actor -eq 'apo.mtproto.dispatcher' -and
                    $props.ContainsKey('req_id')) {
                    $totals.nativeRpcRequests++
                    $key = [string]$rpcEpoch + ':' + [string]$props['req_id']
                    $method = if ($props.ContainsKey('method')) {
                        [string]$props['method']
                    } else { 'UNRECOGNIZED' }
                    if (!$rpcMethods.ContainsKey($method)) {
                        $rpcMethods[$method] = 0
                    }
                    $rpcMethods[$method]++
                    if ($rpcPending.ContainsKey($key)) {
                        $totals.duplicatePendingRequestIds++
                    }
                    $rpcPending[$key] = [long]$event.timestamp_ms
                }
                if ($actor -eq 'apo.mtproto.receiver' -and
                    $props.ContainsKey('req_id')) {
                    $totals.nativeRpcResponses++
                    if ($props.ContainsKey('success') -and
                        $props['success'] -eq $false) {
                        $totals.failedRpcResponses++
                    }
                    $key = [string]$rpcEpoch + ':' + [string]$props['req_id']
                    if ($rpcPending.ContainsKey($key)) {
                        $elapsedMs = ([long]$event.timestamp_ms) - $rpcPending[$key]
                        if ($elapsedMs -ge 0) {
                            $totals.correlatedRpcResponses++
                            $rpcDurations.Add([double]$elapsedMs)
                        }
                        $rpcPending.Remove($key)
                    } else {
                        $totals.orphanRpcResponses++
                    }
                }
                if ($props.ContainsKey('has_identity') -and $props['has_identity'] -eq $true) {
                    $totals.authenticatedStateObservations++
                }
                if ($props.ContainsKey('unread') -and $type -eq 'RAW.COMPUTATION' -and $props['unread'] -gt 0) {
                    $totals.positiveUnreadComputations++
                }
                if ($props.ContainsKey('result') -and $props['result']) {
                    $result = [string]$props['result']
                    if (!$results.ContainsKey($result)) { $results[$result] = 0 }
                    $results[$result]++
                    if ($result -eq 'BLOCKED_NO_NATIVE_DISPATCH') {
                        $totals.nativeDispatchBlocked++
                    }
                }
                if ($props.ContainsKey('action_type') -and $props['action_type']) {
                    $action = [string]$props['action_type']
                    if (!$actions.ContainsKey($action)) { $actions[$action] = 0 }
                    $actions[$action]++
                }
                if ($type -eq 'RAW.DECISION' -and $props.ContainsKey('phase') -and $props['phase'] -eq 'PLANNED') {
                    $totals.plannedActions++
                }
                $phase = if ($props.ContainsKey('new_phase')) { [string]$props['new_phase'] } else { '' }
                if ($phase -eq 'SENT') { $totals.sentPhases++ }
                if ($phase -eq 'ACKNOWLEDGED') { $totals.ackPhases++ }
                if ($phase -eq 'OBSERVED') { $totals.observedPhases++ }
                if ($phase -eq 'CLOSED') { $totals.closedPhases++ }
            }
        } catch {
            $totals.parseErrors++
        }
    }
    $reachedEndOfLedger = $reader.EndOfStream
} finally {
    if ($reader) { $reader.Dispose() }
    if ($file) { $file.Dispose() }
    $sha256.Dispose()
}
function Rows($map) {
    $rows = @()
    foreach ($entry in $map.GetEnumerator()) {
        $rows += [pscustomobject]@{ name=$entry.Key; count=$entry.Value }
    }
    return @($rows | Sort-Object count -Descending)
}
$durationsSorted = @($rpcDurations | Sort-Object)
$p95 = if ($durationsSorted.Count) {
    $idx = [int][Math]::Ceiling($durationsSorted.Count * 0.95) - 1
    $durationsSorted[[Math]::Max(0, $idx)]
} else { $null }
$nativeReadActions = @()
foreach ($entry in $nativeActionTrace.GetEnumerator()) {
    $trace = $entry.Value
    $observedPhases = @($trace.phases)
    $required = @('PLANNED','VERIFIED','AUTHORIZED',
        'DISPATCHED','ACKNOWLEDGED','STATE_CONFIRMED','CLOSED')
    $next = 0
    foreach ($phase in $observedPhases) {
        if ($next -lt $required.Count -and $phase -eq $required[$next]) {
            $next++
        }
    }
    $phaseOrderValid = ($next -eq $required.Count)
    $postconditionValid = ($null -ne $trace.serverDialogCount -and
        $null -ne $trace.localProjectionMatches -and
        $trace.serverDialogCount -gt 0 -and
        $trace.localProjectionMatches -gt 0 -and
        $trace.localProjectionMatches -le $trace.serverDialogCount)
    $e2eVerified = ($phaseOrderValid -and $postconditionValid -and
        $trace.requestId -gt 0 -and $trace.mismatchedRequestIds -eq 0 -and
        $trace.lastResult -eq 'SUCCESS_READ_ONLY')
    $nativeReadActions += [pscustomobject]@{
        actionId=$trace.actionId
        firstSeq=$trace.startSequence
        lastSeq=$trace.lastSequence
        requestId=$trace.requestId
        phases=$observedPhases
        remoteDialogs=$trace.serverDialogCount
        nativeMatchedDialogs=$trace.localProjectionMatches
        lastResult=$trace.lastResult
        phaseOrderValid=$phaseOrderValid
        readOnlyE2EVerified=$e2eVerified
    }
}
$nativeReadActions = @($nativeReadActions | Sort-Object firstSeq)
$e2eCount = @($nativeReadActions | Where-Object {
    $_.readOnlyE2EVerified
}).Count
$exe = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path 'out\Debug\Telegram.exe'
[ordered]@{
    tool='Ω-Telegram native behavior evidence audit'
    profileDirectory=$WorkingDir
    evaluatedAt=(Get-Date).ToString('o')
    executablePresent=(Test-Path $exe)
    ledgerBytes=(Get-Item $ledger).Length
    eventTotals=$totals
    lastSequence=$lastSequence
    lastHash=$lastHash
    reachedEndOfLedger=$reachedEndOfLedger
    evaluatedPrefixIntegrityValid=($totals.parseErrors -eq 0 -and
        $totals.sequenceErrors -eq 0 -and $totals.prevHashLinkErrors -eq 0 -and
        $totals.sha256Errors -eq 0 -and $totals.unsupportedHashVersions -eq 0)
    eventTypes=Rows $types
    actors=Rows $actors
    operationResults=Rows $results
    submittedActionTypes=Rows $actions
    nativeRpcMethods=Rows $rpcMethods
    nativeRpcLatencyP95Ms=$p95
    nativeAPOReadActionCount=$nativeReadActions.Count
    nativeAPOReadClosedVerified=$e2eCount
    nativeAPOReadTraces=@($nativeReadActions | Select-Object -Last 8)
    lastRuntimeUnreadEvents=$lastRuntimeUnreadEvents
    lastRuntimeRepeatedUnreadValues=$lastRuntimeRepeatedUnreadValues
    lastRuntimeLatestUnread=$lastRuntimeLatestUnread
    lastRuntimeRankingEvents=$lastRuntimeRankingEvents
    lastNativeUnreadRanking=$lastNativeUnreadRanking
    rankingShapeErrors=$rankingShapeErrors
    nativeBodyProjectionEvents=$nativeBodyProjectionEvents
    completeNativeBodyProjectionEvents=$completeNativeBodyProjectionEvents
    nativeBodyShapeErrors=$nativeBodyShapeErrors
    lastNativeBodyProjection=$lastNativeBodyProjection
    recoveryPendingEvents=$recoveryPendingEvents
    unmatchedRpcRequestsInLastProcess=$rpcPending.Count
    rpcCorrelationInterpretation='MTProto request/response telemetry is not an APΩ action_id-linked result'
    interpretation='Event types and phase names are telemetry, NOT proof of server ACK or real-world value'
} | ConvertTo-Json -Compress -Depth 6
