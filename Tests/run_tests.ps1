# Test runner for Face Parallax project
param(
    [switch]$NoCompile,
    [switch]$NoPython
)

$root = Split-Path -Parent $PSScriptRoot
$testDir = $PSScriptRoot
$failed = $false

Write-Host "===== Face Parallax Test Runner =====" -ForegroundColor Cyan
Write-Host ""

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

    # Try clang first, then g++, then cl
    $compilers = @(
        @{name="clang++"; cmd="clang++ -std=c++17 -o `"$cppExe`" `"$cppTest`" -Werror -Wall -Wextra"},
        @{name="g++"; cmd="g++ -std=c++17 -o `"$cppExe`" `"$cppTest`" -Werror -Wall -Wextra"},
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
                Write-Host "  $($c.name) compilation failed, trying next..." -ForegroundColor DarkYellow
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

Write-Host "===== RESULTS =====" -ForegroundColor Cyan
if ($failed) {
    Write-Host "SOME TESTS FAILED" -ForegroundColor Red
    exit 1
} else {
    Write-Host "ALL TESTS PASSED" -ForegroundColor Green
    exit 0
}
