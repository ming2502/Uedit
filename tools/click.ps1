param(
  [int]$X = 0,
  [int]$Y = 0
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class UeClick {
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
$value = (($Y -band 0xFFFF) -shl 16) -bor ($X -band 0xFFFF)
$lparam = [IntPtr]$value
[void][UeClick]::PostMessage($proc.MainWindowHandle, 0x0201, [IntPtr]1, $lparam)
Start-Sleep -Milliseconds 80
[void][UeClick]::PostMessage($proc.MainWindowHandle, 0x0202, [IntPtr]0, $lparam)
Write-Output "clicked $X,$Y"
