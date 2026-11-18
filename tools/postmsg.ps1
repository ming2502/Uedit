param(
  [int]$Msg = 0x0102,
  [int]$WParam = 0x58,
  [int]$LParam = 1
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class UePost {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
"@

$proc = Get-Process uedit -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowHandle -ne 0 } |
  Select-Object -First 1
if (-not $proc) {
  Write-Output "no uedit window"
  exit 1
}
[void][UePost]::PostMessage($proc.MainWindowHandle, [uint32]$Msg, [IntPtr]$WParam, [IntPtr]$LParam)
Write-Output ("posted msg=0x{0:X} wparam={1} lparam={2}" -f $Msg, $WParam, $LParam)
