[CmdletBinding()]
param(
    [ValidateSet('Format', 'Fast', 'Full')]
    [string]$Mode = 'Fast',
    [ValidateRange(1, 64)]
    [int]$Jobs = [Math]::Min(12, [Environment]::ProcessorCount),
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$runStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logDirectory = Join-Path $projectRoot "build\quality-logs\$runStamp-$($Mode.ToLowerInvariant())"

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

function Show-FailureSummary {
    param(
        [Parameter(Mandatory)]
        [string]$LogPath
    )

    $diagnosticPattern =
        '(^FAILED:)|(^CMake Error)|(^ninja: error:)|(: fatal error )|(: error [A-Z]+[0-9]+:)|(^.*error: )|(^.*warning: )|([0-9]+% tests passed)'
    $diagnostics = @(Select-String -LiteralPath $LogPath -Pattern $diagnosticPattern |
            Select-Object -Last 80 |
            ForEach-Object { $_.Line })

    Write-Host ''
    Write-Host 'Relevant diagnostics:' -ForegroundColor Red
    if ($diagnostics.Count -gt 0) {
        $diagnostics | ForEach-Object { Write-Host $_ }
    } else {
        Write-Host '(No recognized diagnostic line; showing the log tail.)'
    }

    Write-Host ''
    Write-Host 'Last 80 log lines:' -ForegroundColor Red
    Get-Content -LiteralPath $LogPath -Tail 80 | ForEach-Object { Write-Host $_ }
    Write-Host ''
    Write-Host "Complete log: $LogPath" -ForegroundColor Yellow
}

function Remove-CompilerDependencyTrace {
    param(
        [Parameter(Mandatory)]
        [string]$LogPath
    )

    $filteredPath = "$LogPath.filtered"
    $reader = [System.IO.StreamReader]::new($LogPath, $true)
    $writer = [System.IO.StreamWriter]::new(
        $filteredPath,
        $false,
        [System.Text.UTF8Encoding]::new($false)
    )
    try {
        while ($null -ne ($line = $reader.ReadLine())) {
            if ($line.StartsWith('注意: 包含文件:') -or
                $line.StartsWith('Note: including file:')) {
                continue
            }
            $writer.WriteLine($line)
        }
    } finally {
        $writer.Dispose()
        $reader.Dispose()
    }
    Move-Item -LiteralPath $filteredPath -Destination $LogPath -Force
}

function Invoke-QualityPhase {
    param(
        [Parameter(Mandatory)]
        [string]$Name,
        [Parameter(Mandatory)]
        [string]$LogName,
        [Parameter(Mandatory)]
        [scriptblock]$Action
    )

    $logPath = Join-Path $logDirectory "$LogName.log"
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    Write-Host "[RUN ] $Name"

    $phaseError = $null
    try {
        & $Action *> $logPath
    } catch {
        $phaseError = $_
    }
    # /showIncludes is required for correct Ninja header dependencies. Some localized
    # MSVC/Ninja combinations echo every dependency even though CMake records the right
    # prefix. Keep diagnostics and build progress, but remove those redundant path lines.
    Remove-CompilerDependencyTrace -LogPath $logPath
    $stopwatch.Stop()

    if ($null -ne $phaseError) {
        Write-Host ("[FAIL] {0} ({1:N1} s)" -f $Name, $stopwatch.Elapsed.TotalSeconds) -ForegroundColor Red
        Show-FailureSummary -LogPath $logPath
        throw $phaseError
    }

    $logBytes = (Get-Item -LiteralPath $logPath).Length
    Write-Host ("[PASS] {0} ({1:N1} s, {2:N0} bytes logged)" -f
        $Name, $stopwatch.Elapsed.TotalSeconds, $logBytes) -ForegroundColor Green
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
$cpack = (& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cpack.exe' | Select-Object -First 1)
$clangFormat = (& $vswhere -latest -products * -find 'VC\Tools\Llvm\x64\bin\clang-format.exe' | Select-Object -First 1)
$clangTidy = (& $vswhere -latest -products * -find 'VC\Tools\Llvm\x64\bin\clang-tidy.exe' | Select-Object -First 1)

foreach ($tool in @($cmake, $ninja, $cpack, $clangFormat, $clangTidy)) {
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

New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
Write-Host "Quality gate: Mode=$Mode Jobs=$Jobs Clean=$($Clean.IsPresent -or $Mode -eq 'Full')"
Write-Host "Complete logs: $logDirectory"

if ($Mode -eq 'Format') {
    Invoke-QualityPhase -Name "Format $($sourceFiles.Count) C/C++ files" -LogName 'format' -Action {
        foreach ($file in $sourceFiles) {
            Invoke-Native -FilePath $clangFormat -ArgumentList @('-i', '--style=file', $file.FullName)
        }
    }
    Invoke-QualityPhase -Name 'Format .NET Solution' -LogName 'dotnet-format' -Action {
        Invoke-Native -FilePath 'dotnet' -ArgumentList @('format', (Join-Path $projectRoot 'NetworkFlowMonitor.sln'))
    }
    Write-Host "Quality gate '$Mode' passed."
    exit 0
}

Invoke-QualityPhase -Name "Check formatting for $($sourceFiles.Count) C/C++ files" -LogName 'format-check' -Action {
    foreach ($file in $sourceFiles) {
        Invoke-Native -FilePath $clangFormat -ArgumentList @('--dry-run', '--Werror', '--style=file', $file.FullName)
    }
}

Invoke-QualityPhase -Name 'Check .NET Code Formatting' -LogName 'dotnet-format-check' -Action {
    Invoke-Native -FilePath 'dotnet' -ArgumentList @('format', '--verify-no-changes', (Join-Path $projectRoot 'NetworkFlowMonitor.sln'))
}

$buildArguments = @('--parallel', $Jobs.ToString())
if ($Mode -eq 'Full' -or $Clean) {
    $buildArguments += '--clean-first'
}
$noModuleScan = '-DCMAKE_CXX_SCAN_FOR_MODULES=OFF'

Invoke-QualityPhase -Name 'Configure Debug' -LogName 'debug-configure' -Action {
    Invoke-Native -FilePath $cmake -ArgumentList @('--preset', 'windows-x64-debug', $noModuleScan)
}
Invoke-QualityPhase -Name 'Build Debug' -LogName 'debug-build' -Action {
    Invoke-Native -FilePath $cmake -ArgumentList (@('--build', '--preset', 'windows-x64-debug') + $buildArguments)
}
Invoke-QualityPhase -Name 'Test Debug' -LogName 'debug-test' -Action {
    # Automated quality gates are strictly headless and non-interactive.
    # Exclude legacy C++ UI tests, interactive window tests, and UAC-triggering admin tests.
    $ctestArguments = @(
        '-E', 'chdir', 'build/windows-x64-debug',
        'ctest', '--output-on-failure',
        '-LE', 'legacy-ui|interactive|requires-admin'
    )
    Invoke-Native -FilePath $cmake -ArgumentList $ctestArguments
}

Invoke-QualityPhase -Name 'Test .NET 10 WPF & Protocol Fixtures' -LogName 'dotnet-test' -Action {
    Invoke-Native -FilePath 'dotnet' -ArgumentList @('test', (Join-Path $projectRoot 'NetworkFlowMonitor.sln'), '--configuration', 'Debug', '--verbosity', 'minimal')
}

$tidyBuild = Join-Path $projectRoot 'build\windows-x64-tidy'
Invoke-QualityPhase -Name 'Configure clang-tidy and Clang Static Analyzer' -LogName 'tidy-configure' -Action {
    Invoke-Native -FilePath $cmake -ArgumentList @(
        '-S', $projectRoot,
        '-B', $tidyBuild,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Debug',
        '-DBUILD_TESTING=ON',
        '-DNFM_ENABLE_CLANG_TIDY=ON',
        "-DNFM_CLANG_TIDY_EXECUTABLE=$clangTidy",
        "-DCMAKE_MAKE_PROGRAM=$ninja",
        $noModuleScan
    )
}
Invoke-QualityPhase -Name 'Run clang-tidy and Clang Static Analyzer' -LogName 'tidy-build' -Action {
    Invoke-Native -FilePath $cmake -ArgumentList (@('--build', $tidyBuild) + $buildArguments)
}

if ($Mode -eq 'Full') {
    $analyzeBuild = Join-Path $projectRoot 'build\windows-x64-analyze'
    Invoke-QualityPhase -Name 'Configure MSVC static analysis' -LogName 'analyze-configure' -Action {
        Invoke-Native -FilePath $cmake -ArgumentList @(
            '-S', $projectRoot,
            '-B', $analyzeBuild,
            '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Debug',
            '-DBUILD_TESTING=ON',
            '-DNFM_ENABLE_MSVC_ANALYZE=ON',
            "-DCMAKE_MAKE_PROGRAM=$ninja",
            $noModuleScan
        )
    }
    Invoke-QualityPhase -Name 'Run MSVC static analysis' -LogName 'analyze-build' -Action {
        Invoke-Native -FilePath $cmake -ArgumentList (@('--build', $analyzeBuild) + $buildArguments)
    }

    Invoke-QualityPhase -Name 'Configure Release x64' -LogName 'release-configure' -Action {
        Invoke-Native -FilePath $cmake -ArgumentList @('--preset', 'windows-x64-release', $noModuleScan)
    }
    Invoke-QualityPhase -Name 'Build Release x64' -LogName 'release-build' -Action {
        Invoke-Native -FilePath $cmake -ArgumentList (@('--build', '--preset', 'windows-x64-release') + $buildArguments)
    }
    Invoke-QualityPhase -Name 'Test Release x64' -LogName 'release-test' -Action {
        $ctestArguments = @(
            '-E', 'chdir', 'build/windows-x64-release',
            'ctest', '--output-on-failure',
            '-LE', 'legacy-ui|interactive|requires-admin'
        )
        Invoke-Native -FilePath $cmake -ArgumentList $ctestArguments
    }
    Invoke-QualityPhase -Name 'Package Release x64' -LogName 'package' -Action {
        Invoke-Native -FilePath $cpack -ArgumentList @(
            '--config', (Join-Path $projectRoot 'build/windows-x64-release/CPackConfig.cmake'),
            '-B', (Join-Path $projectRoot 'build/windows-x64-release')
        )
    }

    $publishDir = Join-Path $projectRoot 'build\publish\nfm-ui'
    Invoke-QualityPhase -Name 'Publish .NET 10 WPF UI' -LogName 'dotnet-publish' -Action {
        if (Test-Path -LiteralPath $publishDir) {
            Remove-Item -Recurse -Force -LiteralPath $publishDir
        }
        $dotnetPublishArguments = @(
            'publish',
            (Join-Path $projectRoot 'apps\nfm-ui\NetworkFlowMonitor.csproj'),
            '-c', 'Release',
            '-r', 'win-x64',
            '--self-contained', 'true',
            '/p:PublishSingleFile=true',
            '-o', $publishDir
        )
        Invoke-Native -FilePath 'dotnet' -ArgumentList $dotnetPublishArguments
    }

    Invoke-QualityPhase -Name 'Package Portable Release (.NET 10 & Companion Collector)' -LogName 'package-portable' -Action {
        $version = '0.1.0'
        $cmakePath = Join-Path $projectRoot 'CMakeLists.txt'
        if (Test-Path -LiteralPath $cmakePath) {
            $cmakeText = Get-Content -LiteralPath $cmakePath -Raw
            if ($cmakeText -match 'project\s*\([^)]*VERSION\s+([0-9]+\.[0-9]+(\.[0-9]+)?)') {
                $version = $matches[1].Trim()
            }
        }

        $collectorCandidates = @(
            (Join-Path $projectRoot 'build\windows-x64-release\src\NetworkFlowMonitor.exe'),
            (Join-Path $projectRoot 'build\windows-x64-release\NetworkFlowMonitor.exe')
        )
        $cppCollectorExe = $collectorCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        if (-not $cppCollectorExe) {
            throw "C++ companion collector executable was not found in release build directory"
        }

        $stageDir = Join-Path $projectRoot 'build\stage\portable'
        if (Test-Path -LiteralPath $stageDir) {
            Remove-Item -Recurse -Force -LiteralPath $stageDir
        }
        New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

        # Copy published .NET 10 UI artifacts
        Copy-Item -Path (Join-Path $publishDir '*') -Destination $stageDir -Recurse -Force

        # Copy C++ companion collector executable as nfm-collector.exe
        Copy-Item -LiteralPath $cppCollectorExe -Destination (Join-Path $stageDir 'nfm-collector.exe') -Force

        # Copy README.md
        $readmePath = Join-Path $projectRoot 'README.md'
        if (Test-Path -LiteralPath $readmePath) {
            Copy-Item -LiteralPath $readmePath -Destination (Join-Path $stageDir 'README.md') -Force
        }

        # Create output zip in dist/
        $distDir = Join-Path $projectRoot 'dist'
        New-Item -ItemType Directory -Force -Path $distDir | Out-Null
        $zipPath = Join-Path $distDir "NetworkFlowMonitor-$version-win-x64-portable.zip"

        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }

        Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $zipPath -Force
        Write-Host "Created portable release archive: $zipPath"
    }
}

Write-Host "Quality gate '$Mode' passed."
