[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

function Invoke-Native {
    param([Parameter(Mandatory)][string]$FilePath, [string[]]$ArgumentList = @())
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) { throw "Native command failed: $FilePath $ArgumentList" }
}

$vsInstall = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($vsInstall)) { throw 'Visual Studio C++ tools are required.' }
Import-Module (Join-Path $vsInstall 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll') -Force
Enter-VsDevShell -VsInstallPath $vsInstall -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$cmake = (& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1)
$cpack = (& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cpack.exe' | Select-Object -First 1)
$preset = "windows-x64-$($Configuration.ToLowerInvariant())"
$buildRoot = Join-Path $projectRoot "build\$preset"
$distRoot = Join-Path $projectRoot 'dist'

Invoke-Native $cmake @('--preset', $preset)
Invoke-Native $cmake @('--build', '--preset', $preset, '--clean-first')
New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
Invoke-Native $cpack @('--config', (Join-Path $buildRoot 'CPackConfig.cmake'), '-B', $distRoot)

$archive = Get-ChildItem -LiteralPath $distRoot -Filter 'NetworkFlowMonitor-*-win-x64.zip' -File |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if ($null -eq $archive) { throw 'CPack did not produce the expected ZIP archive.' }
Write-Host "Package: $($archive.FullName)"
Write-Host "SHA256: $((Get-FileHash -LiteralPath $archive.FullName -Algorithm SHA256).Hash)"
