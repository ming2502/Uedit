param([Parameter(Mandatory = $true)][string]$Path)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class RmLock {
  [StructLayout(LayoutKind.Sequential)]
  public struct RM_UNIQUE_PROCESS { public int dwProcessId; public System.Runtime.InteropServices.ComTypes.FILETIME ProcessStartTime; }
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct RM_PROCESS_INFO {
    public RM_UNIQUE_PROCESS Process;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string strAppName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string strServiceShortName;
    public int ApplicationType;
    public uint AppStatus;
    public uint TSSessionId;
    [MarshalAs(UnmanagedType.Bool)] public bool bRestartable;
  }
  [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
  public static extern int RmStartSession(out uint handle, int flags, string key);
  [DllImport("rstrtmgr.dll")]
  public static extern int RmEndSession(uint handle);
  [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
  public static extern int RmRegisterResources(uint handle, uint nFiles, string[] files, uint nApps, RM_UNIQUE_PROCESS[] apps, uint nServices, string[] services);
  [DllImport("rstrtmgr.dll")]
  public static extern int RmGetList(uint handle, out uint needed, ref uint count, [In, Out] RM_PROCESS_INFO[] apps, out uint reasons);
}
"@

$key = [Guid]::NewGuid().ToString()
$session = [uint32]0
if ([RmLock]::RmStartSession([ref]$session, 0, $key) -ne 0) {
  Write-Output "RmStartSession failed"
  exit 1
}
[void][RmLock]::RmRegisterResources($session, 1, @($Path), 0, $null, 0, $null)
$needed = [uint32]0
$count = [uint32]0
$reasons = [uint32]0
[void][RmLock]::RmGetList($session, [ref]$needed, [ref]$count, $null, [ref]$reasons)
if ($needed -eq 0) {
  Write-Output "no locking process found"
  [void][RmLock]::RmEndSession($session)
  exit 0
}
$apps = New-Object RmLock+RM_PROCESS_INFO[] $needed
$count = $needed
[void][RmLock]::RmGetList($session, [ref]$needed, [ref]$count, $apps, [ref]$reasons)
foreach ($app in $apps[0..($count - 1)]) {
  if ($app.Process.dwProcessId -ne 0) {
    $name = (Get-Process -Id $app.Process.dwProcessId -ErrorAction SilentlyContinue).ProcessName
    Write-Output ("pid={0} name={1} app={2}" -f $app.Process.dwProcessId, $name, $app.strAppName)
  }
}
[void][RmLock]::RmEndSession($session)
