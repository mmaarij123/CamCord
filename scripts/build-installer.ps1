param([string]$Version, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourceVersion = (Get-Content -LiteralPath (Join-Path $projectRoot 'ui\package.json') -Raw | ConvertFrom-Json).version
if (-not $Version) { $Version = $sourceVersion }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be numeric major.minor.patch.' }
if ($Version -ne $sourceVersion) { throw 'Installer version must match the source package version.' }
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build-release.ps1') }
$app = Get-Item -LiteralPath (Join-Path $projectRoot 'bin\Release\CamCord.exe')
if ($app.VersionInfo.ProductVersion -ne $Version) { throw 'The Release executable is stale. Rebuild before packaging this version.' }
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
