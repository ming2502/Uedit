param(
  [int]$X1 = 200,
  [int]$Y1 = 120,
  [int]$X2 = 900,
  [int]$Y2 = 180,
  [switch]$Copy
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Ue {
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
$handle = $proc.MainWindowHandle

function MakeLParam([int]$x, [int]$y) {
  $value = (($y -band 0xFFFF) -shl 16) -bor ($x -band 0xFFFF)
  return [IntPtr]$value
}

[void][Ue]::PostMessage($handle, 0x0201, [IntPtr]1, (MakeLParam $X1 $Y1))
Start-Sleep -Milliseconds 120
for ($step = 1; $step -le 12; ++$step) {
  $x = [int]($X1 + ($X2 - $X1) * $step / 12)
  $y = [int]($Y1 + ($Y2 - $Y1) * $step / 12)
  [void][Ue]::PostMessage($handle, 0x0200, [IntPtr]1, (MakeLParam $x $y))
  Start-Sleep -Milliseconds 30
}
[void][Ue]::PostMessage($handle, 0x0202, [IntPtr]0, (MakeLParam $X2 $Y2))
Start-Sleep -Milliseconds 200

if ($Copy) {
  [void][Ue]::PostMessage($handle, 0x0301, [IntPtr]0, [IntPtr]0)
  Start-Sleep -Milliseconds 300
  $text = Get-Clipboard -Raw
  if ($null -eq $text) {
    Write-Output "clipboard: empty"
  } else {
    $preview = $text
    if ($preview.Length -gt 120) { $preview = $preview.Substring(0, 120) }
    Write-Output "clipboard chars=$($text.Length) preview=$preview"
  }
} else {
  Write-Output "selection posted"
}
