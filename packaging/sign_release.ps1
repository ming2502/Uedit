param(
  [Parameter(Mandatory = $true)][string]$PfxPath,
  [Parameter(Mandatory = $true)][string]$PfxPassword,
  [string]$TimestampUrl = "http://timestamp.digicert.com",
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Configuration\uedit.exe"
if (-not (Test-Path $exe)) {
  throw "Build output not found: $exe"
}

$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" `
  -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending |
  Select-Object -First 1
if (-not $signtool) {
  throw "signtool.exe not found. Install the Windows SDK signing tools."
}

Write-Output "Signing executable..."
& $signtool.FullName sign /fd SHA256 /f $PfxPath /p $PfxPassword `
  /tr $TimestampUrl /td SHA256 $exe
if ($LASTEXITCODE -ne 0) { throw "Signing uedit.exe failed" }

Write-Output "Rebuilding packages from the signed executable..."
& powershell -NoProfile -ExecutionPolicy Bypass `
  -File (Join-Path $PSScriptRoot "package_portable.ps1") `
  -Configuration $Configuration | Out-Host
& powershell -NoProfile -ExecutionPolicy Bypass `
  -File (Join-Path $PSScriptRoot "build_msix.ps1") `
  -Configuration $Configuration | Out-Host

$msix = Join-Path $root "dist\UEdit_1.1.1.0_x64.msix"
Write-Output "Signing MSIX..."
& $signtool.FullName sign /fd SHA256 /f $PfxPath /p $PfxPassword `
  /tr $TimestampUrl /td SHA256 $msix
if ($LASTEXITCODE -ne 0) { throw "Signing MSIX failed" }

Write-Output "Verifying signatures..."
& $signtool.FullName verify /pa /v $exe
if ($LASTEXITCODE -ne 0) { throw "Signature verification failed for uedit.exe" }
& $signtool.FullName verify /pa /v $msix
if ($LASTEXITCODE -ne 0) { throw "Signature verification failed for MSIX" }

Write-Output "Signed release artifacts:"
Write-Output "  $exe"
Write-Output "  $msix"
Write-Output "  $(Join-Path $root 'dist\UEdit-1.1.1-win64.zip')"
