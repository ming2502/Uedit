Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class UeWindows {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
}
"@

$proc = Get-Process uedit -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) {
  Write-Output "no uedit process"
  exit 1
}
$target = [uint32]$proc.Id
$script:rows = @()
$callback = [UeWindows+EnumProc]{
  param($h, $l)
  $windowPid = [uint32]0
  [void][UeWindows]::GetWindowThreadProcessId($h, [ref]$windowPid)
  if ($windowPid -eq $target) {
    $title = New-Object System.Text.StringBuilder 256
    [void][UeWindows]::GetWindowText($h, $title, 256)
    $class = New-Object System.Text.StringBuilder 256
    [void][UeWindows]::GetClassName($h, $class, 256)
    if ($title.Length -gt 0) {
      $script:rows += [pscustomobject]@{
        Handle = $h.ToInt64()
        Class = $class.ToString()
        Title = $title.ToString()
        Visible = [UeWindows]::IsWindowVisible($h)
      }
    }
  }
  return $true
}
[void][UeWindows]::EnumWindows($callback, [IntPtr]::Zero)
$script:rows | Format-Table -AutoSize
