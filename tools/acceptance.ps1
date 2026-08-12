[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackageRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [System.IO.Path]::GetFullPath($PackageRoot)
$executable = Join-Path $root 'NetworkFlowMonitor.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Package executable was not found: $executable"
}

$os = Get-CimInstance Win32_OperatingSystem
$service = Get-Service -Name NetworkFlowMonitor -ErrorAction SilentlyContinue
$serviceStatus = if ($null -eq $service) {
    [ordered]@{ installed = $false; state = 'not-installed'; start_type = $null }
} else {
    [ordered]@{ installed = $true; state = $service.Status.ToString(); start_type = $service.StartType.ToString() }
}

$smoke = Start-Process -FilePath $executable -ArgumentList '--ui-smoke-test' -Wait -PassThru
$query = Start-Process -FilePath $executable -ArgumentList '--service-query' -Wait -PassThru
$report = [ordered]@{
    package_root = $root
    executable_bytes = (Get-Item -LiteralPath $executable).Length
    executable_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    os_caption = $os.Caption
    os_version = $os.Version
    os_build = $os.BuildNumber
    service = $serviceStatus
    ui_smoke_exit_code = $smoke.ExitCode
    service_query_exit_code = $query.ExitCode
    measured_at_utc = [DateTime]::UtcNow.ToString('o')
}
$report | ConvertTo-Json -Depth 5
