param([string]$Ffmpeg, [ValidateSet('h264_nvenc','h264_amf','h264_qsv')][string]$HardwareEncoder)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Ffmpeg) { $Ffmpeg = Join-Path $projectRoot 'third_party\ffmpeg\ffmpeg.exe' }
$Ffmpeg = (Resolve-Path -LiteralPath $Ffmpeg).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ tools are required.' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $projectRoot 'obj\bitrate-tests'
New-Item -ItemType Directory -Path $build -Force | Out-Null
Push-Location $build
try {
    $sources = '"' + (Join-Path $projectRoot 'tests\bitrate_tests.cpp') + '" "' + (Join-Path $projectRoot 'src\Process.cpp') + '"'
    $command = '"' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++17 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN ' + $sources + ' /Fe:bitrate_tests.exe'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Bitrate test compilation failed.' }
    $arguments = @($Ffmpeg, (Join-Path $build ('run-' + [guid]::NewGuid().ToString('N'))))
    if ($HardwareEncoder) { $arguments += $HardwareEncoder }
    & (Join-Path $build 'bitrate_tests.exe') @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Bitrate encoding tests failed.' }
} finally { Pop-Location }
