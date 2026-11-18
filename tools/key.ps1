param(
  [int]$Key = 0x73
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class UeKey {
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
[void][UeKey]::PostMessage($proc.MainWindowHandle, 0x0100, [IntPtr]$Key, [IntPtr]0)
Start-Sleep -Milliseconds 60
[void][UeKey]::PostMessage($proc.MainWindowHandle, 0x0101, [IntPtr]$Key, [IntPtr]0)
Write-Output "key 0x$('{0:X}' -f $Key)"
