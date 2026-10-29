param()
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$fixture = Join-Path $PSScriptRoot 'apo_pts_waiter_smoke'
$qt = (Resolve-Path (Join-Path $repo '..\Libraries\win64\Qt-5.15.19')).Path
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
if (!(Test-Path $vcvars)) { throw 'VS18 Community vcvars64.bat is missing' }
$out = Join-Path $repo 'out\apo_pts_waiter_smoke'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$exe = Join-Path $out 'apo-pts-waiter-smoke.exe'
$includes = @(
  '/I "' + (Join-Path $fixture 'shadow') + '"',
  '/I "' + $fixture + '"',
  '/I "' + (Join-Path $repo 'Telegram\SourceFiles') + '"',
  '/I "' + (Join-Path $repo 'Telegram\lib_base') + '"',
  '/I "' + (Join-Path $qt 'include') + '"',
  '/I "' + (Join-Path $qt 'include\QtCore') + '"'
) -join ' '
$compiler = 'cl /nologo /std:c++20 /EHsc /MTd '
$compiler += '/FI"' + (Join-Path $fixture 'prelude.h') + '" ' + $includes
$compiler += ' "' + (Join-Path $fixture 'main.cpp') + '"'
$compiler += ' "' + (Join-Path $repo 'Telegram\SourceFiles\data\data_pts_waiter.cpp') + '"'
$compiler += ' /Fe:"' + $exe + '" /link /LIBPATH:"' + (Join-Path $qt 'lib') + '"'
$compiler += ' Qt5Cored.lib qtpcre2d.lib ws2_32.lib user32.lib advapi32.lib shell32.lib ole32.lib version.lib netapi32.lib userenv.lib winmm.lib mpr.lib'
$command = 'call "' + $vcvars + '" -vcvars_ver=14.44 >nul && ' + $compiler
Push-Location $out
try {
  & cmd.exe /d /s /c $command
  if ($LASTEXITCODE -ne 0) { throw "PTS waiter smoke compilation failed: $LASTEXITCODE" }
  & $exe
  if ($LASTEXITCODE -ne 0) { throw "PTS waiter smoke assertions failed: $LASTEXITCODE" }
} finally {
  Pop-Location
}
Write-Output '[PASS] BUILD+TEST actual isolated data_pts_waiter.cpp completed'
