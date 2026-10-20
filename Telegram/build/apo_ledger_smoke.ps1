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
$compiler += ' /Fe:"' + $exe + '"'
$compiler += ' /link /LIBPATH:"' + (Join-Path $qt 'lib') + '"'
$compiler += ' Qt5Cored.lib qtpcre2d.lib ws2_32.lib user32.lib advapi32.lib shell32.lib ole32.lib version.lib netapi32.lib userenv.lib winmm.lib'
$cmd = 'call "' + $vcvars + '" -vcvars_ver=14.44 >nul && ' + $compiler
Push-Location $out
try {
  & cmd.exe /d /s /c $cmd
  if ($LASTEXITCODE -ne 0) { throw "Native ledger smoke compilation failed: $LASTEXITCODE" }
} finally {
  Pop-Location
}
Remove-Item -LiteralPath $ledger, $corrupted -ErrorAction SilentlyContinue
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
$recoveryLedger = Join-Path $out 'omega-recovery-valid.jsonl'
Remove-Item -LiteralPath $recoveryLedger -ErrorAction SilentlyContinue
foreach ($mode in @('recovery-write', 'recovery-reopen', 'recovery-closed')) {
    & $exe $mode $recoveryLedger
    if ($LASTEXITCODE -ne 0) { throw "Native recovery ledger $mode failed: $LASTEXITCODE" }
    Write-Output "[PASS] $mode"
}
Write-Output '[PASS] native ledger smoke 9/9'
