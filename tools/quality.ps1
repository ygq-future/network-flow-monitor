[CmdletBinding()]
param(
    [ValidateSet('Format', 'Fast', 'Full')]
    [string]$Mode = 'Fast'
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
$ninja = (& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' | Select-Object -First 1)
$clangFormat = (& $vswhere -latest -products * -find 'VC\Tools\Llvm\x64\bin\clang-format.exe' | Select-Object -First 1)
$clangTidy = (& $vswhere -latest -products * -find 'VC\Tools\Llvm\x64\bin\clang-tidy.exe' | Select-Object -First 1)

foreach ($tool in @($cmake, $ninja, $clangFormat, $clangTidy)) {
    if ([string]::IsNullOrWhiteSpace($tool) -or -not (Test-Path -LiteralPath $tool)) {
        throw "A required build tool could not be located: $tool"
    }
}

$env:PATH = "$(Split-Path -Parent $cmake);$(Split-Path -Parent $ninja);$env:PATH"

$sourceFiles = @(
    Get-ChildItem -Path (Join-Path $projectRoot 'src'),
                        (Join-Path $projectRoot 'include'),
                        (Join-Path $projectRoot 'tests') `
                  -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in '.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.hxx', '.ixx' } |
        Sort-Object FullName
)

if ($Mode -eq 'Format') {
    foreach ($file in $sourceFiles) {
        Invoke-Native -FilePath $clangFormat -ArgumentList @('-i', '--style=file', $file.FullName)
    }
    Write-Host "Formatted $($sourceFiles.Count) C/C++ files."
    exit 0
}

foreach ($file in $sourceFiles) {
    Invoke-Native -FilePath $clangFormat -ArgumentList @('--dry-run', '--Werror', '--style=file', $file.FullName)
}

Invoke-Native -FilePath $cmake -ArgumentList @('--preset', 'windows-x64-debug')
Invoke-Native -FilePath $cmake -ArgumentList @('--build', '--preset', 'windows-x64-debug')
Invoke-Native -FilePath $cmake -ArgumentList @('--build', 'build/windows-x64-debug', '--target', 'test')

$tidyBuild = Join-Path $projectRoot 'build\windows-x64-tidy'
Invoke-Native -FilePath $cmake -ArgumentList @(
    '-S', $projectRoot,
    '-B', $tidyBuild,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Debug',
    '-DBUILD_TESTING=ON',
    '-DNFM_ENABLE_CLANG_TIDY=ON',
    "-DNFM_CLANG_TIDY_EXECUTABLE=$clangTidy",
    "-DCMAKE_MAKE_PROGRAM=$ninja"
)
Invoke-Native -FilePath $cmake -ArgumentList @('--build', $tidyBuild)

if ($Mode -eq 'Full') {
    $analyzeBuild = Join-Path $projectRoot 'build\windows-x64-analyze'
    Invoke-Native -FilePath $cmake -ArgumentList @(
        '-S', $projectRoot,
        '-B', $analyzeBuild,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Debug',
        '-DBUILD_TESTING=ON',
        '-DNFM_ENABLE_MSVC_ANALYZE=ON',
        "-DCMAKE_MAKE_PROGRAM=$ninja"
    )
    Invoke-Native -FilePath $cmake -ArgumentList @('--build', $analyzeBuild)

    Invoke-Native -FilePath $cmake -ArgumentList @('--preset', 'windows-x64-release')
    Invoke-Native -FilePath $cmake -ArgumentList @('--build', '--preset', 'windows-x64-release')
    Invoke-Native -FilePath $cmake -ArgumentList @('--build', 'build/windows-x64-release', '--target', 'test')
}

Write-Host "Quality gate '$Mode' passed."
