# Package the approved PNG into Windows and web icon sizes; no runtime dependency.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourcePath = Join-Path $projectRoot 'assets\branding\camcord-logo-taskbar.png'
$publicDir = Join-Path $projectRoot 'ui\public'
New-Item -ItemType Directory -Force -Path $publicDir | Out-Null
$sizes = @(16, 20, 24, 32, 48, 64, 128, 256)
$images = @()
$source = [System.Drawing.Image]::FromFile($sourcePath)
try {
    foreach ($size in $sizes) {
        $bitmap = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $attributes = New-Object System.Drawing.Imaging.ImageAttributes
        $stream = New-Object System.IO.MemoryStream
        try {
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
            $rectangle = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
            $graphics.DrawImage($source, $rectangle, 0, 0, $source.Width, $source.Height,
                [System.Drawing.GraphicsUnit]::Pixel, $attributes)
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $bytes = $stream.ToArray()
            $images += ,$bytes
            if ($size -eq 32) { [System.IO.File]::WriteAllBytes((Join-Path $publicDir 'favicon.png'), $bytes) }
            if ($size -eq 128) { [System.IO.File]::WriteAllBytes((Join-Path $publicDir 'camcord-logo.png'), $bytes) }
        } finally {
            $stream.Dispose()
            $attributes.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally { $source.Dispose() }

$iconPath = Join-Path $projectRoot 'assets\branding\camcord.ico'
$output = [System.IO.File]::Create($iconPath)
$writer = New-Object System.IO.BinaryWriter($output)
try {
    $writer.Write([UInt16]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index = 0; $index -lt $sizes.Count; $index++) {
        $dimension = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$images[$index].Length)
        $writer.Write([UInt32]$offset)
        $offset += $images[$index].Length
    }
    foreach ($bytes in $images) { $writer.Write([byte[]]$bytes) }
} finally { $writer.Dispose() }
Write-Host 'CamCord icons generated with transparent edges at 16-256 px.'
