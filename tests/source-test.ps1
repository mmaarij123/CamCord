param([string]$Ffmpeg, [switch]$LiveWindow, [switch]$Hardware)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Ffmpeg) { $Ffmpeg = Join-Path $projectRoot 'third_party\ffmpeg\ffmpeg.exe' }
$Ffmpeg = (Resolve-Path -LiteralPath $Ffmpeg).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ tools are required.' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $projectRoot 'obj\source-tests'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$sources = @('tests\source_tests.cpp','src\CaptureSources.cpp','src\RegionSelector.cpp','src\CaptureEngine.cpp','src\Process.cpp')
$quotedSources = ($sources | ForEach-Object { '"' + (Join-Path $projectRoot $_) + '"' }) -join ' '
Push-Location $build
try {
    $command = '"' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++17 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN ' + $quotedSources + ' /Fe:source_tests.exe /link user32.lib gdi32.lib dwmapi.lib'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Capture-source tests did not compile.' }
    $artifacts = Join-Path $build ('run-' + [guid]::NewGuid().ToString('N'))
    $arguments = @($Ffmpeg, $artifacts)
    if ($LiveWindow) { $arguments += '--live-window' }
    if ($Hardware) { if (-not $LiveWindow) { throw 'Hardware requires -LiveWindow.' }; $arguments += '--hardware' }
    & (Join-Path $build 'source_tests.exe') @arguments
    if ($LASTEXITCODE -ne 0) { throw "Capture-source tests failed. Artifacts: $artifacts" }
    Write-Host "Capture-source test artifacts: $artifacts"
} finally { Pop-Location }
