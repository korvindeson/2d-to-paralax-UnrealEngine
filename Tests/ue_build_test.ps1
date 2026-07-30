# UE Build Test for FaceParallax
# Builds the MyProject Editor target and verifies DLL creation.
# Prerequisites: Build.bat must be on PATH (UE engine Build/BatchFiles/)
#
# Usage:
#   .\Tests\ue_build_test.ps1 [-ProjectRoot <path>]
#   Default ProjectRoot: .\SAMPLES\MyProject

param(
    [string]$ProjectRoot = (Join-Path $PSScriptRoot "..\SAMPLES\MyProject"),
    [switch]$NoSyntaxValidation
)

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$ProjectRoot = Resolve-Path $ProjectRoot
$UProjectPath = Join-Path $ProjectRoot "MyProject.uproject"
$ModuleName = "MyProjectEditor"
$failed = $false

# Resolve Build.bat
$EngineBatchDir = $null
$allPaths = @($env:PATH -split ';' | Select-Object -Unique)
foreach ($p in $allPaths) {
    if (Test-Path (Join-Path $p "Build.bat")) {
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
if (-not $EngineBatchDir) {
    Write-Host "[FAIL] Build.bat not found on PATH or known locations" -ForegroundColor Red
    exit 1
}
$origPath = $env:PATH
$env:PATH = "$EngineBatchDir;$origPath"

Write-Host "=== UE Build Test ===" -ForegroundColor Cyan
Write-Host "Project: $UProjectPath"
Write-Host "Engine:  $EngineBatchDir"

# 1. Syntax validation
if (-not $NoSyntaxValidation) {
    Write-Host "`n--- Syntax Validation ---" -ForegroundColor Yellow
    python (Join-Path $PSScriptRoot "SyntaxValidator.py") --path "$root" 2>&1
    if ($LASTEXITCODE -ne 0) {
        $failed = $true
        Write-Host "SYNTAX VALIDATION FAILED" -ForegroundColor Red
    } else {
        Write-Host "SYNTAX VALIDATION PASSED" -ForegroundColor Green
    }
}

# 2. Clean Intermediate
$IntermediateDir = Join-Path $ProjectRoot "Intermediate"
if (Test-Path $IntermediateDir) {
    Write-Host "`n[CLEAN] Removing Intermediate/..."
    Remove-Item -Recurse -Force $IntermediateDir -ErrorAction SilentlyContinue
}

# 3. Build
Write-Host "`n--- Building $ModuleName (Win64, Development) ---" -ForegroundColor Yellow
$buildOut = & "Build.bat" $ModuleName "Win64" "Development" "`"$UProjectPath`"" "-WaitMutex" "-FromMsBuild" 2>&1
$buildExit = $LASTEXITCODE
$env:PATH = $origPath

if ($buildExit -ne 0) {
    $failed = $true
    Write-Host "BUILD FAILED (exit $buildExit)" -ForegroundColor Red
    $buildOut | Select-String -Pattern "error" | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}
Write-Host "BUILD PASSED" -ForegroundColor Green

# 4. Verify DLL
$DllPath = Join-Path $ProjectRoot "Binaries\Win64\UnrealEditor-MyProject.dll"
if (Test-Path $DllPath) {
    Write-Host "[OK] DLL: $DllPath" -ForegroundColor Green
} else {
    Write-Host "[WARN] DLL not found at expected path" -ForegroundColor Yellow
}

# 5. Math tests
Write-Host "`n--- Math Tests ---" -ForegroundColor Yellow
$cppExe = Join-Path $PSScriptRoot "ParallaxMathTests.exe"
if (Test-Path $cppExe) {
    & $cppExe
    if ($LASTEXITCODE -ne 0) {
        $failed = $true
        Write-Host "MATH TESTS FAILED" -ForegroundColor Red
        exit 1
    }
    Write-Host "MATH TESTS PASSED" -ForegroundColor Green
} else {
    Write-Host "[SKIP] ParallaxMathTests.exe not found -- compile with:" -ForegroundColor DarkYellow
    Write-Host "       g++ -std=c++17 -o Tests\ParallaxMathTests.exe Tests\ParallaxMathTests.cpp -Werror -Wall -Wextra"
}

if ($failed) {
    Write-Host "`n=== UE Build Test: FAIL ===" -ForegroundColor Red
    exit 1
} else {
    Write-Host "`n=== UE Build Test: PASS ===" -ForegroundColor Cyan
    exit 0
}
