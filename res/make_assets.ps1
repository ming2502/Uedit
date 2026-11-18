Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent
$resDir = Join-Path $root "res"
$assetsDir = Join-Path $root "packaging\assets"
New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null

function New-IconBitmap([int]$size) {
  $bitmap = New-Object System.Drawing.Bitmap $size, $size
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $graphics.Clear([System.Drawing.Color]::Transparent)

  $radius = [int]([Math]::Max(2, $size * 0.22))
  $diameter = $radius * 2
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $path.AddArc(0, 0, $diameter, $diameter, 180, 90)
  $path.AddArc($size - $diameter, 0, $diameter, $diameter, 270, 90)
  $path.AddArc($size - $diameter, $size - $diameter, $diameter, $diameter, 0, 90)
  $path.AddArc(0, $size - $diameter, $diameter, $diameter, 90, 90)
  $path.CloseFigure()

  $rect = New-Object System.Drawing.Rectangle 0, 0, $size, $size
  $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    $rect,
    [System.Drawing.Color]::FromArgb(255, 76, 141, 255),
    [System.Drawing.Color]::FromArgb(255, 43, 96, 220),
    45)
  $graphics.FillPath($brush, $path)

  $fontSize = [float]($size * 0.58)
  $font = New-Object System.Drawing.Font "Segoe UI", $fontSize,
    ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
  $format = New-Object System.Drawing.StringFormat
  $format.Alignment = [System.Drawing.StringAlignment]::Center
  $format.LineAlignment = [System.Drawing.StringAlignment]::Center
  $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
  $layout = New-Object System.Drawing.RectangleF 0, 0, $size, $size
  $graphics.DrawString("U", $font, $white, $layout, $format)

  $graphics.Dispose()
  $font.Dispose()
  $white.Dispose()
  $brush.Dispose()
  $path.Dispose()
  return $bitmap
}

function Get-PngBytes($bitmap) {
  $stream = New-Object System.IO.MemoryStream
  $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
  $bytes = $stream.ToArray()
  $stream.Dispose()
  return , $bytes
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = @()
foreach ($size in $sizes) {
  $bitmap = New-IconBitmap $size
  $images += , (Get-PngBytes $bitmap)
  $bitmap.Dispose()
}

$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $stream
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $size = $sizes[$i]
  $dimension = if ($size -ge 256) { 0 } else { $size }
  $writer.Write([byte]$dimension)
  $writer.Write([byte]$dimension)
  $writer.Write([byte]0)
  $writer.Write([byte]0)
  $writer.Write([UInt16]1)
  $writer.Write([UInt16]32)
  $writer.Write([UInt32]$images[$i].Length)
  $writer.Write([UInt32]$offset)
  $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write($image) }
$writer.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $resDir "uedit.ico"), $stream.ToArray())
$writer.Dispose()
$stream.Dispose()

function Save-Square([int]$size, [string]$name) {
  $bitmap = New-IconBitmap $size
  $bitmap.Save((Join-Path $assetsDir $name),
    [System.Drawing.Imaging.ImageFormat]::Png)
  $bitmap.Dispose()
}

Save-Square 44 "Square44x44Logo.png"
Save-Square 150 "Square150x150Logo.png"
Save-Square 310 "Square310x310Logo.png"
Save-Square 50 "StoreLogo.png"

$wide = New-Object System.Drawing.Bitmap 310, 150
$wideGraphics = [System.Drawing.Graphics]::FromImage($wide)
$wideGraphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$wideGraphics.Clear([System.Drawing.Color]::Transparent)
$icon = New-IconBitmap 100
$wideGraphics.DrawImage($icon, 105, 25, 100, 100)
$icon.Dispose()
$wideGraphics.Dispose()
$wide.Save((Join-Path $assetsDir "Wide310x150Logo.png"),
  [System.Drawing.Imaging.ImageFormat]::Png)
$wide.Dispose()

Write-Output "generated icon and packaging assets"
