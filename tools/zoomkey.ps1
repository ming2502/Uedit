param(
  [int]$Times = 1,
  [switch]$Out
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class UeZoom {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

$proc = Get-Process uedit -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowHandle -ne 0 } |
  Select-Object -First 1
if (-not $proc) { Write-Output "no uedit window"; exit 1 }
$h = $proc.MainWindowHandle
[void][UeZoom]::SetForegroundWindow($h)
$rect = New-Object UeZoom+RECT
[void][UeZoom]::GetWindowRect($h, [ref]$rect)
$cx = [int](($rect.L + $rect.R) / 2)
$cy = [int](($rect.T + $rect.B) / 2)
[void][UeZoom]::SetCursorPos($cx, $cy)
[UeZoom]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
[UeZoom]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 300

$key = if ($Out) { 0xBD } else { 0xBB }
for ($i = 0; $i -lt $Times; ++$i) {
  [UeZoom]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
  [UeZoom]::keybd_event([byte]$key, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 50
  [UeZoom]::keybd_event([byte]$key, 0, 2, [UIntPtr]::Zero)
  [UeZoom]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 250
}
Write-Output ("zoom key x{0} out={1}" -f $Times, $Out.IsPresent)
