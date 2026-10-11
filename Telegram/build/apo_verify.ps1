param(
    [switch]$Fetch,
    [switch]$RunReferenceTests,
    [string]$ReferenceRepo = "C:\Users\pc\Documents\GitHub\telegram-unified-ecosystem"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $repo

function Pass([string]$m) { Write-Host "[PASS] $m" -ForegroundColor Green }
function Fail([string]$m) { Write-Host "[FAIL] $m" -ForegroundColor Red; $script:failed = $true }
function Warn([string]$m) { Write-Host "[WARN] $m" -ForegroundColor Yellow }

$failed = $false
Write-Host "APΩ tdesktop verification"
Write-Host "repo=$repo"

if ($repo -like "$env:TEMP*") { Fail "Repository is under TEMP" } else { Pass "Repository is persistent" }
if ((git rev-parse --show-toplevel) -ne ($repo -replace '\\','/')) { Warn "Git root path representation differs; checking physical path only" }

$origin = git remote get-url origin 2>$null
$upstream = git remote get-url upstream 2>$null
if ($origin -eq "https://github.com/NguyenCuong1989/tdesktop.git") { Pass "origin is canonical writable fork" } else { Fail "origin is not canonical fork: $origin" }
if ($upstream -eq "https://github.com/telegramdesktop/tdesktop.git") { Pass "upstream is official Telegram Desktop" } else { Fail "upstream mismatch: $upstream" }

if ($Fetch) {
    git fetch upstream dev --prune
    git fetch origin --prune
    Pass "Fetched remotes"
}

$status = git status --porcelain
if ([string]::IsNullOrWhiteSpace(($status -join ""))) { Pass "Working tree clean" } else { Fail "Working tree has local changes"; $status }

$head = git rev-parse HEAD
$base = git merge-base HEAD upstream/dev
$upstreamHead = git rev-parse upstream/dev
if ($base -eq $upstreamHead) { Pass "APΩ branch is based on current upstream/dev" } else { Fail "APΩ branch is not based on current upstream/dev" }

$badSubmodules = git submodule status --recursive | Where-Object { $_ -match '^[+\-U]' }
if ($badSubmodules) { Fail "Submodule mismatch detected"; $badSubmodules } else { Pass "All submodules match superproject" }

git diff --check upstream/dev...HEAD
if ($LASTEXITCODE -eq 0) { Pass "git diff --check" } else { Fail "Whitespace/error check failed" }

$required = @(
    "Telegram/SourceFiles/apo/apo_runtime.cpp",
    "Telegram/SourceFiles/apo/apo_behavior_ledger.cpp",
    "Telegram/SourceFiles/apo/apo_verifier.cpp",
    "Telegram/SourceFiles/apo/apo_cognitive_loop.cpp",
    "Telegram/SourceFiles/apo/apo_executor.cpp"
)
foreach ($f in $required) {
    if (Test-Path $f) { Pass "present: $f" } else { Fail "missing: $f" }
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$cl = Get-Command cl -ErrorAction SilentlyContinue
if ($cmake) { Pass "cmake available: $($cmake.Source)" } else { Warn "cmake not on PATH; native build not yet verifiable" }
if ($cl) { Pass "MSVC cl available: $($cl.Source)" } else { Warn "MSVC cl not on PATH; initialize/install official VS 2026 toolchain before native build" }

if ($RunReferenceTests) {
    if (Test-Path $ReferenceRepo) {
        Push-Location $ReferenceRepo
        try {
            $python = if (Test-Path ".\.venv\Scripts\python.exe") { ".\.venv\Scripts\python.exe" } else { "python" }
            & $python -m pytest -q
            if ($LASTEXITCODE -eq 0) { Pass "Reference APΩ Python suite" } else { Fail "Reference APΩ Python suite failed" }
        } finally { Pop-Location }
    } else {
        Fail "Reference repo not found: $ReferenceRepo"
    }
}

if ($failed) { exit 1 }
Write-Host "VERIFICATION_COMPLETE head=$head"
exit 0
