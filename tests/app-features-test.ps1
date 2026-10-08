param([switch]$Live)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $projectRoot 'scripts\setup-json.ps1')
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ tools are required.' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $projectRoot 'obj\app-feature-tests'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$sources = @('tests\app_features_tests.cpp', 'src\UpdateManager.cpp', 'src\StartupManager.cpp', 'src\SettingsManager.cpp')
$quotedSources = ($sources | ForEach-Object { '"' + (Join-Path $projectRoot $_) + '"' }) -join ' '
Push-Location $build
try {
    $command = '"' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++17 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I"' + (Join-Path $projectRoot 'third_party\json') + '" ' + $quotedSources + ' /Fe:app_features_tests.exe /link ole32.lib shell32.lib advapi32.lib uuid.lib winhttp.lib bcrypt.lib'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'App-feature test compilation failed.' }
    $artifacts = Join-Path $build ('run-' + [guid]::NewGuid().ToString('N'))
    $arguments = @($artifacts)
    if ($Live) { $arguments += '--live' }
    & (Join-Path $build 'app_features_tests.exe') @arguments
    if ($LASTEXITCODE -ne 0) { throw "App-feature tests failed. Artifacts: $artifacts" }
} finally { Pop-Location }
