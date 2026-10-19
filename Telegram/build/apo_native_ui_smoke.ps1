# Re-run native Telegram UI checks with the upstream -testagent harness.
param([ValidateRange(20,180)][int]$TimeoutSeconds=80)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$relative='Telegram/SourceFiles/test/test_scenario.cpp'
$slot=Join-Path $repo 'Telegram\SourceFiles\test\test_scenario.cpp'
$scenario=Join-Path $PSScriptRoot 'apo_native_ui_smoke_scenario.cpp'
$vcvars='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$binary=Join-Path $repo 'out\Debug\Telegram.exe'
$fixture=Join-Path $repo 'out\apo_native_ui_smoke_fixture'
$evidence=Join-Path $fixture 'test_evidence'
$testLog=Join-Path $evidence 'test_log.txt'
$buildLog=Join-Path $repo 'out\apo_native_ui_smoke_overlay_build.log'
$restoreLog=Join-Path $repo 'out\apo_native_ui_smoke_restored_build.log'
$backup=Join-Path $repo 'out\apo_native_ui_smoke_original.cpp'

if(!(Test-Path $scenario) -or !(Test-Path $vcvars)){throw 'Missing scenario source or MSVC toolchain'}
& git -C $repo diff --quiet -- $relative
if($LASTEXITCODE -ne 0){throw 'Uncommitted changes in upstream test scenario'}
if(!([IO.File]::ReadAllText($slot)).Contains('The repository copy must stay a no-op.')){
    throw 'Unexpected test_scenario.cpp contents'
}
$original=[IO.File]::ReadAllBytes($slot)
[IO.File]::WriteAllBytes($backup,$original)
$originalHash=(Get-FileHash $backup -Algorithm SHA256).Hash
$head=(& git -C $repo rev-parse HEAD).Trim()
[IO.Directory]::CreateDirectory($fixture)|Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'testing'),'disposable product test',[Text.UTF8Encoding]::new($false))

function Build-Telegram([string]$logPath){
    $line='call "'+$vcvars+'" -vcvars_ver=14.44 >nul && '+
        'cmake --build "'+(Join-Path $repo 'out')+'" --config Debug '+
        '--target Telegram --parallel 2 -- /p:CL_MPCount=3 /nr:false > "'+$logPath+'" 2>&1'
    & cmd.exe /d /s /c $line
    if($LASTEXITCODE -ne 0){
        $tail=(Get-Content $logPath -Tail 22) -join [Environment]::NewLine
        throw ('Native Telegram build failed: '+[Environment]::NewLine+$tail)
    }
}

$passed=$false
$binaryTestHash=''
try {
    [IO.File]::WriteAllBytes($slot,[IO.File]::ReadAllBytes($scenario))
    Build-Telegram $buildLog
    $binaryTestHash=(Get-FileHash $binary -Algorithm SHA256).Hash
    $existing=@(Get-CimInstance Win32_Process -Filter "Name='Telegram.exe'"|
        Where-Object {$_.CommandLine -like "*$fixture*"})
    if($existing.Count -ne 0){throw 'Disposable product test already running'}
    $env:TDESKTOP_TEST_EVIDENCE_DIR=$evidence
    $env:TDESKTOP_TEST_WATCHDOG=[string]([Math]::Min(120,$TimeoutSeconds))
    $p=Start-Process -FilePath $binary -ArgumentList @('-workdir',$fixture,'-testagent','-noupdate','-debug') -WorkingDirectory (Split-Path $binary) -PassThru
    $finished=$p.WaitForExit($TimeoutSeconds*1000)
    if(!$finished){
        $check=Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)"
        if($check -and $check.ExecutablePath -ieq $binary -and $check.CommandLine -like "*$fixture*"){
            $p.CloseMainWindow()|Out-Null
            if(!$p.WaitForExit(4000)){Stop-Process -Id $p.Id -Force}
        }
        throw 'Native Telegram product test exceeded deadline'
    }
    if(!(Test-Path $testLog)){throw 'Product generated no test_log.txt'}
    $content=[IO.File]::ReadAllText($testLog)
    $required=@(
        'SCENARIO_RESULT: PASS (failures: 0)',
        'TEST_COMPLETE',
        'TEST_RESULT: PASS: AP native runtime is running inside Telegram.exe',
        'TEST_RESULT: PASS: Omega Telegram native application name is independent',
        'TEST_RESULT: PASS: Omega Telegram identity is visible in native window title',
        'TEST_RESULT: PASS: native Telegram painted frame is nonblank',
        'TEST_RESULT: PASS: Telegram enters its real QR login step',
        'TEST_RESULT: PASS: entering login UI never fabricates authorization'
    )
    foreach($marker in $required){
        if(!$content.Contains($marker)){throw ("Missing product observation: "+$marker)}
    }
    if($p.ExitCode -ne 0){throw "Product test exited code $($p.ExitCode)"}
    $passed=$true
} finally {
    Remove-Item Env:\TDESKTOP_TEST_EVIDENCE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\TDESKTOP_TEST_WATCHDOG -ErrorAction SilentlyContinue
    [IO.File]::WriteAllBytes($slot,$original)
    if((Get-FileHash $slot -Algorithm SHA256).Hash -ne $originalHash){
        throw 'Native test source was not exactly restored'
    }
    Build-Telegram $restoreLog
    & git -C $repo diff --quiet -- $relative
    if($LASTEXITCODE -ne 0){throw 'Restored source differs from Git HEAD'}
}
if(!$passed){throw 'Native product test did not PASS'}
Write-Output ('PRODUCT_NATIVE_UI_PASS source='+$head+' test_binary_sha256='+$binaryTestHash)
Write-Output ('NORMAL_BINARY_SHA256='+(Get-FileHash $binary -Algorithm SHA256).Hash)
Write-Output ('PRODUCT_TEST_EVIDENCE='+$testLog)
Write-Output ('OVERLAY_BUILD_LOG='+$buildLog)
Write-Output ('RESTORED_BUILD_LOG='+$restoreLog)
