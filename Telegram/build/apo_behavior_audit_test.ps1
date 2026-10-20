$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$audit = Join-Path $PSScriptRoot 'apo_behavior_audit.ps1'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'apo-behavior-audit-' + [Guid]::NewGuid().ToString('N'))
$validLedger = Join-Path $testRoot 'valid.jsonl'
$tamperedLedger = Join-Path $testRoot 'tampered.jsonl'
[void][IO.Directory]::CreateDirectory($testRoot)

function New-LedgerEvent(
    [long]$Sequence,
    [string]$TraceId,
    [string]$Type,
    [string]$Actor,
    [string]$PayloadJson,
    [string]$PreviousHash,
    [int]$HashVersion = 1,
    [long]$TimestampMs = 1000,
    [string]$ParentTraceId = ''
) {
    $canonical = if ($HashVersion -eq 1) {
        '{0}:{1}:{2}:{3}:{4}:{5}' -f $Sequence, $TraceId,
            $Type, $Actor, $PreviousHash, $PayloadJson
    } elseif ($HashVersion -eq 2) {
        '{0}:{1}:{2}:{3}:{4}:{5}:{6}:{7}:{8}' -f $HashVersion,
            $Sequence, $TraceId, $ParentTraceId, $Type, $Actor,
            $TimestampMs, $PreviousHash, $PayloadJson
    } else {
        throw "Unsupported fixture hash version: $HashVersion"
    }
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = [Convert]::ToHexString($sha256.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($canonical))).ToLowerInvariant()
    } finally {
        $sha256.Dispose()
    }
    $parentJson = if ($ParentTraceId) {
        ',"parent_trace_id":"' + $ParentTraceId + '"'
    } else { '' }
    $versionJson = if ($HashVersion -eq 2) { ',"hash_version":2' } else { '' }
    $line = '{"actor":"' + $Actor + '","current_hash":"' + $hash +
        '","event_type":"' + $Type + '"' + $versionJson + $parentJson +
        ',"payload":' + $PayloadJson +
        ',"prev_hash":"' + $PreviousHash + '","sequence":' + $Sequence +
        ',"timestamp_ms":' + $TimestampMs + ',"trace_id":"' + $TraceId + '"}'
    return [pscustomobject]@{ Line=$line; Hash=$hash }
}

try {
    $zero = '0' * 64
    $bodyPayload = '{"account_scope":"fixture","basic_groups":1,' +
        '"broadcast_channels":1,"complete":true,"history_rows":4,' +
        '"inspected_rows":4,"kind":"NATIVE_BODY_PROJECTION",' +
        '"known_unread_rows":4,"measured_at_ms":1000,"mentioned_dialogs":1,' +
        '"source":"Data::Session.chatsList/History::peer",' +
        '"source_loaded":true,"source_trace_id":"body",' +
        '"supergroups":1,"total_loaded_rows":4,"unclassified_peers":0,' +
        '"unread_dialogs":2,"user_dialogs":1}'
    $first = New-LedgerEvent 1 'body' 'RAW.STATE_REDUCE' `
        'apo.state_bridge.native_body' $bodyPayload $zero
    $recoveryPayload = '{"evidence":{"prev_hash":"nested-fixture"},' +
        '"kind":"NATIVE_READ_RECOVERY_PENDING",' +
        '"new_phase":"RECONCILING","result":"OUTCOME_UNRESOLVED"}'
    $second = New-LedgerEvent 2 'recovery' 'RAW.COMPUTATION' `
        'apo.cognitive.recovery' $recoveryPayload $first.Hash 2 1001 'body'
    [IO.File]::WriteAllText($validLedger,
        $first.Line + "`n" + $second.Line + "`n",
        [Text.UTF8Encoding]::new($false))

    $valid = (& $audit -LedgerPath $validLedger -MaxEvents 10) |
        ConvertFrom-Json
    if (!$valid.evaluatedPrefixIntegrityValid -or
        $valid.eventTotals.sha256Errors -ne 0 -or
        $valid.eventTotals.hashVersion1Events -ne 1 -or
        $valid.eventTotals.hashVersion2Events -ne 1 -or
        $valid.nativeBodyProjectionEvents -ne 1 -or
        $valid.completeNativeBodyProjectionEvents -ne 1 -or
        $valid.nativeBodyShapeErrors -ne 0 -or
        !$valid.lastNativeBodyProjection.shapeValid -or
        $valid.recoveryPendingEvents -ne 1) {
        throw 'Valid audit fixture was not accepted with body and recovery evidence'
    }

    $tampered = ($first.Line -replace '"user_dialogs":1', '"user_dialogs":2')
    [IO.File]::WriteAllText($tamperedLedger, $tampered + "`n",
        [Text.UTF8Encoding]::new($false))
    $invalid = (& $audit -LedgerPath $tamperedLedger -MaxEvents 10) |
        ConvertFrom-Json
    if ($invalid.evaluatedPrefixIntegrityValid -or
        $invalid.eventTotals.sha256Errors -ne 1 -or
        $invalid.nativeBodyShapeErrors -ne 1) {
        throw 'Tampered audit fixture did not fail hash and body-shape verification'
    }
    $tamperedV2 = ($second.Line -replace '"timestamp_ms":1001', '"timestamp_ms":1002')
    [IO.File]::WriteAllText($tamperedLedger, $first.Line + "`n" +
        $tamperedV2 + "`n", [Text.UTF8Encoding]::new($false))
    $invalidV2 = (& $audit -LedgerPath $tamperedLedger -MaxEvents 10) |
        ConvertFrom-Json
    if ($invalidV2.evaluatedPrefixIntegrityValid -or
        $invalidV2.eventTotals.sha256Errors -ne 1) {
        throw 'Hash v2 did not detect tampered timestamp metadata'
    }
    Write-Output '[PASS] behavior audit verifies mixed hash versions, v2 metadata, body shape and recovery evidence'
} finally {
    Remove-Item -LiteralPath $validLedger, $tamperedLedger -Force `
        -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $testRoot -Force -ErrorAction SilentlyContinue
}
