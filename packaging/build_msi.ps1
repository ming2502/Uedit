param(
  [string]$Version = "1.1.2.0",
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Configuration\uedit.exe"
if (-not (Test-Path $exe)) {
  throw "Build output not found: $exe"
}

$wixDir = Join-Path $root "build\tools\wix314"
$candle = Join-Path $wixDir "candle.exe"
$light = Join-Path $wixDir "light.exe"
if (-not (Test-Path $candle) -or -not (Test-Path $light)) {
  $zip = Join-Path $root "build\tools\wix314-binaries.zip"
  New-Item -ItemType Directory -Force -Path (Split-Path $zip) | Out-Null
  if (-not (Test-Path $zip)) {
    Write-Output "Downloading WiX Toolset 3.14.1..."
    $ProgressPreference = "SilentlyContinue"
    Invoke-WebRequest -UseBasicParsing `
      -Uri "https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip" `
      -OutFile $zip
  }
  if (Test-Path $wixDir) { Remove-Item $wixDir -Recurse -Force }
  Expand-Archive -Path $zip -DestinationPath $wixDir
}
if (-not (Test-Path $candle)) { throw "candle.exe not found in $wixDir" }

$outDir = Join-Path $root "build\msi"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$msi = Join-Path $dist "UEdit_${Version}_x64.msi"
$wxs = Join-Path $PSScriptRoot "UEdit.Installer.wxs"
$wixobj = Join-Path $outDir "UEdit.wixobj"

$candleArgs = @(
  "-nologo", "-arch", "x64",
  "-dVersion=$Version",
  "-dBuildDir=$(Split-Path $exe -Parent)",
  "-dSourceRoot=$root",
  "-out", $wixobj,
  $wxs
)
& $candle @candleArgs
if ($LASTEXITCODE -ne 0) { throw "candle failed" }

if (Test-Path $msi) { Remove-Item $msi -Force }
$lightArgs = @(
  "-nologo", "-ext", "WixUIExtension", "-cultures:zh-CN",
  "-out", $msi, $wixobj
)
& $light @lightArgs
if ($LASTEXITCODE -ne 0) { throw "light failed" }

Write-Output "MSI package: $msi"
