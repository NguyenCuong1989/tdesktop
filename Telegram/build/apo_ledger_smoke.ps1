param()
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$qt = (Resolve-Path (Join-Path $repo '..\Libraries\win64\Qt-5.15.19')).Path
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
if (!(Test-Path $vcvars)) { throw "MSVC 14.44 developer environment is missing" }
$out = Join-Path $repo 'out\apo_ledger_smoke'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$exe = Join-Path $out 'apo-ledger-smoke.exe'
$source = Join-Path $PSScriptRoot 'apo_ledger_smoke.cpp'
$ledger = Join-Path $out 'valid.jsonl'
$corrupted = Join-Path $out 'corrupted.jsonl'
$include = @(
  '/I "' + (Join-Path $repo 'Telegram\SourceFiles') + '"',
  '/I "' + (Join-Path $qt 'include') + '"',
  '/I "' + (Join-Path $qt 'include\QtCore') + '"'
) -join ' '
$compiler = 'cl /nologo /std:c++20 /EHsc /MTd ' + $include
$compiler += ' "' + $source + '"'
$compiler += ' "' + (Join-Path $repo 'Telegram\SourceFiles\apo\apo_behavior_ledger.cpp') + '"'
$compiler += ' "' + (Join-Path $repo 'Telegram\SourceFiles\apo\apo_state_bridge.cpp') + '"'
$compiler += ' /Fe:"' + $exe + '"'
$compiler += ' /link /LIBPATH:"' + (Join-Path $qt 'lib') + '"'
$compiler += ' Qt5Cored.lib qtpcre2d.lib ws2_32.lib user32.lib advapi32.lib shell32.lib ole32.lib version.lib netapi32.lib userenv.lib winmm.lib mpr.lib'
$cmd = 'call "' + $vcvars + '" -vcvars_ver=14.44 >nul && ' + $compiler
Push-Location $out
try {
  & cmd.exe /d /s /c $cmd
  if ($LASTEXITCODE -ne 0) { throw "Native ledger smoke compilation failed: $LASTEXITCODE" }
} finally {
  Pop-Location
}
function Invoke-LedgerSmoke {
  param(
    [Parameter(Mandatory = $true)][string]$Mode,
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$TimeoutSeconds = 30
  )
  $process = [System.Diagnostics.Process]::Start($exe, ('"{0}" "{1}"' -f $Mode, $Path))
  if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    $process.Kill()
    $process.WaitForExit()
    throw "Native ledger smoke $Mode exceeded ${TimeoutSeconds}s"
  }
  $process.WaitForExit()
  $process.Refresh()
  return $process.ExitCode
}
Remove-Item -LiteralPath $ledger, $corrupted -ErrorAction SilentlyContinue
& $exe 'action-state-domain' $ledger
if ($LASTEXITCODE -ne 0) { throw "Action/state domain binding failed: $LASTEXITCODE" }
Write-Output '[PASS] read-only state evidence cannot authorize native write actions'
& $exe 'storage-policy' $ledger
if ($LASTEXITCODE -ne 0) { throw "Ledger storage policy failed: $LASTEXITCODE" }
Write-Output '[PASS] ledger storage is bounded without deleting evidence'
$mixedLedger = Join-Path $out 'mixed-order-valid.jsonl'
Remove-Item -LiteralPath $mixedLedger -ErrorAction SilentlyContinue
$exitCode = Invoke-LedgerSmoke -Mode 'mixed-order' -Path $mixedLedger
if ($exitCode -ne 0) { throw "Mixed sync/async ledger ordering failed: $exitCode" }
$mixed = @(Get-Content -LiteralPath $mixedLedger | ForEach-Object { $_ | ConvertFrom-Json })
if ($mixed.Count -ne 2 -or
    $mixed[0].trace_id -ne 'async-a' -or
    $mixed[1].trace_id -ne 'sync-b' -or
    [int64]$mixed[0].timestamp_ms -gt [int64]$mixed[1].timestamp_ms -or
    $mixed[1].prev_hash -ne $mixed[0].current_hash) {
    throw 'Mixed sync/async events were not durably sequenced by admission order'
}
foreach ($event in $mixed) {
    $parentTrace = if ($null -eq $event.PSObject.Properties['parent_trace_id']) {
        ''
    } else {
        [string]$event.parent_trace_id
    }
    $payloadJson = $event.payload | ConvertTo-Json -Compress
    $canonical = @(
        [string]$event.hash_version,
        [string]$event.sequence,
        [string]$event.trace_id,
        $parentTrace,
        [string]$event.event_type,
        [string]$event.actor,
        [string]$event.timestamp_ms,
        [string]$event.prev_hash,
        $payloadJson
    ) -join ':'
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $expected = [BitConverter]::ToString(
            $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
    if ($event.current_hash -ne $expected) {
        throw "Mixed ordering event $($event.sequence) hash mismatch"
    }
}
Write-Output '[PASS] sync and async events share admission order, timestamps and hash chain'
$sealLedger = Join-Path $out 'seal-order-valid.jsonl'
Remove-Item -LiteralPath $sealLedger -ErrorAction SilentlyContinue
$exitCode = Invoke-LedgerSmoke -Mode 'seal-order' -Path $sealLedger
if ($exitCode -ne 0) { throw "Ledger shutdown seal failed: $exitCode" }
$sealed = @(Get-Content -LiteralPath $sealLedger | ForEach-Object { $_ | ConvertFrom-Json })
if ($sealed.Count -ne 2 -or
    $sealed[0].trace_id -ne 'async-c' -or
    $sealed[1].trace_id -ne 'shutdown' -or
    $sealed[1].payload.status -ne 'APO_RUNTIME_STOPPED') {
    throw 'Shutdown did not seal after all accepted observations'
}
Write-Output '[PASS] shutdown seal drains accepted events and rejects later admission'
$overflowLedger = Join-Path $out 'overflow-gap-valid.jsonl'
Remove-Item -LiteralPath $overflowLedger -ErrorAction SilentlyContinue
$exitCode = Invoke-LedgerSmoke -Mode 'overflow' -Path $overflowLedger
if ($exitCode -ne 0) { throw "Ledger overflow gap failed: $exitCode" }
$overflow = @(Get-Content -LiteralPath $overflowLedger | ForEach-Object { $_ | ConvertFrom-Json })
if ($overflow.Count -ne 1 -or
    $overflow[0].actor -ne 'apo.behavior_ledger' -or
    $overflow[0].payload.status -ne 'ASYNC_EVIDENCE_GAP') {
    throw 'Overflow did not persist exactly one reserved evidence-gap event'
}
$exitCode = Invoke-LedgerSmoke -Mode 'overflow-reopen' -Path $overflowLedger
if ($exitCode -ne 0) { throw "Ledger gap replay fail-closed check failed: $exitCode" }
Write-Output '[PASS] overflow records one durable gap and replay remains fail-closed'
$failedLedger = Join-Path $out 'write-failure-valid.jsonl'
Remove-Item -LiteralPath $failedLedger -ErrorAction SilentlyContinue
$exitCode = Invoke-LedgerSmoke -Mode 'write-failure' -Path $failedLedger
if ($exitCode -ne 0) { throw "Ledger write failure handling failed: $exitCode" }
if ((Get-Item -LiteralPath $failedLedger).Length -ne 0) {
    throw 'Failed ledger write left a partial event'
}
Write-Output '[PASS] persistence failure wakes sync waiters without partial state'
$asyncLedger = Join-Path $out 'async-valid.jsonl'
Remove-Item -LiteralPath $asyncLedger -ErrorAction SilentlyContinue
& $exe 'async-write' $asyncLedger
if ($LASTEXITCODE -ne 0) { throw "Async ledger writer failed: $LASTEXITCODE" }
Write-Output '[PASS] observational events drain from the bounded async queue'
foreach ($mode in @('write', 'reopen', 'verify4')) {
  & $exe $mode $ledger
  if ($LASTEXITCODE -ne 0) { throw "Native ledger smoke $mode failed: $LASTEXITCODE" }
  Write-Output "[PASS] $mode"
}
$original = [IO.File]::ReadAllText($ledger)
$modified = $original.Replace('"actor":"test.ledger"', '"actor":"tampered.ledger"')
if ($modified -eq $original) { throw "Test setup: ledger tamper sentinel absent" }
[IO.File]::WriteAllText($corrupted, $modified, [Text.UTF8Encoding]::new($false))
& $exe reject $corrupted
if ($LASTEXITCODE -ne 0) { throw "Corrupt ledger wrongly accepted or fell back to default: $LASTEXITCODE" }
Write-Output '[PASS] corrupt ledger refuses further writes'
Write-Output ('[EVIDENCE] original_sha256=' + (Get-FileHash $ledger -Algorithm SHA256).Hash)
$stateLedger = Join-Path $out 'omega-state-valid.jsonl'
$invalidLedger = Join-Path $out 'omega-state-invalid.jsonl'
Remove-Item -LiteralPath $stateLedger, $invalidLedger -ErrorAction SilentlyContinue
foreach ($case in @(
    @{ Mode = 'state-write'; Path = $stateLedger },
    @{ Mode = 'state-reopen'; Path = $stateLedger },
    @{ Mode = 'state-invalid'; Path = $invalidLedger }
)) {
    & $exe $case.Mode $case.Path
    if ($LASTEXITCODE -ne 0) { throw "Omega state evidence $($case.Mode) failed: $LASTEXITCODE" }
    Write-Output "[PASS] $($case.Mode)"
}
& $exe 'dialogs-postcondition' $ledger
if ($LASTEXITCODE -ne 0) { throw "Native dialog postcondition guard failed: $LASTEXITCODE" }
Write-Output '[PASS] dialogs postcondition requires complete server-to-native matching'
$stateBridgeLedger = Join-Path $out 'omega-state-bridge-known-zero-valid.jsonl'
Remove-Item -LiteralPath $stateBridgeLedger -ErrorAction SilentlyContinue
& $exe 'state-bridge-known-zero' $stateBridgeLedger
if ($LASTEXITCODE -ne 0) { throw "StateBridge known-zero/dedup/reset failed: $LASTEXITCODE" }
Write-Output '[PASS] state-bridge-known-zero: loaded Stars/TON zero is known, unchanged values deduplicate, account switch resets state'
$stateBridgeFailureLedger = Join-Path $out 'omega-state-bridge-persistence-failure-valid.jsonl'
Remove-Item -LiteralPath $stateBridgeFailureLedger -ErrorAction SilentlyContinue
& $exe 'state-bridge-persistence-failure' $stateBridgeFailureLedger
if ($LASTEXITCODE -ne 0) { throw "StateBridge persistence fail-closed failed: $LASTEXITCODE" }
Write-Output '[PASS] state-bridge-persistence-failure: unread/Stars/TON remain unpublished when RecordBehavior rejects persistence'
$stateBridgeProfileFailureLedger = Join-Path $out 'omega-state-bridge-profile-persistence-failure-valid.jsonl'
Remove-Item -LiteralPath $stateBridgeProfileFailureLedger -ErrorAction SilentlyContinue
& $exe 'state-bridge-profile-persistence-failure' $stateBridgeProfileFailureLedger
if ($LASTEXITCODE -ne 0) { throw "StateBridge profile persistence fail-closed failed: $LASTEXITCODE" }
$profileFailureEvents = @(Get-Content -LiteralPath $stateBridgeProfileFailureLedger |
    ForEach-Object { $_ | ConvertFrom-Json })
$profileEvents = @($profileFailureEvents | Where-Object { $_.actor -eq 'apo.state_bridge' })
if ($profileFailureEvents.Count -ne 6 -or $profileEvents.Count -ne 1 -or
    $profileEvents[0].payload.has_identity -ne $true -or
    @($profileEvents[0].payload.PSObject.Properties.Name | Sort-Object) -join ',' -ne
        'has_identity,is_premium,profile_updated') {
    throw 'Profile identity ledger payload was not the expected safe marker'
}
$profileFailureRaw = [IO.File]::ReadAllText($stateBridgeProfileFailureLedger)
$profileSecrets = @(
    '+19995550101', '+19995550102', 'synthetic-101', 'synthetic-102',
    '"user_id":101', '"user_id":102', '"account_user_id":101',
    '"account_user_id":102', '"first_name":"Synthetic"',
    '"last_name":"One"', '"last_name":"Two"'
)
if ($profileSecrets | Where-Object { $profileFailureRaw.Contains($_) }) {
    throw 'Synthetic profile phone, ID, or content leaked into the ledger'
}
Write-Output '[PASS] state-bridge-profile-persistence-failure: id=101 persists with native body projection; sealed id=102 switch fails closed to identity=0 with no stale profile/balance/projection; ledger contains one safe identity marker and no synthetic profile data'
& $exe 'send-approval-preview' $ledger
if ($LASTEXITCODE -ne 0) { throw "Native send approval preview failed: $LASTEXITCODE" }
Write-Output '[PASS] native send approval preview retains exact message and target over 120 characters (UI-only)'
& $exe 'account-binding' $ledger
if ($LASTEXITCODE -ne 0) { throw "Native account binding guard failed: $LASTEXITCODE" }
Write-Output '[PASS] native dispatch account binding rejects missing and switched accounts'
& $exe 'account-scope-binding' $ledger
if ($LASTEXITCODE -ne 0) { throw "Native account scope binding failed: $LASTEXITCODE" }
Write-Output '[PASS] native dispatch scope matches account and working directory; rejects foreign ledger state'
$intentLedger = Join-Path $out 'omega-recovery-intent-valid.jsonl'
Remove-Item -LiteralPath $intentLedger -ErrorAction SilentlyContinue
foreach ($mode in @('recovery-intent-write', 'recovery-intent-reopen', 'recovery-intent-still-open')) {
    & $exe $mode $intentLedger
    if ($LASTEXITCODE -ne 0) { throw "Native pre-dispatch recovery $mode failed: $LASTEXITCODE" }
    Write-Output "[PASS] $mode"
}
$ambiguousLedger = Join-Path $out 'omega-recovery-ambiguous-valid.jsonl'
Remove-Item -LiteralPath $ambiguousLedger -ErrorAction SilentlyContinue
foreach ($mode in @('recovery-ambiguous-write', 'recovery-ambiguous-reopen')) {
    & $exe $mode $ambiguousLedger
    if ($LASTEXITCODE -ne 0) { throw "Ambiguous RPC recovery $mode failed: $LASTEXITCODE" }
    Write-Output "[PASS] $mode"
}
$writeRecoveryLedger = Join-Path $out 'omega-native-send-recovery-valid.jsonl'
Remove-Item -LiteralPath $writeRecoveryLedger -ErrorAction SilentlyContinue
foreach ($mode in @(
    'send-unknown-write',
    'send-unknown-reopen',
    'send-observed-reconcile',
    'send-closed-reopen'
)) {
    & $exe $mode $writeRecoveryLedger
    if ($LASTEXITCODE -ne 0) {
        throw "Native write recovery $mode failed: $LASTEXITCODE"
    }
    Write-Output "[PASS] $mode"
}
$dispatchGapLedger = Join-Path $out 'omega-native-send-dispatch-gap-valid.jsonl'
Remove-Item -LiteralPath $dispatchGapLedger -ErrorAction SilentlyContinue
foreach ($mode in @('send-ack-without-dispatched-write', 'send-ack-without-dispatched-reopen')) {
    & $exe $mode $dispatchGapLedger
    if ($LASTEXITCODE -ne 0) {
        throw "Native send dispatch gap recovery $mode failed: $LASTEXITCODE"
    }
    Write-Output "[PASS] $mode"
}
$recoveryLedger = Join-Path $out 'omega-recovery-valid.jsonl'
Remove-Item -LiteralPath $recoveryLedger -ErrorAction SilentlyContinue
foreach ($mode in @('recovery-write', 'recovery-reopen', 'recovery-closed')) {
    & $exe $mode $recoveryLedger
    if ($LASTEXITCODE -ne 0) { throw "Native recovery ledger $mode failed: $LASTEXITCODE" }
    Write-Output "[PASS] $mode"
}
& $exe 'native-body-contract' $ledger
if ($LASTEXITCODE -ne 0) { throw "Native body projection contract invalid: $LASTEXITCODE" }
Write-Output '[PASS] native body perception contract is account-scoped and read-only'
$nativeBodyLedger = Join-Path $out 'omega-native-body-bridge-valid.jsonl'
Remove-Item -LiteralPath $nativeBodyLedger -ErrorAction SilentlyContinue
& $exe 'native-body-bridge' $nativeBodyLedger
if ($LASTEXITCODE -ne 0) { throw "Native StateBridge account-projection integration failed: $LASTEXITCODE" }
Write-Output '[PASS] native body bridge persists account-scoped projection and resets on account switch'
$traceLedger = Join-Path $out 'omega-generated-trace-valid.jsonl'
Remove-Item -LiteralPath $traceLedger -ErrorAction SilentlyContinue
& $exe 'generated-trace-unique' $traceLedger
if ($LASTEXITCODE -ne 0) { throw "Generated trace uniqueness failed: $LASTEXITCODE" }
$traceIds = Get-Content -LiteralPath $traceLedger | ForEach-Object { ($_ | ConvertFrom-Json).trace_id }
if ($traceIds.Count -ne 2 -or $traceIds[0] -eq $traceIds[1]) {
    throw 'Generated trace IDs are not unique'
}
Write-Output '[PASS] generated trace IDs are unique across adjacent events'
$upgradeLedger = Join-Path $out 'omega-v1-to-v2-upgrade-valid.jsonl'
Remove-Item -LiteralPath $upgradeLedger -ErrorAction SilentlyContinue
$legacyPayload = '{"kind":"HASH_V1_FIXTURE"}'
$legacyCanonical = '1:v1-fixture:RAW.COMPUTATION:test.ledger:' + ('0' * 64) + ':' + $legacyPayload
$legacyBytes = [Text.Encoding]::UTF8.GetBytes($legacyCanonical)
$legacySha = [Security.Cryptography.SHA256]::Create()
try {
    $legacyHash = [BitConverter]::ToString(
        $legacySha.ComputeHash($legacyBytes)).Replace('-', '').ToLowerInvariant()
} finally {
    $legacySha.Dispose()
}
$legacyLine = '{"actor":"test.ledger","current_hash":"' + $legacyHash +
    '","event_type":"RAW.COMPUTATION","payload":' + $legacyPayload +
    ',"prev_hash":"' + ('0' * 64) +
    '","sequence":1,"timestamp_ms":1000,"trace_id":"v1-fixture"}'
[IO.File]::WriteAllText($upgradeLedger, $legacyLine + "`n",
    [Text.UTF8Encoding]::new($false))
& $exe 'append-legacy-v1' $upgradeLedger
if ($LASTEXITCODE -ne 0) { throw "Ledger v1-to-v2 upgrade failed: $LASTEXITCODE" }
$upgraded = Get-Content -LiteralPath $upgradeLedger | ForEach-Object { $_ | ConvertFrom-Json }
if ($upgraded.Count -ne 2 -or
    $null -ne $upgraded[0].PSObject.Properties['hash_version'] -or
    $upgraded[1].hash_version -ne 2 -or
    $upgraded[1].parent_trace_id -ne 'v1-fixture') {
    throw 'Ledger v1-to-v2 upgrade did not preserve the expected hash schema boundary'
}
Write-Output '[PASS] verified hash v1 ledger upgrades in place to metadata-bound v2 events'
Write-Output '[PASS] native ledger smoke suite completed'
