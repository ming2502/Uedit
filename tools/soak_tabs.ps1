param(
  [int]$Minutes = 3
)

$ErrorActionPreference = "Stop"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class TabSoak {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr p, uint f);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int a, bool i, int pid);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\Release\uedit.exe"
$first = Join-Path $root "fixtures\sample_256mb.log"
$second = Join-Path $root "fixtures\extract_demo.txt"

$proc = Start-Process -FilePath $exe -ArgumentList @($first, "--search=ERROR") -PassThru
Start-Sleep -Seconds 3
Start-Process -FilePath $exe -ArgumentList @($second) | Out-Null
Start-Sleep -Seconds 2

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
  $tabX = if (($iteration % 2) -eq 0) { 110 } else { 330 }
  $tabY = 57
  $tabLParam = [IntPtr](($tabY -shl 16) -bor ($tabX -band 0xFFFF))
  [void][TabSoak]::PostMessage($hwnd, 0x0201, [IntPtr]1, $tabLParam)
  [void][TabSoak]::PostMessage($hwnd, 0x0202, [IntPtr]0, $tabLParam)

  $rect = New-Object TabSoak+RECT
  [void][TabSoak]::GetWindowRect($hwnd, [ref]$rect)
  $cx = [int](($rect.L + $rect.R) / 2)
  $cy = [int](($rect.T + $rect.B) / 2)
  $delta = if (($iteration % 2) -eq 0) { 0xFF88 } else { 120 }
  $wparam = [IntPtr]($delta -shl 16)
  $lparam = [IntPtr](($cy -shl 16) -bor ($cx -band 0xFFFF))
  [void][TabSoak]::PostMessage($hwnd, 0x020A, $wparam, $lparam)

  Start-Sleep -Milliseconds 120
  ++$iteration

  if ((Get-Date) -ge $nextSample) {
    $process = Get-Process -Id $proc.Id
    $processHandle = [TabSoak]::OpenProcess(0x1000, $false, $proc.Id)
    $gdi = 0
    $user = 0
    if ($processHandle -ne [IntPtr]::Zero) {
      $gdi = [TabSoak]::GetGuiResources($processHandle, 0)
      $user = [TabSoak]::GetGuiResources($processHandle, 1)
      [void][TabSoak]::CloseHandle($processHandle)
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
  [void][TabSoak]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
  Start-Sleep -Seconds 2
  if (-not $proc.HasExited) { $proc.Kill() }
}

$samples | Format-Table -AutoSize
$private = $samples | ForEach-Object { $_.PrivateMB }
$handles = $samples | ForEach-Object { $_.Handles }
Write-Output ("iterations=" + $iteration)
Write-Output ("private min/max: {0}/{1} MB" -f ($private | Measure-Object -Minimum).Minimum, ($private | Measure-Object -Maximum).Maximum)
Write-Output ("handles min/max: {0}/{1}" -f ($handles | Measure-Object -Minimum).Minimum, ($handles | Measure-Object -Maximum).Maximum)
