# Re-run native Telegram UI checks with the upstream -testagent harness.
param([ValidateRange(20,180)][int]$TimeoutSeconds=80)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$relative='Telegram/SourceFiles/test/test_scenario.cpp'
$slot=Join-Path $repo 'Telegram\SourceFiles\test\test_scenario.cpp'
$scenario=Join-Path $PSScriptRoot 'apo_native_ui_smoke_scenario.cpp'
$vcvars='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$project=Join-Path $repo 'out\Telegram\Telegram.vcxproj'
$fixture=Join-Path $repo 'out\apo_native_ui_smoke_fixture'
$runDir=Join-Path $fixture ('run-'+[Guid]::NewGuid().ToString('N'))
$binaryDir=Join-Path $runDir 'bin'
$binary=Join-Path $binaryDir 'Telegram.exe'
$profile=Join-Path $runDir 'profile'
$skipReferences=Join-Path $runDir 'skip_project_references.targets'
$evidence=Join-Path $runDir 'test_evidence'
$testLog=Join-Path $evidence 'test_log.txt'
$buildLog=Join-Path $runDir 'overlay_build.log'
$restoreLog=Join-Path $runDir 'restored_build.log'
$backup=Join-Path $runDir 'original_test_scenario.cpp'

if(!(Test-Path $scenario) -or !(Test-Path $vcvars) -or !(Test-Path $project)){
    throw 'Missing scenario source, generated Telegram.vcxproj, or MSVC toolchain'
}
if(@(Get-Process MSBuild -ErrorAction SilentlyContinue).Count -ne 0){
    throw 'Another MSBuild process is active; refusing to start a competing build'
}
& git -C $repo diff --quiet HEAD -- $relative
if($LASTEXITCODE -ne 0){
    throw 'Indexed or working-tree changes in upstream test scenario'
}
if(!([IO.File]::ReadAllText($slot)).Contains('The repository copy must stay a no-op.')){
    throw 'Unexpected test_scenario.cpp contents'
}
$original=[IO.File]::ReadAllBytes($slot)
$originalHash=(Get-FileHash $slot -Algorithm SHA256).Hash
$scenarioHash=(Get-FileHash $scenario -Algorithm SHA256).Hash
$head=(& git -C $repo rev-parse HEAD).Trim()
if(Test-Path -LiteralPath $fixture){
    $fixtureItem=Get-Item -LiteralPath $fixture -Force
    if(!$fixtureItem.PSIsContainer -or
            (($fixtureItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)){
        throw 'Fixture root is not a regular directory'
    }
} else {
    [IO.Directory]::CreateDirectory($fixture)|Out-Null
}
if(Test-Path -LiteralPath $runDir){throw 'Generated run directory already exists'}
[IO.Directory]::CreateDirectory($runDir)|Out-Null
$runItem=Get-Item -LiteralPath $runDir -Force
if(($runItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
    throw 'Generated run directory is a reparse point'
}
[IO.File]::WriteAllBytes($backup,$original)
if((Get-FileHash $backup -Algorithm SHA256).Hash -ne $originalHash){
    throw 'Run-scoped backup does not match the original test scenario'
}
[IO.File]::WriteAllText($skipReferences,@'
<Project>
  <ItemGroup>
    <ProjectReference Remove="@(ProjectReference)" />
  </ItemGroup>
  <ItemDefinitionGroup>
    <Link>
      <MinimalRebuildFromTracking>false</MinimalRebuildFromTracking>
    </Link>
  </ItemDefinitionGroup>
</Project>
'@,[Text.UTF8Encoding]::new($false))
[IO.Directory]::CreateDirectory($profile)|Out-Null
[IO.File]::WriteAllText((Join-Path $profile 'testing'),'disposable product test',[Text.UTF8Encoding]::new($false))

function Build-Telegram([string]$logPath){
    $outDir=($binaryDir -replace '\\','/')+'/'
    $afterTargets=($skipReferences -replace '\\','/')
    $properties='/m:1 /nr:false '+
        '/p:Configuration=Debug /p:Platform=x64 '+
        '/p:OutDir="'+$outDir+'" /p:BuildProjectReferences=false '+
        '/p:CustomAfterMicrosoftCommonTargets="'+$afterTargets+'" '+
        '/p:CL_MPCount=1'
    $compileLine='call "'+$vcvars+'" -vcvars_ver=14.44 >nul && '+
        'MSBuild.exe "'+$project+'" /t:ClCompile '+$properties+
        ' > "'+$logPath+'" 2>&1'
    & cmd.exe /d /s /c $compileLine
    if($LASTEXITCODE -eq 0){
        $linkLine='call "'+$vcvars+'" -vcvars_ver=14.44 >nul && '+
        'MSBuild.exe "'+$project+'" /t:_Link '+$properties+
        ' /p:ForceRebuild=true >> "'+$logPath+'" 2>&1'
        & cmd.exe /d /s /c $linkLine
    }
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
    $existing=@(Get-Process Telegram -ErrorAction SilentlyContinue|
        Where-Object {$_.Path -ieq $binary})
    if($existing.Count -ne 0){throw 'Disposable product test already running'}
    $env:TDESKTOP_TEST_EVIDENCE_DIR=$evidence
    $env:TDESKTOP_TEST_WATCHDOG=[string]([Math]::Min(120,$TimeoutSeconds))
    $p=Start-Process -FilePath $binary -ArgumentList @('-workdir',$profile,'-testagent','-noupdate','-debug') -WorkingDirectory (Split-Path $binary) -PassThru
    $finished=$p.WaitForExit($TimeoutSeconds*1000)
    if(!$finished){
        $check=Get-Process -Id $p.Id -ErrorAction SilentlyContinue
        if($check -and $check.Path -ieq $binary){
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
        'TEST_RESULT: PASS: AP native ledger uses this Telegram working directory',
        'TEST_RESULT: PASS: Omega Telegram native application name is independent',
        'TEST_RESULT: PASS: Omega Telegram identity is visible in native window title',
        'TEST_RESULT: PASS: native Telegram painted frame is nonblank',
        'TEST_RESULT: PASS: Telegram enters its real QR login step',
        'TEST_RESULT: PASS: entering login UI never fabricates authorization',
        'TEST_RESULT: PASS: native Ω(P) accepts the sealed convergent fixture',
        'TEST_RESULT: PASS: native Ω(P) rejects a mathematically out-of-domain state',
        'TEST_RESULT: PASS: native Ω(P) rejects infinite state components',
        'TEST_RESULT: PASS: native Ω(P) rejects NaN state components',
        'TEST_RESULT: PASS: unchanged unread count does not append another ledger event',
        'TEST_RESULT: PASS: changed unread count appends exactly one ledger event',
        'TEST_RESULT: PASS: invalid negative unread count is ignored',
        'TEST_RESULT: PASS: anonymous cognitive fixture has no authorized account',
        'TEST_RESULT: PASS: anonymous step records one observation but no fabricated action',
        'TEST_RESULT: PASS: native RPC inspection rejects unauthenticated client without dispatch',
        'TEST_RESULT: PASS: unwired native dispatch must complete with failure not success'
    )
    foreach($marker in $required){
        if(!$content.Contains($marker)){throw ("Missing product observation: "+$marker)}
    }
    if($p.ExitCode -ne 0){throw "Product test exited code $($p.ExitCode)"}
    $passed=$true
} finally {
    Remove-Item Env:\TDESKTOP_TEST_EVIDENCE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\TDESKTOP_TEST_WATCHDOG -ErrorAction SilentlyContinue
    # Preserve unexpected concurrent edits instead of clobbering them.
    $atRestoreHash=(Get-FileHash $slot -Algorithm SHA256).Hash
    if($atRestoreHash -eq $scenarioHash){
        [IO.File]::WriteAllBytes($slot,$original)
    } elseif($atRestoreHash -ne $originalHash){
        throw ("Test scenario changed concurrently; refusing overwrite. "+
            "Original bytes preserved at: "+$backup)
    }
    if((Get-FileHash $slot -Algorithm SHA256).Hash -ne $originalHash){
        throw 'Native test source was not exactly restored'
    }
    Build-Telegram $restoreLog
    & git -C $repo diff --quiet HEAD -- $relative
    if($LASTEXITCODE -ne 0){throw 'Restored source differs from Git HEAD'}
}
if(!$passed){throw 'Native product test did not PASS'}
Write-Output ('PRODUCT_NATIVE_UI_PASS source='+$head+' scenario_source_sha256='+$scenarioHash+' test_binary_sha256='+$binaryTestHash)
Write-Output ('RESTORED_SOURCE_SHA256='+$originalHash)
Write-Output ('RESTORED_FIXTURE_BINARY_SHA256='+(Get-FileHash $binary -Algorithm SHA256).Hash)
Write-Output ('RUN_DIRECTORY='+$runDir)
Write-Output ('PRODUCT_TEST_EVIDENCE='+$testLog)
Write-Output ('OVERLAY_BUILD_LOG='+$buildLog)
Write-Output ('RESTORED_BUILD_LOG='+$restoreLog)
