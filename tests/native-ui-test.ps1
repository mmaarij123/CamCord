$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ tools are required.' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $projectRoot 'obj\native-ui-tests'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$sources = @('tests\native_ui_tests.cpp','src\MainWindow.cpp','src\RecordingManager.cpp','src\AudioCaptureEngine.cpp',
    'src\CaptureEngine.cpp','src\CaptureSources.cpp','src\RegionSelector.cpp','src\Process.cpp','src\OutputManager.cpp',
    'src\HardwareEncoderDetector.cpp','src\SettingsManager.cpp','src\StartupManager.cpp','src\UpdateManager.cpp')
$quotedSources = ($sources | ForEach-Object { '"' + (Join-Path $projectRoot $_) + '"' }) -join ' '
Push-Location $build
try {
    $command = '"' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++17 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /I"' +
        (Join-Path $projectRoot 'third_party\json') + '" /I"' + (Join-Path $projectRoot 'third_party\webview2\build\native\include') + '" ' + $quotedSources +
        ' /Fe:native_ui_tests.exe /link /LIBPATH:"' + (Join-Path $projectRoot 'third_party\webview2\build\native\x64') +
        '" WebView2LoaderStatic.lib user32.lib gdi32.lib dwmapi.lib ole32.lib oleaut32.lib shell32.lib shlwapi.lib uuid.lib advapi32.lib avrt.lib winhttp.lib bcrypt.lib version.lib'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Native UI lifecycle test did not compile.' }
    & (Join-Path $build 'native_ui_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Native UI lifecycle tests failed.' }
} finally { Pop-Location }
