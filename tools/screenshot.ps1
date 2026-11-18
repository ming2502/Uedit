param(
  [string]$OutFile = (Join-Path $PSScriptRoot '..\fixtures\shot.png'),
  [string]$Keys = "",
  [int]$ClickX = -1,
  [int]$ClickY = -1
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$proc = Get-Process uedit -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowHandle -ne 0 } |
  Select-Object -First 1
if (-not $proc) {
  Write-Output "no uedit window"
  exit 1
}

$handle = $proc.MainWindowHandle
$rect = New-Object Native+RECT
[void][Native]::GetWindowRect($handle, [ref]$rect)
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top

if ($ClickX -ge 0 -and $ClickY -ge 0) {
  [void][Native]::SetForegroundWindow($handle)
  Start-Sleep -Milliseconds 300
  [void][Native]::SetCursorPos($rect.Left + $ClickX, $rect.Top + $ClickY)
  [Native]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
  [Native]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 500
}

if ($Keys -ne "") {
  [void][Native]::SetForegroundWindow($handle)
  Start-Sleep -Milliseconds 300
  [System.Windows.Forms.SendKeys]::SendWait($Keys)
  Start-Sleep -Milliseconds 1200
}

$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$hdc = $graphics.GetHdc()
[void][Native]::PrintWindow($handle, $hdc, 2)
$graphics.ReleaseHdc($hdc)
$graphics.Dispose()
$bitmap.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Output "saved $OutFile ($width x $height)"
