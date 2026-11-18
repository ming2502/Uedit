param(
  [switch]$Save
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class UeProbe {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$proc = Get-Process uedit -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowHandle -ne 0 } |
  Select-Object -First 1
if (-not $proc) {
  Write-Output "no uedit window"
  exit 1
}
$main = $proc.MainWindowHandle
$mainTid = [UeProbe]::GetWindowThreadProcessId($main, [ref]([uint32]0))
[void][UeProbe]::SetForegroundWindow($main)
Start-Sleep -Milliseconds 400

if ($Save) {
  [UeProbe]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
  [UeProbe]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 40
  [UeProbe]::keybd_event(0x46, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 40
  [UeProbe]::keybd_event(0x46, 0, 2, [UIntPtr]::Zero)
  [UeProbe]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero)
  [UeProbe]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 500
  [UeProbe]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [UeProbe]::keybd_event(0x0D, 0, 2, [UIntPtr]::Zero)
} else {
  [UeProbe]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [UeProbe]::keybd_event(0x4F, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [UeProbe]::keybd_event(0x4F, 0, 2, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [UeProbe]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
}

$target = [uint32]$proc.Id
$script:dialog = [IntPtr]::Zero
for ($i = 0; $i -lt 40 -and $script:dialog -eq [IntPtr]::Zero; $i++) {
  Start-Sleep -Milliseconds 100
  $callback = [UeProbe+EnumProc]{
    param($h, $l)
    $pid2 = [uint32]0
    [void][UeProbe]::GetWindowThreadProcessId($h, [ref]$pid2)
    if ($pid2 -eq $target -and [UeProbe]::IsWindowVisible($h)) {
      $class = New-Object System.Text.StringBuilder 64
      [void][UeProbe]::GetClassName($h, $class, 64)
      if ($class.ToString() -eq "#32770") {
        $script:dialog = $h
        return $false
      }
    }
    return $true
  }
  [void][UeProbe]::EnumWindows($callback, [IntPtr]::Zero)
}

if ($script:dialog -eq [IntPtr]::Zero) {
  Write-Output "dialog=not-found"
  exit 1
}

Start-Sleep -Milliseconds 800
$dialogTid = [UeProbe]::GetWindowThreadProcessId($script:dialog, [ref]([uint32]0))
$owner = [UeProbe]::GetWindow($script:dialog, 4)
$title = New-Object System.Text.StringBuilder 256
[void][UeProbe]::GetWindowText($script:dialog, $title, 256)
$dr = New-Object UeProbe+RECT
$or = New-Object UeProbe+RECT
[void][UeProbe]::GetWindowRect($script:dialog, [ref]$dr)
[void][UeProbe]::GetWindowRect($main, [ref]$or)
Write-Output ("dialog-tid={0} main-tid={1} owner={2} main={3}" -f $dialogTid, $mainTid, $owner, $main)
Write-Output ("title={0}" -f $title.ToString())
Write-Output ("dialog-rect={0},{1} {2}x{3}" -f $dr.Left, $dr.Top, ($dr.Right - $dr.Left), ($dr.Bottom - $dr.Top))
Write-Output ("owner-rect={0},{1} {2}x{3}" -f $or.Left, $or.Top, ($or.Right - $or.Left), ($or.Bottom - $or.Top))

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
$bmp.Save((Join-Path $PSScriptRoot '..\fixtures\dlg_open.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()
Write-Output "saved=fixtures\dlg_open.png"

# Leave dialog open for inspection.
