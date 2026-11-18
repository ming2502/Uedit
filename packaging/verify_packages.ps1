param(
  [string]$Version = "1.1.2",
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Configuration\uedit.exe"
$zip = Join-Path $root "dist\UEdit-$Version-win64.zip"
$msi = Join-Path $root "dist\UEdit_$Version.0_x64.msi"
if (-not (Test-Path $exe)) { throw "Missing $exe" }
if (-not (Test-Path $zip)) { throw "Missing $zip" }
if (-not (Test-Path $msi)) { throw "Missing $msi" }

$verify = Join-Path $root "build\verify"
if (Test-Path $verify) { Remove-Item $verify -Recurse -Force }
New-Item -ItemType Directory -Force -Path $verify | Out-Null
Expand-Archive -Path $zip -DestinationPath (Join-Path $verify "zip")

$msiTarget = Join-Path $verify "msi"
New-Item -ItemType Directory -Force -Path $msiTarget | Out-Null
$process = Start-Process msiexec.exe -Wait -PassThru -ArgumentList @(
  "/a", "`"$msi`"", "/qn", "TARGETDIR=`"$msiTarget`"")
if ($process.ExitCode -ne 0) {
  throw "msiexec administrative install failed: $($process.ExitCode)"
}
$msiExe = Get-ChildItem $msiTarget -Recurse -Filter uedit.exe |
  Select-Object -First 1
if (-not $msiExe) { throw "uedit.exe not found inside the MSI" }

$buildHash = (Get-FileHash $exe -Algorithm SHA256).Hash
$zipHash = (Get-FileHash (Join-Path $verify "zip\uedit.exe") -Algorithm SHA256).Hash
$msiHash = (Get-FileHash $msiExe.FullName -Algorithm SHA256).Hash
if ($buildHash -ne $zipHash) { throw "Portable exe hash differs from build output" }
if ($buildHash -ne $msiHash) { throw "MSI exe hash differs from build output" }

$msiVersion = ""
try {
  $installer = New-Object -ComObject WindowsInstaller.Installer
  $database = $installer.OpenDatabase($msi, 0)
  $view = $database.OpenView(
    "SELECT Value FROM Property WHERE Property='ProductVersion'")
  $view.Execute()
  $record = $view.Fetch()
  if ($record) { $msiVersion = $record.StringData(1) }
} catch {
  $msiVersion = "unknown"
}
if ($msiVersion -ne "unknown" -and $msiVersion -ne "$Version.0") {
  throw "MSI ProductVersion $msiVersion does not match $Version.0"
}

$versionText = (Get-Content (Join-Path $verify "zip\VERSION.txt") -Raw).Trim()
Write-Output "portable:      $zip"
Write-Output "msi:           $msi"
Write-Output "exe sha256:    $buildHash"
Write-Output "VERSION.txt:   $versionText"
Write-Output "msi version:   $msiVersion"
Write-Output "license file:  $(Test-Path (Join-Path $verify 'zip\PCRE2-LICENCE.txt'))"
Write-Output "hashes match build output: yes"
