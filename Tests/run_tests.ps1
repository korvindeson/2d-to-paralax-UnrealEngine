# Master test runner for Face Parallax project
param(
    [switch]$NoCompile,
    [switch]$NoPython,
    [switch]$IncludeUEBuild,
    [switch]$SyncSamples,
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

# 1a. Verify _gen_embed.py is up-to-date (compare embedded sources to disk)
Write-Host "--- _gen_embed.py Staleness Check ---" -ForegroundColor Yellow
$genEmbedPy = Join-Path $root "_gen_embed.py"
$genEmbedCheck = Join-Path $root "Tests\_gen_embed_check.py"
if (Test-Path $genEmbedPy) {
    # Parse EMBEDDED_SOURCES from deploy.py via regex (avoid importing unreal module)
    @"
import re, os, base64
with open(os.path.join(r'$root', 'deploy.py'), 'r', encoding='utf-8') as f:
    content = f.read()
# Find EMBEDDED_SOURCES dict block: from '{' at brace_start to matching '}'
m_start = re.search(r'EMBEDDED_SOURCES\s*=\s*\{', content)
if not m_start:
    print('Could not find EMBEDDED_SOURCES in deploy.py')
    exit(1)
brace_depth = 0
start = content.index('{', m_start.start())
end = start
for i in range(start, len(content)):
    if content[i] == '{': brace_depth += 1
    elif content[i] == '}':
        brace_depth -= 1
        if brace_depth == 0: end = i + 1; break
block = content[start:end]
# Extract quoted filename and base64 string pairs
pairs = re.findall(r'\"([\w.]+)\":\s*base64\.b64decode\(\'([^\']+)\'\)', block)
ok = True
for fname, b64data in pairs:
    fpath = os.path.join(r'$root', fname)
    if not os.path.exists(fpath):
        print(f'MISSING: {fname}')
        ok = False; continue
    with open(fpath, 'rb') as f:
        disk_data = f.read()
    emb_data = base64.b64decode(b64data)
    if disk_data != emb_data:
        print(f'STALE: {fname} (run _gen_embed.py to update)')
        ok = False
if ok:
    print('All embedded sources are up-to-date')
    exit(0)
else:
    exit(1)
"@ | Out-File -FilePath $genEmbedCheck -Encoding utf8
    $pyOut = python $genEmbedCheck 2>&1
    $pyExit = $LASTEXITCODE
    Remove-Item $genEmbedCheck -ErrorAction SilentlyContinue
    Write-Host $pyOut
    if ($pyExit -ne 0) {
        $failed = $true
        Write-Host "_GEN_EMBED STALENESS CHECK FAILED" -ForegroundColor Red
    } else {
        Write-Host "_GEN_EMBED CHECK PASSED" -ForegroundColor Green
    }
} else {
    Write-Host "_gen_embed.py not found, skipping" -ForegroundColor DarkYellow
}
Write-Host ""

# 1b. Sync SAMPLES before UE build (required for compilation fidelity)
$doSync = $SyncSamples -or $IncludeUEBuild
if ($doSync) {
    Write-Host "--- SAMPLES Sync ---" -ForegroundColor Yellow
    $samplesDir = Join-Path $root "SAMPLES\MyProject\Source\MyProject"
    if (Test-Path $samplesDir) {
        Get-ChildItem "$root\*.h", "$root\*.cpp" | ForEach-Object {
            $dest = Join-Path $samplesDir $_.Name
            Copy-Item -Path $_.FullName -Destination $dest -Force
            Write-Host "  Copied $($_.Name)"
        }
        Write-Host "SAMPLES SYNC COMPLETED" -ForegroundColor Green
    } else {
        Write-Host "SAMPLES directory not found, skipping" -ForegroundColor DarkYellow
    }
    Write-Host ""
}

# 1c. Python syntax validator
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
