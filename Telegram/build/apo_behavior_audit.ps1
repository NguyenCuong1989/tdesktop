# Ω-Telegram native behavioral evidence audit.
# Reads ONLY structural fields: no phone, message body, tokens, or account identifiers.
param(
    [string]$WorkingDir = '',
    [int]$MaxEvents = 200000
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($WorkingDir)) {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    $WorkingDir = Join-Path $repo 'out\omega-native-authorized'
}
$ledger = Join-Path $WorkingDir 'apo\raw_behavior.jsonl'
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
$lastUnreadValue = $null
$lastRuntimeUnreadEvents = 0
$lastRuntimeRepeatedUnreadValues = 0
$lastRuntimeLatestUnread = $null
$lastNativeUnreadRanking = $null
$lastRuntimeRankingEvents = 0
$rankingShapeErrors = 0
$totals = [ordered]@{
    eventCount=0
    parseErrors=0
    sequenceErrors=0
    prevHashLinkErrors=0
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
            if ($sequence -ne ($lastSequence + 1)) { $totals.sequenceErrors++ }
            if ($prev -ne $lastHash) { $totals.prevHashLinkErrors++ }
            $lastSequence = $sequence
            $lastHash = [string]$event.current_hash
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
} finally {
    if ($reader) { $reader.Dispose() }
    if ($file) { $file.Dispose() }
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
$exe = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path 'out\Debug\Telegram.exe'
[ordered]@{
    tool='Ω-Telegram native behavior evidence audit'
    profileDirectory=$WorkingDir
    evaluatedAt=(Get-Date).ToString('o')
    executablePresent=(Test-Path $exe)
    ledgerBytes=(Get-Item $ledger).Length
    eventTotals=$totals
    lastSequence=$lastSequence
    eventTypes=Rows $types
    actors=Rows $actors
    operationResults=Rows $results
    submittedActionTypes=Rows $actions
    nativeRpcMethods=Rows $rpcMethods
    nativeRpcLatencyP95Ms=$p95
    lastRuntimeUnreadEvents=$lastRuntimeUnreadEvents
    lastRuntimeRepeatedUnreadValues=$lastRuntimeRepeatedUnreadValues
    lastRuntimeLatestUnread=$lastRuntimeLatestUnread
    lastRuntimeRankingEvents=$lastRuntimeRankingEvents
    lastNativeUnreadRanking=$lastNativeUnreadRanking
    rankingShapeErrors=$rankingShapeErrors
    unmatchedRpcRequestsInLastProcess=$rpcPending.Count
    rpcCorrelationInterpretation='MTProto request/response telemetry is not an APΩ action_id-linked result'
    interpretation='Event types and phase names are telemetry, NOT proof of server ACK or real-world value'
} | ConvertTo-Json -Compress -Depth 6
