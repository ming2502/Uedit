Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
$p = Get-Process uedit -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "no uedit process"; exit 1 }
$r = New-Object W+RECT
[void][W]::GetWindowRect($p.MainWindowHandle, [ref]$r)
"rect=$($r.L),$($r.T),$($r.R),$($r.B) size=$($r.R - $r.L)x$($r.B - $r.T) zoomed=$([W]::IsZoomed($p.MainWindowHandle)) iconic=$([W]::IsIconic($p.MainWindowHandle))"
if (Test-Path "$env:LOCALAPPDATA\UEdit\settings.json") {
  Get-Content "$env:LOCALAPPDATA\UEdit\settings.json"
} else {
  "no settings file"
}
