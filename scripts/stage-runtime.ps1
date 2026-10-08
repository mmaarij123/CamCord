param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outputDirectory = Join-Path $projectRoot "bin\$Configuration"
$uiDestination = Join-Path $outputDirectory 'ui'
$uiSource = Join-Path $projectRoot 'ui\dist'
if (-not (Test-Path (Join-Path $uiSource 'index.html'))) { throw 'Build the React UI first with scripts\build-ui.ps1.' }
if (-not (Test-Path (Join-Path $projectRoot 'third_party\ffmpeg\ffmpeg.exe'))) { throw 'Run scripts\setup-ffmpeg.ps1 first.' }
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
if (Test-Path -LiteralPath $uiDestination) {
    $resolvedTarget = (Resolve-Path -LiteralPath $uiDestination).Path
    if ($resolvedTarget -ne [IO.Path]::GetFullPath((Join-Path $projectRoot "bin\$Configuration\ui"))) { throw 'Refusing to remove an unexpected UI staging path.' }
    Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}
Copy-Item -LiteralPath $uiSource -Destination $uiDestination -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\ffmpeg\ffmpeg.exe') -Destination $outputDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md'),(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination $outputDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $outputDirectory -Force
$licenseDirectory = Join-Path $outputDirectory 'licenses'
if (Test-Path -LiteralPath $licenseDirectory) {
    $expectedLicenseDirectory = [IO.Path]::GetFullPath((Join-Path $projectRoot "bin\$Configuration\licenses"))
    $resolvedLicenseDirectory = (Resolve-Path -LiteralPath $licenseDirectory).Path
    if ($resolvedLicenseDirectory -ne $expectedLicenseDirectory) { throw 'Refusing to remove an unexpected license staging path.' }
    Remove-Item -LiteralPath $resolvedLicenseDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $licenseDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\json\LICENSE.MIT') -Destination (Join-Path $licenseDirectory 'nlohmann-json-MIT.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\ffmpeg\LICENSE') -Destination (Join-Path $licenseDirectory 'FFmpeg-GPLv3.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\ffmpeg\README.txt') -Destination (Join-Path $licenseDirectory 'FFmpeg-build.txt') -Force
Get-ChildItem -LiteralPath (Join-Path $projectRoot 'third_party\webview2') -File -Filter '*LICENSE*' | Copy-Item -Destination $licenseDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\webview2\NOTICE.txt') -Destination (Join-Path $licenseDirectory 'WebView2-NOTICE.txt') -Force
& node (Join-Path $PSScriptRoot 'collect-licenses.mjs') $projectRoot $licenseDirectory
if ($LASTEXITCODE -ne 0) { throw 'Frontend license collection failed.' }
