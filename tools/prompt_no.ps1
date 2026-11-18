param(
  [int]$Command = 7
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class UePrompt {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@

$proc = Get-Process uedit -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) {
  Write-Output "no uedit process"
  exit 1
}
$target = [uint32]$proc.Id
$script:dialog = [IntPtr]::Zero
$callback = [UePrompt+EnumProc]{
  param($h, $l)
  $windowPid = [uint32]0
  [void][UePrompt]::GetWindowThreadProcessId($h, [ref]$windowPid)
  if ($windowPid -eq $target) {
    $class = New-Object System.Text.StringBuilder 64
    [void][UePrompt]::GetClassName($h, $class, 64)
    if ($class.ToString() -eq "#32770") { $script:dialog = $h }
  }
  return $true
}
[void][UePrompt]::EnumWindows($callback, [IntPtr]::Zero)
if ($script:dialog -eq [IntPtr]::Zero) {
  Write-Output "no prompt dialog"
  exit 1
}
[void][UePrompt]::PostMessage($script:dialog, 0x0111, [IntPtr]$Command, [IntPtr]0)
Write-Output ("sent command {0} to {1}" -f $Command, $script:dialog.ToInt64())
