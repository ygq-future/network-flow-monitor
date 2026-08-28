[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [ValidateRange(1, 120)][int]$StartupDelaySeconds = 2,
    [ValidateRange(1, 120)][int]$StopTimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NfmCollectorAcceptanceNative
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr CreateEvent(IntPtr attributes, bool manualReset, bool initialState, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool SetEvent(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr handle);
}
'@

$root = [IO.Path]::GetFullPath($PackageRoot)
$executable = Join-Path $root 'nfm-collector.exe'
$loggerName = 'NetworkFlowMonitor.SystemTrace'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Collector executable was not found: $executable" }

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'The collector acceptance probe must run elevated.'
}

function Test-NfmLogger {
    return (@(logman query -ets 2>$null) | Select-String -SimpleMatch $loggerName) -ne $null
}

if (Test-NfmLogger) { throw "Refusing to start while $loggerName already exists." }
$eventName = "Local\NetworkFlowMonitor.Stop.Probe.$PID.$([Guid]::NewGuid().ToString('N'))"
$eventHandle = [NfmCollectorAcceptanceNative]::CreateEvent([IntPtr]::Zero, $true, $false, $eventName)
if ($eventHandle -eq [IntPtr]::Zero) { throw "CreateEvent failed with Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())." }

$collector = $null
$runningBeforeSignal = $false
$signaled = $false
$exited = $false
$forcedCleanup = $false
$exitCode = $null
try {
    $collector = Start-Process -FilePath $executable -ArgumentList @('--stop-event', ('"' + $eventName + '"')) -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds $StartupDelaySeconds
    $collector.Refresh()
    $runningBeforeSignal = -not $collector.HasExited
    if ($runningBeforeSignal) {
        $signaled = [NfmCollectorAcceptanceNative]::SetEvent($eventHandle)
        $exited = $collector.WaitForExit($StopTimeoutSeconds * 1000)
    }
    if ($exited) { $collector.Refresh(); $exitCode = $collector.ExitCode }
} finally {
    if ($null -ne $collector) {
        $collector.Refresh()
        if (-not $collector.HasExited) {
            $forcedCleanup = $true
            Stop-Process -Id $collector.Id -Force -ErrorAction SilentlyContinue
        }
    }
    [NfmCollectorAcceptanceNative]::CloseHandle($eventHandle) | Out-Null
}

$loggerPresent = Test-NfmLogger
if ($loggerPresent) { & logman stop $loggerName -ets 2>$null | Out-Null }
$passed = $runningBeforeSignal -and $signaled -and $exited -and $exitCode -eq 0 -and -not $loggerPresent -and -not $forcedCleanup
[ordered]@{
    executable_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    collector_pid = if ($null -ne $collector) { $collector.Id } else { $null }
    running_before_signal = $runningBeforeSignal
    stop_event_signaled = $signaled
    exited_after_signal = $exited
    exit_code = $exitCode
    logger_present_after_exit = $loggerPresent
    forced_cleanup = $forcedCleanup
    passed = $passed
    measured_at_utc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json
if (-not $passed) { exit 1 }
