[CmdletBinding()]
param(
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

function Invoke-Native {
    param(
        [Parameter(Mandatory)]
        [string]$FilePath,
        [string[]]$ArgumentList = @()
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Native command failed with exit code ${LASTEXITCODE}: $FilePath $ArgumentList"
    }
}

function Assert-WorkspacePath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $root = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\') + '\'
    $candidate = [System.IO.Path]::GetFullPath($Path)
    if (-not $candidate.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the workspace: $candidate"
    }
    return $candidate
}

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer was not found at $vswhere"
}

$vsInstall = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($vsInstall)) {
    throw 'Visual Studio with the x64/x86 C++ tools is required'
}

$devShellModule = Join-Path $vsInstall 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShellModule -Force
Enter-VsDevShell -VsInstallPath $vsInstall -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'

$cmake = (& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($cmake) -or -not (Test-Path -LiteralPath $cmake)) {
    throw 'CMake was not found in the Visual Studio installation'
}

$versionMatch = [regex]::Match(
    (Get-Content (Join-Path $projectRoot 'CMakeLists.txt') -Raw),
    '(?is)project\s*\([^)]*?\bVERSION\s+([0-9]+(?:\.[0-9]+)+)'
)
if (-not $versionMatch.Success) {
    throw 'The project version could not be read from CMakeLists.txt'
}
$projectVersion = $versionMatch.Groups[1].Value
$preset = "windows-x64-$($Configuration.ToLowerInvariant())"
$buildRoot = Join-Path $projectRoot "build\$preset"
$distRoot = Assert-WorkspacePath (Join-Path $projectRoot 'dist')
$packageName = "NetworkFlowMonitor-$projectVersion-win-x64"
$packageRoot = Assert-WorkspacePath (Join-Path $distRoot $packageName)
$zipPath = Assert-WorkspacePath (Join-Path $distRoot "$packageName.zip")

Invoke-Native -FilePath $cmake -ArgumentList @('--preset', $preset)
Invoke-Native -FilePath $cmake -ArgumentList @('--build', '--preset', $preset, '--clean-first')

New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
Get-ChildItem -LiteralPath $distRoot -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'NetworkFlowMonitor-*' } |
    ForEach-Object {
        $candidate = Assert-WorkspacePath $_.FullName
        if ($candidate -eq $distRoot) {
            throw "Refusing to remove the distribution root"
        }
        Remove-Item -LiteralPath $candidate -Recurse -Force
    }
if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null

Invoke-Native -FilePath $cmake -ArgumentList @('--install', $buildRoot, '--prefix', $packageRoot)

$executable = Join-Path $packageRoot 'NetworkFlowMonitor.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "The install tree does not contain $executable"
}
$unexpectedDlls = @(Get-ChildItem -LiteralPath $packageRoot -Filter '*.dll' -File -ErrorAction SilentlyContinue)
if ($unexpectedDlls.Count -ne 0) {
    throw "The package contains optional DLLs: $($unexpectedDlls.Name -join ', ')"
}

Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $zipPath -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
$size = (Get-Item -LiteralPath $executable).Length
Write-Host "Package: $zipPath"
Write-Host "Executable bytes: $size"
Write-Host "SHA256: $hash"
