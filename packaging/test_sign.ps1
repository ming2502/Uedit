$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$testDir = Join-Path $root "dist\test-signing"
New-Item -ItemType Directory -Force -Path $testDir | Out-Null

$sourceExe = Join-Path $root "build\Release\uedit.exe"
$sourceMsix = Join-Path $root "dist\UEdit_1.1.1.0_x64.msix"
if (-not (Test-Path $sourceExe) -or -not (Test-Path $sourceMsix)) {
  throw "Release artifacts are missing"
}

$exe = Join-Path $testDir "uedit.exe"
$msix = Join-Path $testDir "UEdit_test_signed.msix"
Copy-Item $sourceExe $exe -Force
Copy-Item $sourceMsix $msix -Force

$cert = New-SelfSignedCertificate `
  -Type CodeSigningCert `
  -Subject "CN=UEdit" `
  -FriendlyName "UEdit Test Signing" `
  -CertStoreLocation "Cert:\CurrentUser\My" `
  -KeyExportPolicy Exportable `
  -NotAfter (Get-Date).AddYears(1)
if (-not $cert) { throw "Failed to create test certificate" }

$cerPath = Join-Path $testDir "UEdit-Test.cer"
$pfxPath = Join-Path $testDir "UEdit-Test.pfx"
$passwordText = "UEditTest!"
$password = ConvertTo-SecureString -String $passwordText -Force -AsPlainText
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $password | Out-Null

$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" `
  -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending |
  Select-Object -First 1
if (-not $signtool) { throw "signtool.exe not found" }

function Sign-File([string]$path) {
  & $signtool.FullName sign /fd SHA256 /f $pfxPath /p $passwordText `
    /tr "http://timestamp.digicert.com" /td SHA256 $path
  if ($LASTEXITCODE -ne 0) {
    Write-Output "Timestamped signing failed; retrying without timestamp"
    & $signtool.FullName sign /fd SHA256 /f $pfxPath /p $passwordText $path
    if ($LASTEXITCODE -ne 0) { throw "Signing failed: $path" }
  }
}

Sign-File $exe
Sign-File $msix

$exeSignature = Get-AuthenticodeSignature $exe
$msixSignature = Get-AuthenticodeSignature $msix
Write-Output "exe status: $($exeSignature.Status) — $($exeSignature.StatusMessage)"
Write-Output "msix status: $($msixSignature.Status) — $($msixSignature.StatusMessage)"

Write-Output "Certificate: $cerPath"
Write-Output "Signed executable: $exe"
Write-Output "Signed MSIX: $msix"

Remove-Item -Path "Cert:\CurrentUser\My\$($cert.Thumbprint)" -Force
Remove-Item $pfxPath -Force
Write-Output "Temporary certificate and PFX removed; public .cer kept for test trust."
