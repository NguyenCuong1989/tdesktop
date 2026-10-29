# Build the Ω-Telegram Windows icon from the unchanged upstream artwork.
# The purple Omega badge clearly distinguishes the fork from official Telegram.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$source = Join-Path $repo 'Telegram\Resources\art\icon256.png'
$output = Join-Path $repo 'Telegram\Resources\art\omega_icon256.ico'
$preview = Join-Path $repo 'out\omega_brand_icon_preview.png'
if (!(Test-Path $source)) { throw 'Upstream source icon missing' }
$original = [Drawing.Image]::FromFile($source)
$images = New-Object System.Collections.Generic.List[object]
try {
    foreach ($n in @(16,24,32,48,64,128,256)) {
        $bitmap = New-Object Drawing.Bitmap($n, $n)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.DrawImage($original, 0, 0, $n, $n)
            $diameter = [int][Math]::Round($n * 0.48)
            $left = $n - $diameter - [int][Math]::Round($n * 0.025)
            $top = $n - $diameter - [int][Math]::Round($n * 0.025)
            $circle = New-Object Drawing.RectangleF([float]$left,[float]$top,[float]$diameter,[float]$diameter)
            $fill = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(255,90,42,170))
            $border = New-Object Drawing.Pen([Drawing.Color]::White,[float][Math]::Max(1,$n/64))
            try {
                $graphics.FillEllipse($fill, $circle)
                $graphics.DrawEllipse($border, $circle)
            } finally { $fill.Dispose(); $border.Dispose() }
            if ($n -ge 24) {
                $size = [float][Math]::Max(8, $diameter * 0.73)
                $font = New-Object Drawing.Font('Segoe UI Symbol', $size, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
                $white = New-Object Drawing.SolidBrush([Drawing.Color]::White)
                $format = New-Object Drawing.StringFormat
                $format.Alignment = [Drawing.StringAlignment]::Center
                $format.LineAlignment = [Drawing.StringAlignment]::Center
                try { $graphics.DrawString([char]0x03A9, $font, $white, $circle, $format) }
                finally { $font.Dispose(); $white.Dispose(); $format.Dispose() }
            }
            if ($n -eq 256) { $bitmap.Save($preview,[Drawing.Imaging.ImageFormat]::Png) }
            $buffer = New-Object IO.MemoryStream
            try {
                $bitmap.Save($buffer,[Drawing.Imaging.ImageFormat]::Png)
                $images.Add([pscustomobject]@{Size=$n;Bytes=$buffer.ToArray()})
            } finally { $buffer.Dispose() }
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
} finally { $original.Dispose() }
$stream = New-Object IO.MemoryStream
$writer = New-Object IO.BinaryWriter($stream)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($part in $images) {
        $writer.Write([byte]($part.Size % 256))
        $writer.Write([byte]($part.Size % 256))
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$part.Bytes.Length)
        $writer.Write([uint32]$offset)
        $offset += $part.Bytes.Length
    }
    foreach ($part in $images) { $writer.Write([byte[]]$part.Bytes) }
    $writer.Flush()
    [IO.File]::WriteAllBytes($output,$stream.ToArray())
} finally { $writer.Dispose(); $stream.Dispose() }
Write-Output ('BRAND_ICON_CREATED='+$output)
Write-Output ('IMAGE_SIZES='+ (($images|ForEach-Object {$_.Size}) -join ','))
Write-Output ('ICON_SHA256='+((Get-FileHash $output -Algorithm SHA256).Hash))
Write-Output ('UPSTREAM_ICON_PRESERVED='+((Get-FileHash $source -Algorithm SHA256).Hash))
