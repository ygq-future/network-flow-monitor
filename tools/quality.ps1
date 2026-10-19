<#
.SYNOPSIS
    Repository quality gate for Network Flow Monitor.

.DESCRIPTION
    Mode Contract:
      Format
        Apply C++ and frontend formatting only.

      Fast
        Fast headless development gate:
        format verification,
        Debug C++ build/tests,
        frontend format/lint/typecheck/tests/build.

      Full
        Full headless validation:
        Fast +
        clang-tidy/static analysis +
        MSVC analysis +
        Release build/tests +
        package validation.
#>

[CmdletBinding()]
param(
    [ValidateSet('Format', 'Fast', 'Full')][string]$Mode = 'Fast',
    [ValidateRange(1, 64)][int]$Jobs = [Math]::Min(12, [Environment]::ProcessorCount),
    [switch]$Clean,
    [switch]$VerboseOutput
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$frontendRoot = Join-Path $projectRoot 'apps\nfm-ui'
$frontendManifest = Join-Path $frontendRoot 'package.json'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer was not found at $vswhere"
}
$vsInstall = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($vsInstall)) {
    throw 'Visual Studio with the x64 C++ workload is required.'
}
$installerDir = Split-Path -Parent $vswhere
$env:PATH = "$installerDir;$env:PATH"
Import-Module (Join-Path $vsInstall 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll') -Force
Enter-VsDevShell -VsInstallPath $vsInstall -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$env:VSLANG = '1033'

$cmake = (& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1)
$ninja = (& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' | Select-Object -First 1)
$cpack = (& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cpack.exe' | Select-Object -First 1)
$clangFormat = (& $vswhere -latest -products * -find 'VC\Tools\Llvm\x64\bin\clang-format.exe' | Select-Object -First 1)
$clangTidy = (& $vswhere -latest -products * -find 'VC\Tools\Llvm\x64\bin\clang-tidy.exe' | Select-Object -First 1)

$requiredTools = @($cmake, $ninja, $clangFormat)
if ($Mode -eq 'Full') {
    $requiredTools += @($cpack, $clangTidy)
}
foreach ($tool in $requiredTools) {
    if ([string]::IsNullOrWhiteSpace($tool) -or -not (Test-Path -LiteralPath $tool)) {
        throw "Required build tool was not found: $tool"
    }
}
$env:PATH = "$(Split-Path -Parent $cmake);$(Split-Path -Parent $ninja);$env:PATH"

$sourceFiles = @(Get-ChildItem -Path (Join-Path $projectRoot 'src'), (Join-Path $projectRoot 'include'), (Join-Path $projectRoot 'tests') -Recurse -File |
    Where-Object { $_.Extension -in '.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.hxx' } |
    Sort-Object FullName)

# State tracking for summary
$global:PassCount = 0
$global:WarnCount = 0
$global:FailCount = 0

function Write-SummaryAndExit {
    param([string]$GateMode, [int]$ExitCode)

    Write-Host ''
    Write-Host '----------------------------------------'
    Write-Host ('PASS {0,3}' -f $global:PassCount) -ForegroundColor Green
    Write-Host ('WARN {0,3}' -f $global:WarnCount) -ForegroundColor $(if ($global:WarnCount -gt 0) { 'Yellow' } else { 'DarkGray' })
    Write-Host ('FAIL {0,3}' -f $global:FailCount) -ForegroundColor $(if ($global:FailCount -gt 0) { 'Red' } else { 'DarkGray' })
    Write-Host ''

    if ($ExitCode -eq 0) {
        Write-Host ("Quality gate '{0}': PASS" -f $GateMode) -ForegroundColor Green
    } else {
        Write-Host ("Quality gate '{0}': FAIL" -f $GateMode) -ForegroundColor Red
    }
    exit $ExitCode
}

function Filter-OutputLines {
    param([string[]]$Lines)

    $filtered = [System.Collections.Generic.List[string]]::new()
    foreach ($rawLine in $Lines) {
        if ($null -eq $rawLine) { continue }
        $line = $rawLine.ToString().TrimEnd()
        if ([string]::IsNullOrWhiteSpace($line)) { continue }

        # Always preserve deprecation and obsolescence diagnostics
        if ($line -match '(?i)\b(?:deprecated|obsolete|will stop functioning|will be removed)\b') {
            $filtered.Add($line)
            continue
        }

        # Filter MSVC /showIncludes (Chinese and English)
        if ($line -match '^(?:注意:\s*包含文件:\s*|Note:\s*including\s*file:\s*)') { continue }

        # Filter normal build progress (Ninja / CMake / compiler command noise)
        if ($line -match '^\s*\[\s*\d+/\d+\]') { continue }
        if ($line -match '^\s*(?:Building|Linking|Generating)\b') { continue }

        # Filter Bun / package manager noise
        if ($line -match '^\s*(?:bun\s+v\d|\[\d+ms\]\s+resolved|checked\s+\d+\s+packages|Saved lockfile)') { continue }

        $filtered.Add($line)
    }
    return $filtered.ToArray()
}

function Extract-Diagnostics {
    param([string[]]$Lines, [int]$MaxDiagnostics = 30)

    $diagnostics = [System.Collections.Generic.List[string]]::new()

    # Pass 1: look for explicit error / warning / failure / deprecation patterns
    foreach ($line in $Lines) {
        if ($line -match '(?::\s*(?:fatal )?error\b|:\s*warning\b|error\s+[A-Z]\d+|warning\s+[A-Z]\d+|\bFAILED\b|Assertion failed|Expected:|Received:|error\s+TS\d+:|\bFAIL\b|(?i)\b(?:deprecated|obsolete|will stop functioning|will be removed)\b)') {
            $diagnostics.Add($line)
        }
    }

    # Pass 2: if no explicit pattern matched, take non-empty lines
    if ($diagnostics.Count -eq 0) {
        foreach ($line in $Lines) {
            $diagnostics.Add($line)
        }
    }

    $totalFound = $diagnostics.Count
    $output = [System.Collections.Generic.List[string]]::new()
    $count = [Math]::Min($totalFound, $MaxDiagnostics)
    for ($i = 0; $i -lt $count; $i++) {
        $output.Add($diagnostics[$i])
    }

    if ($totalFound -gt $MaxDiagnostics) {
        $omitted = $totalFound - $MaxDiagnostics
        $output.Add("... $omitted additional diagnostics omitted")
    }

    return $output.ToArray()
}

function Invoke-GateStep {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][scriptblock]$Action,
        [scriptblock]$ParseDetail
    )

    $colWidth = 30
    $paddedName = $Name.PadRight($colWidth)

    if ($VerboseOutput) {
        Write-Host "[RUN ] $Name"
        try {
            $result = & $Action
            $exitCode = if ($null -ne $LASTEXITCODE) { $LASTEXITCODE } else { 0 }
            if ($exitCode -ne 0) { throw "Command failed with exit code $exitCode" }

            $detail = ''
            if ($ParseDetail) {
                $detail = & $ParseDetail @($result)
            }
            Write-Host ("[PASS] {0} {1}" -f $paddedName, $detail).TrimEnd() -ForegroundColor Green
            $global:PassCount++
        } catch {
            Write-Host ("[FAIL] {0}" -f $paddedName) -ForegroundColor Red
            Write-Host $_.ToString() -ForegroundColor Red
            $global:FailCount++
            Write-SummaryAndExit -GateMode $Mode -ExitCode 1
        }
        return
    }

    # Default Compact execution: capture in memory
    $captured = @()
    $stepFailed = $false
    $failureException = $null
    $global:LASTEXITCODE = 0

    try {
        $prevEAP = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $captured = & $Action 2>&1
        $ErrorActionPreference = $prevEAP
        $exitCode = if ($null -ne $LASTEXITCODE) { $LASTEXITCODE } else { 0 }
        if ($exitCode -ne 0) {
            $stepFailed = $true
        }
    } catch {
        $stepFailed = $true
        $failureException = $_
    }

    $stringLines = @()
    if ($null -ne $captured) {
        foreach ($item in $captured) {
            if ($null -ne $item) { $stringLines += $item.ToString() }
        }
    }
    $filteredLines = Filter-OutputLines -Lines $stringLines

    if (-not $stepFailed) {
        $detail = ''
        if ($ParseDetail) {
            $detail = & $ParseDetail $filteredLines
        }
        Write-Host ("[PASS] {0} {1}" -f $paddedName, $detail).TrimEnd() -ForegroundColor Green
        $global:PassCount++
    } else {
        $global:FailCount++
        $diagnostics = @(Extract-Diagnostics -Lines @($filteredLines) -MaxDiagnostics 30)
        Write-Host ("[FAIL] {0}" -f $paddedName) -ForegroundColor Red
        Write-Host ''
        if ($diagnostics.Length -gt 0) {
            foreach ($diag in $diagnostics) {
                Write-Host "  $diag" -ForegroundColor Red
            }
        } elseif ($failureException) {
            Write-Host "  $($failureException.ToString())" -ForegroundColor Red
        }
        Write-SummaryAndExit -GateMode $Mode -ExitCode 1
    }
}

Write-Host "Quality gate: $Mode"
Write-Host ''

# ------------------------------------------------------------------------------
# 1. Format Mode
# ------------------------------------------------------------------------------
if ($Mode -eq 'Format') {
    Invoke-GateStep -Name 'C++ format' -Action {
        foreach ($file in $sourceFiles) {
            & $clangFormat -i --style=file $file.FullName
            if ($LASTEXITCODE -ne 0) { return $LASTEXITCODE }
        }
        return 0
    } -ParseDetail { "$($sourceFiles.Count) files formatted" }

    if (Test-Path -LiteralPath $frontendManifest) {
        Invoke-GateStep -Name 'Frontend format' -Action {
            Push-Location $frontendRoot
            try {
                & bun run format
            } finally {
                Pop-Location
            }
        }
    }

    Write-SummaryAndExit -GateMode $Mode -ExitCode 0
}

# ------------------------------------------------------------------------------
# 2. Fast Mode (Headless development quality gate)
# ------------------------------------------------------------------------------

# Phase 0: Deprecation policy check (authoritative config file validation)
Invoke-GateStep -Name 'Deprecation policy' -Action {
    $policyViolations = [System.Collections.Generic.List[string]]::new()

    # Check frontend tsconfig
    if (Test-Path -LiteralPath $frontendRoot) {
        $tsconfigs = @(Get-ChildItem -Path $frontendRoot -Filter 'tsconfig*.json' -File -ErrorAction SilentlyContinue)
        foreach ($cfg in $tsconfigs) {
            $content = Get-Content -LiteralPath $cfg.FullName -Raw -ErrorAction SilentlyContinue
            if ($content -match '"ignoreDeprecations"') {
                $policyViolations.Add("Forbidden deprecation suppression in $($cfg.Name): 'ignoreDeprecations'")
            }
            if ($content -match '"baseUrl"') {
                $policyViolations.Add("Deprecated compiler option in $($cfg.Name): 'baseUrl' is deprecated in modern TypeScript")
            }
        }

        # Check frontend eslint config
        $eslintConfigs = @(Get-ChildItem -Path $frontendRoot -Filter 'eslint.config.*' -File -ErrorAction SilentlyContinue)
        foreach ($cfg in $eslintConfigs) {
            $content = Get-Content -LiteralPath $cfg.FullName -Raw -ErrorAction SilentlyContinue
            if ($content -match 'eslint-disable\s+(@typescript-eslint/)?no-deprecated') {
                $policyViolations.Add("Forbidden blanket deprecation suppression in $($cfg.Name)")
            }
        }
    }

    # Check root and cmake files for blanket suppressions
    $cmakeFiles = @(Get-ChildItem -Path (Join-Path $projectRoot 'cmake'), (Join-Path $projectRoot 'CMakeLists.txt') -File -ErrorAction SilentlyContinue)
    foreach ($cfg in $cmakeFiles) {
        $content = Get-Content -LiteralPath $cfg.FullName -Raw -ErrorAction SilentlyContinue
        if ($content -match '/_CRT_SECURE_NO_WARNINGS\b' -and $content -match 'add_compile_definitions') {
            $policyViolations.Add("Forbidden blanket CRT warning suppression in $($cfg.Name)")
        }
    }

    if ($policyViolations.Count -gt 0) {
        throw ($policyViolations -join "`n")
    }
} -ParseDetail { '0 violations' }

# Phase A: C++ formatting check
Invoke-GateStep -Name 'C++ format' -Action {
    foreach ($file in $sourceFiles) {
        $out = & $clangFormat --dry-run --Werror --style=file $file.FullName 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Output $out
            return $LASTEXITCODE
        }
    }
    return 0
} -ParseDetail { "$($sourceFiles.Count) files" }

