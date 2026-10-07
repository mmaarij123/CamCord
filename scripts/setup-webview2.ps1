$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'download.ps1')
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$archive = Join-Path $projectRoot 'third_party\downloads\webview2-1.0.3485.44.zip'
Get-VerifiedDownload 'https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.3485.44/microsoft.web.webview2.1.0.3485.44.nupkg' $archive 'BC09150B179246AC90189649B13BE8E6B11B3AC200E817E18DF106E1F3CF489E'
$destination = Join-Path $projectRoot 'third_party\webview2'
Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
if (-not (Test-Path (Join-Path $destination 'build\native\x64\WebView2LoaderStatic.lib'))) { throw 'WebView2 SDK is incomplete.' }
