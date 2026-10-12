param([string]$Ffmpeg)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not $Ffmpeg){$Ffmpeg=Join-Path $projectRoot 'third_party\ffmpeg\ffmpeg.exe'}
$Ffmpeg=(Resolve-Path -LiteralPath $Ffmpeg).Path
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$build=Join-Path $projectRoot 'obj\replay-tests'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$sources=@('tests\replay_tests.cpp','src\ReplayManager.cpp','src\SettingsManager.cpp','src\CaptureEngine.cpp','src\CaptureSources.cpp','src\HardwareEncoderDetector.cpp','src\OutputManager.cpp','src\Process.cpp')
$quotedSources=($sources|ForEach-Object{'"'+(Join-Path $projectRoot $_)+'"'})-join ' '
Push-Location $build
try {
  $command='"'+$vcvars+'" >nul && cl.exe /nologo /EHsc /std:c++17 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I"'+(Join-Path $projectRoot 'third_party\json')+'" '+$quotedSources+' /Fe:replay_tests.exe /link ole32.lib shell32.lib uuid.lib avrt.lib dwmapi.lib user32.lib'
  & $env:ComSpec /d /s /c $command
  if($LASTEXITCODE -ne 0){throw 'Replay tests did not compile.'}
  & (Join-Path $build 'replay_tests.exe') $Ffmpeg (Join-Path $build ('run-'+[guid]::NewGuid().ToString('N')))
  if($LASTEXITCODE -ne 0){throw 'Replay tests failed.'}
}finally{Pop-Location}
