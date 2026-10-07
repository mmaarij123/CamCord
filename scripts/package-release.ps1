param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build-release.ps1') }
$distribution = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Force -Path $distribution | Out-Null
& (Join-Path $PSScriptRoot 'build-installer.ps1') -SkipBuild
