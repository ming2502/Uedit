param(
  [int]$X = 0,
  [int]$Y = 0
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class UeCursor {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool GetCursorInfo(ref CURSORINFO info);
  [DllImport("user32.dll")] public static extern IntPtr LoadCursor(IntPtr h, IntPtr id);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct CURSORINFO {
    public int cbSize;
    public int flags;
    public IntPtr hCursor;
    public POINT ptScreenPos;
  }
}
"@

$proc = Get-Process uedit -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowHandle -ne 0 } |
  Select-Object -First 1
$point = New-Object UeCursor+POINT
if ($proc) {
  [void][UeCursor]::SetForegroundWindow($proc.MainWindowHandle)
  Start-Sleep -Milliseconds 300
  $point.X = $X
  $point.Y = $Y
  [void][UeCursor]::ClientToScreen($proc.MainWindowHandle, [ref]$point)
} else {
  $point.X = $X
  $point.Y = $Y
}
[void][UeCursor]::SetCursorPos($point.X, $point.Y)
Start-Sleep -Milliseconds 500
$info = New-Object UeCursor+CURSORINFO
$info.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($info)
[void][UeCursor]::GetCursorInfo([ref]$info)
$map = @{
  ([UeCursor]::LoadCursor([IntPtr]::Zero, [IntPtr]32512).ToInt64()) = "ARROW"
  ([UeCursor]::LoadCursor([IntPtr]::Zero, [IntPtr]32513).ToInt64()) = "IBEAM"
  ([UeCursor]::LoadCursor([IntPtr]::Zero, [IntPtr]32644).ToInt64()) = "SIZEWE"
  ([UeCursor]::LoadCursor([IntPtr]::Zero, [IntPtr]32645).ToInt64()) = "SIZENS"
  ([UeCursor]::LoadCursor([IntPtr]::Zero, [IntPtr]32649).ToInt64()) = "HAND"
}
$name = $map[$info.hCursor.ToInt64()]
if (-not $name) { $name = "other:$($info.hCursor.ToInt64())" }
Write-Output "cursor=$name"
