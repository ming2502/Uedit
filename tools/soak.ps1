param(
  [int]$Minutes = 5,
  [string]$File = (Join-Path $PSScriptRoot '..\fixtures\sample_1gb.log')
)

$ErrorActionPreference = "Stop"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class SoakNative {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr p, uint f);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int a, bool i, int pid);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

$exe = Join-Path $PSScriptRoot '..\build\Release\uedit.exe'
$proc = Start-Process -FilePath $exe `
  -ArgumentList $File, "--theme=dark", "--search=NEEDLE_2026" -PassThru
Start-Sleep -Seconds 5
$deadline = (Get-Date).AddMinutes($Minutes)
$nextSample = Get-Date
$samples = @()
$iteration = 0

while ((Get-Date) -lt $deadline) {
  $proc.Refresh()
  if ($proc.HasExited) { break }
  $hwnd = $proc.MainWindowHandle
  if ($hwnd -eq [IntPtr]::Zero) {
    Start-Sleep -Milliseconds 200
    continue
  }
  $rect = New-Object SoakNative+RECT
  [void][SoakNative]::GetWindowRect($hwnd, [ref]$rect)
  $cx = [int](($rect.L + $rect.R) / 2)
  $cy = [int](($rect.T + $rect.B) / 2)
  $delta = if (($iteration % 2) -eq 0) { 120 } else { 0xFF88 }
  $wparam = [IntPtr]($delta -shl 16)
  $lparam = [IntPtr](($cy -shl 16) -bor ($cx -band 0xFFFF))
  [void][SoakNative]::PostMessage($hwnd, 0x020A, $wparam, $lparam)

  if (($iteration % 75) -eq 0) {
    [void][SoakNative]::PostMessage($hwnd, 0x00A1, [IntPtr]9, [IntPtr]::Zero)
  }

  Start-Sleep -Milliseconds 200
  ++$iteration

  if ((Get-Date) -ge $nextSample) {
    $process = Get-Process -Id $proc.Id
    $processHandle = [SoakNative]::OpenProcess(0x1000, $false, $proc.Id)
    $gdi = 0
    $user = 0
    if ($processHandle -ne [IntPtr]::Zero) {
      $gdi = [SoakNative]::GetGuiResources($processHandle, 0)
      $user = [SoakNative]::GetGuiResources($processHandle, 1)
      [void][SoakNative]::CloseHandle($processHandle)
    }
    $samples += [pscustomobject]@{
      Time      = (Get-Date).ToString("HH:mm:ss")
      PrivateMB = [math]::Round($process.PrivateMemorySize64 / 1MB, 1)
      WorkingMB = [math]::Round($process.WorkingSet64 / 1MB, 1)
      Handles   = $process.HandleCount
      GDI       = $gdi
      USER      = $user
    }
    $nextSample = (Get-Date).AddSeconds(15)
  }
}

if (-not $proc.HasExited) {
  $hwnd = $proc.MainWindowHandle
  [void][SoakNative]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
  Start-Sleep -Seconds 2
  if (-not $proc.HasExited) { $proc.Kill() }
}

$samples | Format-Table -AutoSize
if ($samples.Count -gt 0) {
  $private = $samples | Measure-Object PrivateMB -Minimum -Maximum -Average
  $handles = $samples | Measure-Object Handles -Minimum -Maximum
  "iterations=$iteration"
  "private min/max/avg MB: $($private.Minimum)/$($private.Maximum)/$([math]::Round($private.Average,1))"
  "handles min/max: $($handles.Minimum)/$($handles.Maximum)"
}
