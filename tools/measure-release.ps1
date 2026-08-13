[CmdletBinding()]
param(
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$preset = "windows-x64-$($Configuration.ToLowerInvariant())"
$buildRoot = Join-Path $projectRoot "build\$preset"
$executable = Join-Path $buildRoot 'src\NetworkFlowMonitor.exe'
$distRoot = Join-Path $projectRoot 'dist'
$reportPath = Join-Path $distRoot "measurements-$($Configuration.ToLowerInvariant()).json"

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Build the $Configuration executable before measuring it: $executable"
}
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer was not found at $vswhere"
}

$dumpbin = (& $vswhere -latest -products * -find 'VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($dumpbin) -or -not (Test-Path -LiteralPath $dumpbin)) {
    throw 'dumpbin.exe was not found in the Visual Studio installation'
}

$os = Get-CimInstance Win32_OperatingSystem
$dumpbinOutput = @(& $dumpbin /DEPENDENTS $executable 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed with exit code $LASTEXITCODE"
}
$dependencies = @(
    $dumpbinOutput |
        ForEach-Object { [regex]::Match($_.ToString(), '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') } |
        Where-Object { $_.Success } |
        ForEach-Object { $_.Groups[1].Value } |
        Sort-Object -Unique
)

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$smoke = Start-Process -FilePath $executable -ArgumentList '--ui-smoke-test' -Wait -PassThru
$stopwatch.Stop()

New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
$report = [ordered]@{
    configuration = $Configuration
    executable = $executable
    executable_bytes = (Get-Item -LiteralPath $executable).Length
    executable_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    dependencies = $dependencies
    ui_smoke_exit_code = $smoke.ExitCode
    ui_smoke_wall_clock_ms = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)
    os_caption = $os.Caption
    os_version = $os.Version
    os_build = $os.BuildNumber
    measured_at_utc = [DateTime]::UtcNow.ToString('o')
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Host "Measurement report: $reportPath"
Write-Host ($report | ConvertTo-Json -Depth 4)
