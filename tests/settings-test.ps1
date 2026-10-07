$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ build tools are required.' }
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ build tools are required.' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $projectRoot 'obj\settings-tests'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$sources = @('tests\settings_tests.cpp', 'src\SettingsManager.cpp', 'src\OutputManager.cpp')
$quotedSources = ($sources | ForEach-Object { '"' + (Join-Path $projectRoot $_) + '"' }) -join ' '
Push-Location $build
try {
    $command = '"' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++17 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN ' + $quotedSources + ' /Fe:settings_tests.exe /link ole32.lib shell32.lib uuid.lib advapi32.lib'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Settings/output integration test compile failed.' }
    $artifacts = Join-Path $build ('run-' + [guid]::NewGuid().ToString('N'))
    & (Join-Path $build 'settings_tests.exe') $artifacts
    if ($LASTEXITCODE -ne 0) { throw "Settings/output integration tests failed. Artifacts: $artifacts" }
    Write-Host "Settings/output integration test artifacts: $artifacts"
} finally { Pop-Location }