# Phase B: C++ Debug configure
Invoke-GateStep -Name 'C++ Debug configure' -Action {
    & $cmake --preset windows-x64-debug
}

# Phase C: C++ Debug build (incremental by default, --clean-first if $Clean)
$debugBuildArgs = @('--build', '--preset', 'windows-x64-debug', '--parallel', $Jobs.ToString())
if ($Clean) {
    $debugBuildArgs += '--clean-first'
}
Invoke-GateStep -Name 'C++ Debug build' -Action {
    & $cmake @debugBuildArgs
}

# Phase D: C++ automated tests (headless, non-interactive, exclude admin)
Invoke-GateStep -Name 'C++ tests' -Action {
    & $cmake -E chdir 'build/windows-x64-debug' ctest --output-on-failure -LE 'requires-admin'
} -ParseDetail {
    param($lines)
    $text = $lines -join "`n"
    if ($text -match '(\d+)%\s+tests\s+passed,\s+(\d+)\s+tests\s+failed\s+out\s+of\s+(\d+)') {
        $total = [int]$matches[3]
        $failed = [int]$matches[2]
        $passed = $total - $failed
        return "$passed passed"
    }
    return ''
}

# Phase E - J: Frontend verification (if apps/nfm-ui/package.json exists)
if (Test-Path -LiteralPath $frontendManifest) {
    $packageJson = Get-Content -LiteralPath $frontendManifest -Raw | ConvertFrom-Json
    $hasFrontendTest = ($null -ne $packageJson.scripts) -and ($null -ne $packageJson.scripts.test)

    # E. Frontend dependencies
    Invoke-GateStep -Name 'Frontend dependencies' -Action {
        Push-Location $frontendRoot
        try {
            & bun install --frozen-lockfile
            if ($LASTEXITCODE -ne 0) { return $LASTEXITCODE }
            return 0
        } finally {
            Pop-Location
        }
    }

    # F. Prettier check
    Invoke-GateStep -Name 'Prettier' -Action {
        Push-Location $frontendRoot
        try {
            & bun run format:check
            if ($LASTEXITCODE -ne 0) { return $LASTEXITCODE }
            return 0
        } finally {
            Pop-Location
        }
    }

    # G. ESLint (0 warnings)
    Invoke-GateStep -Name 'ESLint' -Action {
        Push-Location $frontendRoot
        try {
            & bun run lint --max-warnings 0
            if ($LASTEXITCODE -ne 0) { return $LASTEXITCODE }
            return 0
        } finally {
            Pop-Location
        }
    } -ParseDetail { '0 warnings' }

    # H. TypeScript
    Invoke-GateStep -Name 'TypeScript' -Action {
        Push-Location $frontendRoot
        try {
            & bun run typecheck
            if ($LASTEXITCODE -ne 0) { return $LASTEXITCODE }
            return 0
        } finally {
            Pop-Location
        }
    } -ParseDetail { '0 errors' }

    # I. Frontend tests (if defined in package.json)
    if ($hasFrontendTest) {
        Invoke-GateStep -Name 'Frontend tests' -Action {
            Push-Location $frontendRoot
            try {
                & bun run test
                if ($LASTEXITCODE -ne 0) { return $LASTEXITCODE }
                return 0
            } finally {
                Pop-Location
            }
        } -ParseDetail {
            param($lines)
            $text = $lines -join "`n"
            if ($text -match '(\d+)\s+passed') {
                return "$($matches[1]) passed"
            }
            return ''
        }
    }

    # J. Frontend production build
    Invoke-GateStep -Name 'Frontend build' -Action {
        Push-Location $frontendRoot
        try {
            & bun run build
            if ($LASTEXITCODE -ne 0) { return $LASTEXITCODE }
            return 0
        } finally {
            Pop-Location
        }
    }
}

