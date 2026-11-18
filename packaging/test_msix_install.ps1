$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$cer = Join-Path $root "dist\test-signing\UEdit-Test.cer"
$msix = Join-Path $root "dist\test-signing\UEdit_test_signed.msix"
if (-not (Test-Path $cer) -or -not (Test-Path $msix)) {
  throw "Test-signed artifacts are missing"
}

$installed = Get-AppxPackage UEdit.Desktop -ErrorAction SilentlyContinue
if ($installed) { Remove-AppxPackage $installed.PackageFullName }

Import-Certificate -FilePath $cer `
  -CertStoreLocation Cert:\CurrentUser\Root | Out-Null

try {
  Add-AppxPackage -Path $msix
  $package = Get-AppxPackage UEdit.Desktop
  if (-not $package) { throw "Package was not registered" }
  Write-Output "installed: $($package.PackageFullName)"
  Write-Output "location:  $($package.InstallLocation)"

  $family = $package.PackageFamilyName
  Start-Process "shell:AppsFolder\$family!UEdit"
  Start-Sleep -Seconds 5
  $process = Get-Process uedit -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($process) {
    Write-Output "launched:  $($process.MainWindowTitle)"
    [void]$process.CloseMainWindow()
    Start-Sleep -Seconds 3
    if (-not $process.HasExited) { $process.Kill() }
  } else {
    Write-Output "launch failed"
  }
} finally {
  $package = Get-AppxPackage UEdit.Desktop -ErrorAction SilentlyContinue
  if ($package) {
    Remove-AppxPackage $package.PackageFullName
    Write-Output "uninstalled: $($package.PackageFullName)"
  }
  $cert = Get-ChildItem Cert:\CurrentUser\Root |
    Where-Object { $_.Subject -eq "CN=UEdit" } |
    Select-Object -First 1
  if ($cert) {
    Remove-Item "Cert:\CurrentUser\Root\$($cert.Thumbprint)" -Force
    Write-Output "test certificate removed from Root"
  }
}
