param(
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Configuration\uedit.exe"
if (-not (Test-Path $exe)) {
  throw "Build output not found: $exe"
}

$staging = Join-Path $root "build\portable\UEdit"
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

Copy-Item $exe $staging
"UEdit 1.1.1.0 (x64)" | Out-File (Join-Path $staging "VERSION.txt") -Encoding utf8

$license = Join-Path $root "third_party\pcre2-10.44\LICENCE"
if (Test-Path $license) {
  Copy-Item $license (Join-Path $staging "PCRE2-LICENCE.txt")
}

$zip = Join-Path $dist "UEdit-1.1.1-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zip
Write-Output "Portable package: $zip"