# If in Fast mode, finish here
if ($Mode -eq 'Fast') {
    Write-SummaryAndExit -GateMode $Mode -ExitCode 0
}

# ------------------------------------------------------------------------------
# 3. Full Mode (Deep validation & Release packaging)
# ------------------------------------------------------------------------------

$fullBuildArgs = @('--parallel', $Jobs.ToString())
if ($Clean -or $Mode -eq 'Full') {
    $fullBuildArgs += '--clean-first'
}

# Phase 11: clang-tidy & Clang Static Analyzer
$tidyBuild = Join-Path $projectRoot 'build\windows-x64-tidy'
Invoke-GateStep -Name 'Configure clang-tidy' -Action {
    & $cmake -S $projectRoot -B $tidyBuild -G 'Ninja' -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DNFM_ENABLE_CLANG_TIDY=ON "-DNFM_CLANG_TIDY_EXECUTABLE=$clangTidy" "-DCMAKE_MAKE_PROGRAM=$ninja"
}

Invoke-GateStep -Name 'clang-tidy' -Action {
    & $cmake (@('--build', $tidyBuild) + $fullBuildArgs)
} -ParseDetail { '0 diagnostics' }

# Phase 12: MSVC static analysis
$analyzeBuild = Join-Path $projectRoot 'build\windows-x64-analyze'
Invoke-GateStep -Name 'Configure MSVC analyze' -Action {
    & $cmake -S $projectRoot -B $analyzeBuild -G 'Ninja' -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DNFM_ENABLE_MSVC_ANALYZE=ON "-DCMAKE_MAKE_PROGRAM=$ninja"
}

