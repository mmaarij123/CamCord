param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $PSScriptRoot 'setup-ffmpeg.ps1')
& (Join-Path $PSScriptRoot 'setup-webview2.ps1')
& (Join-Path $PSScriptRoot 'build-ui.ps1') -Configuration $Configuration
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio or Build Tools with Desktop development with C++.' }
$msbuild = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild and the x64 C++ compiler were not found.' }
& $msbuild (Join-Path $projectRoot 'CamCord.sln') /m "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'CamCord native build failed.' }
Write-Host "CamCord is ready in bin\$Configuration."
