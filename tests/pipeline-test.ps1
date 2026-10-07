param([string]$Ffmpeg)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Ffmpeg) { $Ffmpeg = Join-Path $projectRoot 'third_party\ffmpeg\ffmpeg.exe' }
if (-not (Test-Path -LiteralPath $Ffmpeg)) { throw 'Pass -Ffmpeg with the path to ffmpeg.exe.' }
$Ffmpeg = (Resolve-Path -LiteralPath $Ffmpeg).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ build tools are required.' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $projectRoot 'obj\pipeline-tests'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$sources = @('tests\pipeline_tests.cpp', 'src\RecordingManager.cpp', 'src\CaptureEngine.cpp', 'src\HardwareEncoderDetector.cpp', 'src\OutputManager.cpp', 'src\Process.cpp')
$quotedSources = ($sources | ForEach-Object { '"' + (Join-Path $projectRoot $_) + '"' }) -join ' '
Push-Location $build
try {
    $command = '"' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++17 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN ' + $quotedSources + ' /Fe:pipeline_tests.exe /link ole32.lib shell32.lib uuid.lib avrt.lib'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Pipeline test compile failed.' }
    $artifacts = Join-Path $build ('run-' + [guid]::NewGuid().ToString('N'))
    & (Join-Path $build 'pipeline_tests.exe') $Ffmpeg $artifacts
    if ($LASTEXITCODE -ne 0) { throw "Pipeline tests failed. Artifacts: $artifacts" }
    Write-Host "Synthetic pipeline test artifacts: $artifacts"
} finally { Pop-Location }