Invoke-GateStep -Name 'MSVC analyze' -Action {
    & $cmake (@('--build', $analyzeBuild) + $fullBuildArgs)
} -ParseDetail { '0 warnings' }

# Phase 13: C++ Release configure & build
Invoke-GateStep -Name 'Configure Release x64' -Action {
    & $cmake --preset windows-x64-release
}

Invoke-GateStep -Name 'C++ Release build' -Action {
    & $cmake (@('--build', '--preset', 'windows-x64-release') + $fullBuildArgs)
}

# Phase 14: C++ Release tests
Invoke-GateStep -Name 'C++ Release tests' -Action {
    & $cmake -E chdir 'build/windows-x64-release' ctest --output-on-failure -LE 'requires-admin'
} -ParseDetail {
    param($lines)
    $text = $lines -join "`n"
    if ($text -match '(\d+)%\s+tests\s+passed,\s+(\d+)\s+tests\s+failed\s+out\s+of\s+(\d+)') {
        $total = [int]$matches[3]
        $failed = [int]$matches[2]
        $passed = $total - $failed
        return "$passed passed"
    }
    return ''
}

# Phase 15: Package Release
Invoke-GateStep -Name 'Package Release' -Action {
    & $cpack --config (Join-Path $projectRoot 'build\windows-x64-release\CPackConfig.cmake') -B (Join-Path $projectRoot 'build\windows-x64-release')
}

Write-SummaryAndExit -GateMode $Mode -ExitCode 0
