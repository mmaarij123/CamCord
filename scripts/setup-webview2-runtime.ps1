$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$destination = Join-Path $projectRoot 'third_party\webview2-runtime'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$bootstrapper = Join-Path $destination 'MicrosoftEdgeWebview2Setup.exe'
if (-not (Test-Path -LiteralPath $bootstrapper)) {
    Invoke-WebRequest -Uri 'https://go.microsoft.com/fwlink/p/?LinkId=2124703' -OutFile "$bootstrapper.partial" -UseBasicParsing
    $signature = Get-AuthenticodeSignature -LiteralPath "$bootstrapper.partial"
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') { throw 'WebView2 bootstrapper does not have a valid Microsoft signature.' }
    Move-Item -LiteralPath "$bootstrapper.partial" -Destination $bootstrapper -Force
}
$signature = Get-AuthenticodeSignature -LiteralPath $bootstrapper
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') { throw 'WebView2 bootstrapper signature verification failed.' }
Write-Host 'Verified Microsoft WebView2 bootstrapper (internet required when runtime is missing).'
