param(
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Configuration\uedit.exe"
if (-not (Test-Path $exe)) {
  throw "Build output not found: $exe"
}

$staging = Join-Path $root "build\msix\staging"
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

Copy-Item $exe $staging
Copy-Item (Join-Path $PSScriptRoot "AppxManifest.xml") $staging
Copy-Item (Join-Path $PSScriptRoot "assets") $staging -Recurse

$makeappx = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\makeappx.exe" `
  -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending |
  Select-Object -First 1
if (-not $makeappx) {
  throw "makeappx.exe not found. Install the Windows SDK packaging tools."
}

$output = Join-Path $dist "UEdit_1.1.1.0_x64.msix"
& $makeappx.FullName pack /d $staging /p $output /o
if ($LASTEXITCODE -ne 0) {
  throw "makeappx failed with exit code $LASTEXITCODE"
}

Write-Output "MSIX package: $output"
Write-Output "Unsigned package. Sign it with signtool before installing:"
Write-Output "  signtool sign /fd SHA256 /a /f <cert.pfx> $output"
