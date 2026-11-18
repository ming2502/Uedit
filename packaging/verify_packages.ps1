param(
  [string]$Version = "1.1.1",
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Configuration\uedit.exe"
$zip = Join-Path $root "dist\UEdit-$Version-win64.zip"
$msix = Join-Path $root "dist\UEdit_$Version.0_x64.msix"
if (-not (Test-Path $exe)) { throw "Missing $exe" }
if (-not (Test-Path $zip)) { throw "Missing $zip" }
if (-not (Test-Path $msix)) { throw "Missing $msix" }

$verify = Join-Path $root "build\verify"
if (Test-Path $verify) { Remove-Item $verify -Recurse -Force }
New-Item -ItemType Directory -Force -Path $verify | Out-Null
Expand-Archive -Path $zip -DestinationPath (Join-Path $verify "zip")
$makeappx = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\makeappx.exe" `
  -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending |
  Select-Object -First 1
if (-not $makeappx) { throw "makeappx.exe not found" }
& $makeappx.FullName unpack /p $msix /d (Join-Path $verify "msix") /o | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makeappx unpack failed" }

$buildHash = (Get-FileHash $exe -Algorithm SHA256).Hash
$zipHash = (Get-FileHash (Join-Path $verify "zip\uedit.exe") -Algorithm SHA256).Hash
$msixHash = (Get-FileHash (Join-Path $verify "msix\uedit.exe") -Algorithm SHA256).Hash
if ($buildHash -ne $zipHash) { throw "Portable exe hash differs from build output" }
if ($buildHash -ne $msixHash) { throw "MSIX exe hash differs from build output" }

[xml]$manifest = Get-Content (Join-Path $verify "msix\AppxManifest.xml")
$manifestVersion = $manifest.Package.Identity.Version
if ($manifestVersion -ne "$Version.0") {
  throw "Manifest version $manifestVersion does not match $Version.0"
}

$versionText = (Get-Content (Join-Path $verify "zip\VERSION.txt") -Raw).Trim()
Write-Output "portable:      $zip"
Write-Output "msix:          $msix"
Write-Output "exe sha256:    $buildHash"
Write-Output "VERSION.txt:   $versionText"
Write-Output "manifest:      $manifestVersion"
Write-Output "license file:  $(Test-Path (Join-Path $verify 'zip\PCRE2-LICENCE.txt'))"
Write-Output "hashes match build output: yes"
