# Master test runner for Face Parallax project
param(
    [switch]$NoCompile,
    [switch]$NoPython,
    [switch]$IncludeUEBuild,
    [string]$UEProjectRoot
)

$root = Split-Path -Parent $PSScriptRoot
$testDir = $PSScriptRoot
$failed = $false

Write-Host "===== Face Parallax Test Runner =====" -ForegroundColor Cyan
Write-Host ""

# Resolve UE engine path
$EngineBatchDir = $null
$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
$machinePath = [Environment]::GetEnvironmentVariable("PATH", "Machine")
$combinedPaths = "$userPath;$machinePath;$env:PATH"
$allPaths = $combinedPaths -split ';' | Select-Object -Unique
foreach ($p in $allPaths) {
    $candidate = Join-Path $p "Build.bat"
    if (Test-Path $candidate) {
        $EngineBatchDir = $p
        break
    }
}
if (-not $EngineBatchDir) {
    $candidates = @(
        "H:\unreal\UE_5.8\Engine\Build\BatchFiles",
        "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles"
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "Build.bat")) {
            $EngineBatchDir = $c
            break
        }
    }
}

# 1. Python syntax validator
if (-not $NoPython) {
    Write-Host "--- Python Syntax Validator ---" -ForegroundColor Yellow
    try {
        $pyOut = python "$testDir\SyntaxValidator.py" --path "$root" 2>&1
        $pyExit = $LASTEXITCODE
        Write-Host $pyOut
        if ($pyExit -ne 0) {
            $failed = $true
            Write-Host "SYNTAX VALIDATION FAILED" -ForegroundColor Red
        } else {
            Write-Host "SYNTAX VALIDATION PASSED" -ForegroundColor Green
        }
    } catch {
        Write-Host "Python not available, skipping syntax validation" -ForegroundColor DarkYellow
    }
    Write-Host ""
}

# 2. C++ math tests
if (-not $NoCompile) {
    Write-Host "--- C++ Math Tests ---" -ForegroundColor Yellow
    $cppTest = "$testDir\ParallaxMathTests.cpp"
    $cppExe = "$testDir\ParallaxMathTests.exe"

    $compilers = @(
        @{name="g++ (ucrt64)"; cmd="g++ -std=c++17 -o `"$cppExe`" `"$cppTest`" -Werror -Wall -Wextra"},
        @{name="clang++"; cmd="clang++ -std=c++17 -o `"$cppExe`" `"$cppTest`" -Werror -Wall -Wextra"},
        @{name="cl (MSVC)"; cmd="cl /std:c++17 /EHsc /Fe:`"$cppExe`" `"$cppTest`" 2>&1"}
    )

    $compiled = $false
    foreach ($c in $compilers) {
        $which = (Get-Command $c.name.Split()[0] -ErrorAction SilentlyContinue)
        if ($which) {
            Write-Host "  Compiling with $($c.name)..."
            $compileOut = Invoke-Expression $c.cmd 2>&1
            if ($LASTEXITCODE -eq 0) {
                $compiled = $true
                break
            } else {
                Write-Host "  $($c.name) failed, trying next..." -ForegroundColor DarkYellow
            }
        }
    }

    if ($compiled -and (Test-Path $cppExe)) {
        Write-Host "  Running tests..."
        $testOut = & $cppExe 2>&1
        $testExit = $LASTEXITCODE
        Write-Host $testOut
        if ($testExit -ne 0) {
            $failed = $true
            Write-Host "C++ MATH TESTS FAILED" -ForegroundColor Red
        } else {
            Write-Host "C++ MATH TESTS PASSED" -ForegroundColor Green
        }
        Remove-Item $cppExe -ErrorAction SilentlyContinue
    } else {
        Write-Host "No C++ compiler found, skipping C++ math tests" -ForegroundColor DarkYellow
    }
    Write-Host ""
}

# 3. UE Build test (optional, requires Build.bat on PATH)
if ($IncludeUEBuild -and $EngineBatchDir) {
    Write-Host "--- UE Build Test ---" -ForegroundColor Yellow
    $origPath = $env:PATH
    $env:PATH = "$EngineBatchDir;$origPath"

    if (-not $UEProjectRoot) {
        $UEProjectRoot = Join-Path $root "SAMPLES\MyProject"
    }
    $UEProjectRoot = Resolve-Path $UEProjectRoot
    $UProjectPath = Join-Path $UEProjectRoot "MyProject.uproject"
    $ModuleName = "MyProjectEditor"
    $BuildArgs = @($ModuleName, "Win64", "Development", "`"$UProjectPath`"", "-WaitMutex", "-FromMsBuild")

    Write-Host "  Project: $UProjectPath"
    Write-Host "  Engine:   $EngineBatchDir"
    Write-Host "  Building $ModuleName..."

    $buildOut = & "Build.bat" $ModuleName "Win64" "Development" "`"$UProjectPath`"" "-WaitMutex" "-FromMsBuild" 2>&1
    $buildExit = $LASTEXITCODE

    $env:PATH = $origPath

    if ($buildExit -ne 0) {
        $failed = $true
        Write-Host "UE BUILD FAILED (exit $buildExit)" -ForegroundColor Red
        Write-Host $buildOut | Select-String -Pattern "error"
    } else {
        Write-Host "UE BUILD PASSED" -ForegroundColor Green
        $DllPath = Join-Path $UEProjectRoot "Binaries\Win64\UnrealEditor-MyProject.dll"
        if (Test-Path $DllPath) {
            Write-Host "[OK] DLL: $DllPath" -ForegroundColor Green
        }
    }
    Write-Host ""
} elseif ($IncludeUEBuild) {
    Write-Host "--- UE Build Test ---" -ForegroundColor Yellow
    Write-Host "[SKIP] Build.bat not found on PATH or known locations" -ForegroundColor DarkYellow
    Write-Host "       Set PATH to UE Engine\Build\BatchFiles or install UE at:"
    Write-Host "       H:\unreal\UE_5.8\Engine\Build\BatchFiles"
    Write-Host ""
}

Write-Host "===== RESULTS =====" -ForegroundColor Cyan
if ($failed) {
    Write-Host "SOME TESTS FAILED" -ForegroundColor Red
    exit 1
} else {
    Write-Host "ALL TESTS PASSED" -ForegroundColor Green
    exit 0
}
