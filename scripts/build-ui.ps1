param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not (Get-Command npm.cmd -ErrorAction SilentlyContinue)) { throw 'Install Node.js 22.12+ (including npm) before building the UI.' }
Push-Location (Join-Path $projectRoot 'ui')
try {
    & npm.cmd ci --no-audit --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'UI dependency installation failed.' }
    & npm.cmd run build
    if ($LASTEXITCODE -ne 0) { throw 'React UI build failed.' }
} finally { Pop-Location }
& (Join-Path $PSScriptRoot 'stage-runtime.ps1') -Configuration $Configuration
