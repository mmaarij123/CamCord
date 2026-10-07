param([string]$Version = '1.3.0', [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be numeric major.minor.patch.' }
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build-release.ps1') }
& (Join-Path $PSScriptRoot 'setup-webview2-runtime.ps1')
$candidates = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")
$compiler = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $compiler) { $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue; if ($command) { $compiler = $command.Source } }
if (-not $compiler) { throw 'Install Inno Setup 6 from https://jrsoftware.org/isdl.php, then rerun this script.' }
& $compiler "/DMyAppVersion=$Version" (Join-Path $projectRoot 'installer\CamCord.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
$setup = Join-Path $projectRoot 'dist\CamCord-Setup.exe'
Write-Host "Setup ready: $setup"
Get-FileHash -LiteralPath $setup -Algorithm SHA256
