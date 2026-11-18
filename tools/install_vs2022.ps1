$ErrorActionPreference = "Stop"
$resultFile = Join-Path $PSScriptRoot 'vs_install_result.txt'
"[$(Get-Date -Format o)] starting VS2022 Build Tools install" |
  Out-File -FilePath $resultFile -Encoding utf8

$installerArgs = @(
  "--quiet",
  "--wait",
  "--norestart",
  "--nocache",
  "--add", "Microsoft.VisualStudio.Workload.VCTools",
  "--includeRecommended"
)

$process = Start-Process -FilePath (Join-Path $PSScriptRoot 'vs_buildtools.exe') `
  -ArgumentList $installerArgs -Verb RunAs -Wait -PassThru

"[$(Get-Date -Format o)] installer exit=$($process.ExitCode)" |
  Out-File -FilePath $resultFile -Append -Encoding utf8
