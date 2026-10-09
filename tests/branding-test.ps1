param([switch]$CheckBuild)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Assert-VisibleMark([System.Drawing.Bitmap]$Bitmap, [string]$Label) {
    $left = $Bitmap.Width
    $top = $Bitmap.Height
    $right = -1
    $bottom = -1
    for ($y = 0; $y -lt $Bitmap.Height; $y++) {
        for ($x = 0; $x -lt $Bitmap.Width; $x++) {
            $pixel = $Bitmap.GetPixel($x, $y)
            # Coral frame or ivory dot, not the dark tile or transparent margin.
            if ($pixel.A -ge 200 -and $pixel.R -ge 170 -and
                (($pixel.R -gt ($pixel.G * 1.35)) -or ($pixel.G -ge 180 -and $pixel.B -ge 180))) {
                $left = [Math]::Min($left, $x)
                $top = [Math]::Min($top, $y)
                $right = [Math]::Max($right, $x)
                $bottom = [Math]::Max($bottom, $y)
            }
        }
    }
    $width = $right - $left + 1
    $height = $bottom - $top + 1
    if ($width -lt [Math]::Ceiling($Bitmap.Width * 0.75) -or
        $height -lt [Math]::Ceiling($Bitmap.Height * 0.75)) {
        throw "$Label has too much padding: visible mark $width x $height on $($Bitmap.Width) x $($Bitmap.Height)."
    }
    if ($Bitmap.GetPixel(0, 0).A -ne 0) { throw "$Label has lost transparent corners." }
    Write-Host "$Label passed: visible mark $width x $height on $($Bitmap.Width) x $($Bitmap.Height)."
}

$favicon = [System.Drawing.Bitmap]::FromFile((Join-Path $projectRoot 'ui\public\favicon.png'))
try {
    if ($favicon.Width -ne 32 -or $favicon.Height -ne 32) { throw 'Favicon must be 32 x 32.' }
    Assert-VisibleMark $favicon 'Taskbar-sized source'
} finally { $favicon.Dispose() }

if ($CheckBuild) {
    foreach ($relative in @('bin\Release\CamCord.exe','dist\CamCord-Setup.exe')) {
        $icon = [System.Drawing.Icon]::ExtractAssociatedIcon((Join-Path $projectRoot $relative))
        if (-not $icon) { throw "$relative has no Windows icon." }
        $bitmap = $icon.ToBitmap()
        try { Assert-VisibleMark $bitmap $relative }
        finally { $bitmap.Dispose(); $icon.Dispose() }
    }
}
