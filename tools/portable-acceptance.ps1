[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackageRoot,
    [ValidateRange(1, 120)]
    [int]$StartupDelaySeconds = 2,
    [ValidateRange(1, 120)]
    [int]$StopTimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class NetworkFlowMonitorPortableAcceptanceNative
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr CreateEvent(
        IntPtr attributes, bool manualReset, bool initialState, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool SetEvent(IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr handle);
}
'@

$root = [System.IO.Path]::GetFullPath($PackageRoot)
$executable = Join-Path $root 'NetworkFlowMonitor.exe'
$serviceName = 'NetworkFlowMonitor'
$loggerName = 'NetworkFlowMonitor.SystemTrace'

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Package executable was not found: $executable"
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'The portable collector acceptance probe must run elevated.'
}

$unexpectedFiles = @(Get-ChildItem -LiteralPath $root -File | Where-Object {
        $_.Name -notin @('NetworkFlowMonitor.exe', 'README.md')
    })

function Test-NfmLogger {
    $output = @(logman query -ets 2>$null)
    return ($output | Select-String -SimpleMatch $loggerName) -ne $null
}

function Get-NfmServiceObservation {
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -eq $service) {
        return [ordered]@{ installed = $false; state = 'not-installed' }
    }
    return [ordered]@{ installed = $true; state = $service.Status.ToString() }
}

$initialLoggerPresent = Test-NfmLogger
$initialService = Get-NfmServiceObservation
if ($initialLoggerPresent) {
    throw "Refusing to start the probe while $loggerName already exists. Stop the existing project logger first."
}

$eventName = "Local\NetworkFlowMonitor.Stop.Probe.$PID.$([Guid]::NewGuid().ToString('N'))"
$eventHandle = [NetworkFlowMonitorPortableAcceptanceNative]::CreateEvent(
    [IntPtr]::Zero, $true, $false, $eventName)
if ($eventHandle -eq [IntPtr]::Zero) {
    $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "CreateEvent failed with Win32 error $errorCode."
}

$collector = $null
$signaled = $false
$exited = $false
$forcedCleanup = $false
$exitCode = $null
$loggerCleanupAttempted = $false
$loggerCleanupExitCode = $null
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

try {
    $collector = Start-Process -FilePath $executable -ArgumentList @(
        '--collector', '--stop-event', ('"' + $eventName + '"')
    ) -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds $StartupDelaySeconds
    $collector.Refresh()
    $runningBeforeSignal = -not $collector.HasExited

    if ($runningBeforeSignal) {
        $signaled = [NetworkFlowMonitorPortableAcceptanceNative]::SetEvent($eventHandle)
        $exited = $collector.WaitForExit($StopTimeoutSeconds * 1000)
    } else {
        $exited = $true
    }

    if ($exited) {
        $collector.Refresh()
        $exitCode = $collector.ExitCode
    } else {
        $forcedCleanup = $true
        Stop-Process -Id $collector.Id -Force -ErrorAction SilentlyContinue
        $collector.WaitForExit(5000)
    }
} finally {
    if ($null -ne $collector) {
        try {
            $collector.Refresh()
            if (-not $collector.HasExited) {
                $forcedCleanup = $true
                Stop-Process -Id $collector.Id -Force -ErrorAction SilentlyContinue
            }
        } catch [System.InvalidOperationException] {
            # The process exited between Refresh and HasExited; no cleanup is needed.
        }
    }
    if ($eventHandle -ne [IntPtr]::Zero) {
        [NetworkFlowMonitorPortableAcceptanceNative]::CloseHandle($eventHandle) | Out-Null
    }
    $stopwatch.Stop()
}

$loggerPresentAfterExit = Test-NfmLogger
if ($loggerPresentAfterExit -and -not $initialLoggerPresent) {
    $loggerCleanupAttempted = $true
    & logman stop $loggerName -ets 2>$null | Out-Null
    $loggerCleanupExitCode = $LASTEXITCODE
}
$loggerPresentFinal = Test-NfmLogger
$serviceFinal = Get-NfmServiceObservation
$processStillRunning = $false
if ($null -ne $collector) {
    try {
        $collector.Refresh()
        $processStillRunning = -not $collector.HasExited
    } catch [System.InvalidOperationException] {
        $processStillRunning = $false
    }
}

$passed = $runningBeforeSignal -and $signaled -and $exited -and $exitCode -eq 0 -and
    -not $processStillRunning -and -not $loggerPresentFinal -and
    $serviceFinal.state -eq $initialService.state -and -not $forcedCleanup
$report = [ordered]@{
    package_root = $root
    executable_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    os_caption = (Get-CimInstance Win32_OperatingSystem).Caption
    os_version = (Get-CimInstance Win32_OperatingSystem).Version
    os_build = (Get-CimInstance Win32_OperatingSystem).BuildNumber
    initial_service = $initialService
    final_service = $serviceFinal
    initial_logger_present = $initialLoggerPresent
    logger_present_after_exit = $loggerPresentAfterExit
    logger_cleanup_attempted = $loggerCleanupAttempted
    logger_cleanup_exit_code = $loggerCleanupExitCode
    logger_present_final = $loggerPresentFinal
    event_name_prefix = 'Local\NetworkFlowMonitor.Stop.'
    collector_pid = if ($null -ne $collector) { $collector.Id } else { $null }
    running_before_signal = $runningBeforeSignal
    stop_event_signaled = $signaled
    exited_after_signal = $exited
    exit_code = $exitCode
    process_still_running = $processStillRunning
    forced_cleanup = $forcedCleanup
    elapsed_ms = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)
    unexpected_package_files = @($unexpectedFiles | ForEach-Object { $_.Name })
    passed = $passed
    measured_at_utc = [DateTime]::UtcNow.ToString('o')
}
$report | ConvertTo-Json -Depth 8
if (-not $passed) {
    exit 1
}
