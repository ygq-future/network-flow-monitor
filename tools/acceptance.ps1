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

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$smoke = Start-Process -FilePath $executable -ArgumentList '--ui-smoke-test' -PassThru
$peakWorkingSet = [int64]0
$peakCpuMilliseconds = [double]0
$sampleCount = 0
while (-not $smoke.HasExited) {
    try {
        $smoke.Refresh()
        $peakWorkingSet = [math]::Max($peakWorkingSet, [int64]$smoke.WorkingSet64)
        $peakCpuMilliseconds = [math]::Max(
            $peakCpuMilliseconds,
            [double]$smoke.TotalProcessorTime.TotalMilliseconds)
        $sampleCount++
    } catch [System.InvalidOperationException] {
        break
    }
    Start-Sleep -Milliseconds 25
}
$stopwatch.Stop()
$smoke.Refresh()
$peakWorkingSet = [math]::Max($peakWorkingSet, [int64]$smoke.WorkingSet64)
$peakCpuMilliseconds = [math]::Max(
    $peakCpuMilliseconds,
    [double]$smoke.TotalProcessorTime.TotalMilliseconds)
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
    ui_smoke_wall_clock_ms = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)
    ui_smoke_peak_working_set_bytes = $peakWorkingSet
    ui_smoke_peak_cpu_ms = [math]::Round($peakCpuMilliseconds, 3)
    ui_smoke_sample_count = $sampleCount
    service_query_exit_code = $query.ExitCode
    measured_at_utc = [DateTime]::UtcNow.ToString('o')
}
$report | ConvertTo-Json -Depth 5
