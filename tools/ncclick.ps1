param(
  [int]$Hit = 9  # HTMAXBUTTON
)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class NC {
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
$WM_NCLBUTTONDOWN = 0x00A1
[void][NC]::PostMessage($proc.MainWindowHandle, $WM_NCLBUTTONDOWN, [IntPtr]$Hit, [IntPtr]::Zero)
Write-Output "posted hit=$Hit"
