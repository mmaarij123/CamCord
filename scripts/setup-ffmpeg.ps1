param([string]$Destination = (Join-Path $PSScriptRoot '..\third_party\ffmpeg'))
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'download.ps1')
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$archive = Join-Path $projectRoot 'third_party\downloads\ffmpeg-9.0.2-essentials_build.zip'
Get-VerifiedDownload 'https://github.com/GyanD/codexffmpeg/releases/download/9.0.2/ffmpeg-9.0.2-essentials_build.zip' $archive '60F467265B1E312373DBCD92200C2618A74850F98D3D078E94296BB3FA2047BA'
$extract = Join-Path $projectRoot 'third_party\downloads\ffmpeg-9.0.2'
if (-not (Test-Path (Join-Path $extract 'ffmpeg-9.0.2-essentials_build\bin\ffmpeg.exe'))) {
    Expand-Archive -LiteralPath $archive -DestinationPath $extract -Force
}
$package = Join-Path $extract 'ffmpeg-9.0.2-essentials_build'
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Copy-Item -LiteralPath (Join-Path $package 'bin\ffmpeg.exe') -Destination (Join-Path $Destination 'ffmpeg.exe') -Force
Copy-Item -LiteralPath (Join-Path $package 'bin\ffprobe.exe') -Destination (Join-Path $Destination 'ffprobe.exe') -Force
Copy-Item -LiteralPath (Join-Path $package 'LICENSE'),(Join-Path $package 'README.txt') -Destination $Destination -Force
Write-Host 'Verified FFmpeg 9.0.2 essentials is ready.'
