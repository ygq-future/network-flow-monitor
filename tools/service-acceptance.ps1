[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackageRoot,
    [switch]$MutateScm,
    [switch]$UseUac
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $MutateScm) {
    throw 'This probe changes SCM state. Re-run with the explicit -MutateScm switch.'
}

$root = [System.IO.Path]::GetFullPath($PackageRoot)
$executable = Join-Path $root 'NetworkFlowMonitor.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Package executable was not found: $executable"
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isElevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isElevated -and -not $UseUac) {
    throw 'Run elevated or pass -UseUac so each mutating action can request elevation.'
}

$serviceName = 'NetworkFlowMonitor'
$initialService = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($null -ne $initialService) {
    throw "Refusing to take over an already-installed $serviceName service. Uninstall it first or use a clean target host."
}

$actions = [System.Collections.Generic.List[object]]::new()
$installedByProbe = $false
$failed = $false
$uacUsed = $false

function Get-ServiceObservation {
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -eq $service) {
        return [ordered]@{ installed = $false; state = 'not-installed' }
    }
    return [ordered]@{ installed = $true; state = $service.Status.ToString() }
}

function Invoke-NfmAction {
    param(
        [Parameter(Mandatory)]
        [string]$Argument,
        [Parameter(Mandatory)]
        [bool]$Mutating,
        [int[]]$ExpectedExitCodes = @(0)
    )

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = -1
    $errorText = $null
    try {
        $needsUac = $UseUac -and -not $isElevated -and $Mutating
        if ($needsUac) {
            $script:uacUsed = $true
            $process = Start-Process -FilePath $executable -ArgumentList $Argument -Verb RunAs -Wait -PassThru
        } else {
            $process = Start-Process -FilePath $executable -ArgumentList $Argument -Wait -PassThru -WindowStyle Hidden
        }
        $exitCode = $process.ExitCode
    } catch {
        $errorText = $_.Exception.Message
    }
    $stopwatch.Stop()
    $ok = $ExpectedExitCodes -contains $exitCode
    if (-not $ok) {
        $script:failed = $true
    }
    $actions.Add([ordered]@{
            argument = $Argument
            mutating = $Mutating
            exit_code = $exitCode
            ok = $ok
            error = $errorText
            elapsed_ms = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)
            service = Get-ServiceObservation
        })
    return $ok
}

try {
    $null = Invoke-NfmAction '--service-install-manual' $true
    # The command may report an error after SCM has already created the service.
    # The preflight rejects an existing service, so any service visible now belongs to this probe.
    $installedByProbe = (Get-ServiceObservation).installed
    $null = Invoke-NfmAction '--service-query' $false
    $null = Invoke-NfmAction '--service-start' $true
    $null = Invoke-NfmAction '--service-query' $false
    $null = Invoke-NfmAction '--service-stop' $true
    $null = Invoke-NfmAction '--service-query' $false
    $null = Invoke-NfmAction '--service-restart' $true
    $null = Invoke-NfmAction '--service-query' $false
    $null = Invoke-NfmAction '--service-stop' $true
    $null = Invoke-NfmAction '--service-uninstall' $true
    $installedByProbe = $false
    $null = Invoke-NfmAction '--service-query' $false -ExpectedExitCodes @(1060)
} finally {
    if ($installedByProbe) {
        try { $null = Invoke-NfmAction '--service-stop' $true } catch { $script:failed = $true }
        try { $null = Invoke-NfmAction '--service-uninstall' $true } catch { $script:failed = $true }
    }
}

$os = Get-CimInstance Win32_OperatingSystem
$report = [ordered]@{
    package_root = $root
    executable_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    os_caption = $os.Caption
    os_version = $os.Version
    os_build = $os.BuildNumber
    elevated_at_start = $isElevated
    uac_requested = $UseUac.IsPresent
    uac_used = $uacUsed
    actions = $actions
    final_service = Get-ServiceObservation
    passed = -not $failed -and -not (Get-ServiceObservation).installed
    measured_at_utc = [DateTime]::UtcNow.ToString('o')
}
$report | ConvertTo-Json -Depth 8
if (-not $report.passed) {
    exit 1
}
