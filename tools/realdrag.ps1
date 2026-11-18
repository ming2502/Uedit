param(
  [int]$X1 = 0,
  [int]$Y1 = 0,
  [int]$X2 = 0,
  [int]$Y2 = 0
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class UeDrag {
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

$proc = Get-Process uedit -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowHandle -ne 0 } |
  Select-Object -First 1
if (-not $proc) { Write-Output "no uedit window"; exit 1 }
$h = $proc.MainWindowHandle
$p = New-Object UeDrag+POINT
$p.X = $X1; $p.Y = $Y1
[void][UeDrag]::ClientToScreen($h, [ref]$p)
[void][UeDrag]::SetCursorPos($p.X, $p.Y)
[UeDrag]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 120
$steps = 16
for ($i = 1; $i -le $steps; ++$i) {
  $sx = [int]($X1 + ($X2 - $X1) * $i / $steps)
  $sy = [int]($Y1 + ($Y2 - $Y1) * $i / $steps)
  $q = New-Object UeDrag+POINT
  $q.X = $sx; $q.Y = $sy
  [void][UeDrag]::ClientToScreen($h, [ref]$q)
  [void][UeDrag]::SetCursorPos($q.X, $q.Y)
  Start-Sleep -Milliseconds 30
}
[UeDrag]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Write-Output "dragged $X1,$Y1 -> $X2,$Y2"
