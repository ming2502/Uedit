param(
  [ValidateSet("f3", "f4", "settings", "esc")][string]$Action = "f3"
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class UeHotkey {
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
[void][UeHotkey]::SetForegroundWindow($h)
$rect = New-Object UeHotkey+RECT
[void][UeHotkey]::GetWindowRect($h, [ref]$rect)
$cx = [int](($rect.L + $rect.R) / 2)
$ty = $rect.T + 20
[void][UeHotkey]::SetCursorPos($cx, $ty)
[UeHotkey]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
[UeHotkey]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 300

function PressKey([byte]$vk) {
  [UeHotkey]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 40
  [UeHotkey]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
}

switch ($Action) {
  "f3" { PressKey 0x72 }
  "f4" { PressKey 0x73 }
  "esc" { PressKey 0x1B }
  "settings" {
    [UeHotkey]::keybd_event(0x11, 0x1D, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [UeHotkey]::keybd_event(0xBC, 0x33, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [UeHotkey]::keybd_event(0xBC, 0x33, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [UeHotkey]::keybd_event(0x11, 0x1D, 2, [UIntPtr]::Zero)
  }
}
Write-Output "sent $Action"
