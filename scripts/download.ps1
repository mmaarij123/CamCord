function Get-VerifiedDownload([string]$Uri, [string]$Path, [string]$Sha256) {
    if ((Test-Path -LiteralPath $Path) -and ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Sha256)) { return }
    New-Item -ItemType Directory -Path (Split-Path $Path -Parent) -Force | Out-Null
    $temporaryFile = "$Path.partial"
    Invoke-WebRequest -Uri $Uri -OutFile $temporaryFile -UseBasicParsing
    if ((Get-FileHash -LiteralPath $temporaryFile -Algorithm SHA256).Hash -ne $Sha256) {
        throw "Checksum mismatch for $Uri. The untrusted file is retained at $temporaryFile and will not be used."
    }
    Move-Item -LiteralPath $temporaryFile -Destination $Path -Force
}
