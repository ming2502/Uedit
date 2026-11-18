Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class UeMotion {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$proc = Get-Process uedit -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowHandle -ne 0 } |
  Select-Object -First 1
if (-not $proc) { Write-Output "no uedit window"; exit 1 }
$main = $proc.MainWindowHandle
[void][UeMotion]::SetForegroundWindow($main)
Start-Sleep -Milliseconds 300

$target = [uint32]$proc.Id
[UeMotion]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 40
[UeMotion]::keybd_event(0x4F, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 40
[UeMotion]::keybd_event(0x4F, 0, 2, [UIntPtr]::Zero)
[UeMotion]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)

$start = Get-Date
$last = ""
$script:dialog = [IntPtr]::Zero
while (((Get-Date) - $start).TotalMilliseconds -lt 1500) {
  $script:dialog = [IntPtr]::Zero
  $callback = [UeMotion+EnumProc]{
    param($h, $l)
    $pid2 = [uint32]0
    [void][UeMotion]::GetWindowThreadProcessId($h, [ref]$pid2)
    if ($pid2 -eq $target) {
      $class = New-Object System.Text.StringBuilder 32
      [void][UeMotion]::GetClassName($h, $class, 32)
      if ($class.ToString() -eq "#32770") {
        $script:dialog = $h
        return $false
      }
    }
    return $true
  }
  [void][UeMotion]::EnumWindows($callback, [IntPtr]::Zero)

  if ($script:dialog -ne [IntPtr]::Zero) {
    $rect = New-Object UeMotion+RECT
    [void][UeMotion]::GetWindowRect($script:dialog, [ref]$rect)
    $visible = [UeMotion]::IsWindowVisible($script:dialog)
    $elapsed = [int](((Get-Date) - $start).TotalMilliseconds)
    $line = ("t={0,4}ms rect={1},{2} {3}x{4} visible={5}" -f
      $elapsed,
      $rect.Left, $rect.Top, ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top),
      $visible)
    if ($line -ne $last) {
      Write-Output $line
      $last = $line
    }
  }
  Start-Sleep -Milliseconds 15
}

if ($script:dialog -ne [IntPtr]::Zero) {
  [void][UeMotion]::PostMessage($script:dialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
}
