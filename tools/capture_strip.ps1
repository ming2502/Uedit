param(
  [string]$OutFile = (Join-Path $PSScriptRoot '..\fixtures\strip.png'),
  [int]$Width = 700,
  [int]$Height = 34,
  [int]$OffsetX = 0,
  [int]$OffsetY = 0,
  [int]$Scale = 3
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class StripNative {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$proc = Get-Process uedit -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowHandle -ne 0 } |
  Select-Object -First 1
if (-not $proc) { Write-Output "no uedit window"; exit 1 }
$handle = $proc.MainWindowHandle
[void][StripNative]::SetForegroundWindow($handle)
$rect = New-Object StripNative+RECT
[void][StripNative]::GetWindowRect($handle, [ref]$rect)
$cx = [int](($rect.Left + $rect.Right) / 2)
$cy = [int](($rect.Top + $rect.Bottom) / 2)
[void][StripNative]::SetCursorPos($cx, $cy)
[StripNative]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
[StripNative]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 600

$sourceX = $rect.Left + $OffsetX
$sourceY = $rect.Top + $OffsetY
$bitmap = New-Object System.Drawing.Bitmap $Width, $Height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($sourceX, $sourceY, 0, 0,
  (New-Object System.Drawing.Size $Width, $Height))
$graphics.Dispose()

$scaled = New-Object System.Drawing.Bitmap ($Width * $Scale), ($Height * $Scale)
$scaledGraphics = [System.Drawing.Graphics]::FromImage($scaled)
$scaledGraphics.InterpolationMode =
  [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$scaledGraphics.DrawImage($bitmap, 0, 0, $scaled.Width, $scaled.Height)
$scaledGraphics.Dispose()
$bitmap.Dispose()
$scaled.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$scaled.Dispose()
Write-Output "saved $OutFile"
