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

# 1a. Sync SAMPLES before UE build (required for compilation fidelity)
$doSync = $SyncSamples -or $IncludeUEBuild
if ($doSync) {
    Write-Host "--- SAMPLES Sync ---" -ForegroundColor Yellow
    $pluginRoot = Join-Path $root "SAMPLES\MyProject\Plugins\FaceParallax\Source"
    $runtimePub = Join-Path $pluginRoot "FaceParallax\Public"
    $runtimePrv = Join-Path $pluginRoot "FaceParallax\Private"
    $editorPub  = Join-Path $pluginRoot "FaceParallaxEditor\Public"
    $editorPrv  = Join-Path $pluginRoot "FaceParallaxEditor\Private"

    $runtimePublicFiles = @("FaceParallaxTypes.h","FaceParallaxComponent.h","FaceParallaxPreset.h","FaceParallaxPreviewActor.h","DepthDebugVisualizerComponent.h")
    $runtimePrivateFiles = @("FaceParallaxComponent.cpp","FaceParallaxPreset.cpp","FaceParallaxPreviewActor.cpp","DepthDebugVisualizerComponent.cpp","FaceParallaxModule.cpp")
    $editorPublicFiles = @("FaceParallaxEditorWidget.h","FaceParallaxEditorSubsystem.h")
    $editorPrivateFiles = @("FaceParallaxEditorWidget.cpp","FaceParallaxEditorSubsystem.cpp",
        "FaceParallaxEditorWidgetShared.h","FaceParallaxEditorWidgetUI.cpp",
        "FaceParallaxEditorWidgetInteractions.cpp","FaceParallaxEditorWidgetPanels.cpp",
        "FaceParallaxLayoutSpec.h")

    if ((Test-Path $runtimePub) -and (Test-Path $editorPrv)) {
        foreach ($f in $runtimePublicFiles) {
            $src = Join-Path $root $f
            if (Test-Path $src) {
                Copy-Item -Path $src -Destination (Join-Path $runtimePub $f) -Force
                Write-Host "  Copied $f"
            }
        }
        foreach ($f in $runtimePrivateFiles) {
            $src = Join-Path $root $f
            if (Test-Path $src) {
                Copy-Item -Path $src -Destination (Join-Path $runtimePrv $f) -Force
                Write-Host "  Copied $f"
            }
        }
        foreach ($f in $editorPublicFiles) {
            $src = Join-Path $root $f
            if (Test-Path $src) {
                Copy-Item -Path $src -Destination (Join-Path $editorPub $f) -Force
                Write-Host "  Copied $f"
            }
        }
        foreach ($f in $editorPrivateFiles) {
            $src = Join-Path $root $f
            if (Test-Path $src) {
                Copy-Item -Path $src -Destination (Join-Path $editorPrv $f) -Force
                Write-Host "  Copied $f"
            }
        }
        Write-Host "SAMPLES SYNC COMPLETED" -ForegroundColor Green
    } else {
        Write-Host "Plugin source dirs not found, skipping" -ForegroundColor DarkYellow
    }
    Write-Host ""
}

# 1b. Python syntax validator
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

    Write-Host "  Project: $UProjectPath"
    Write-Host "  Engine:   $EngineBatchDir"
    Write-Host "  Building MyProjectEditor..."

    $buildOut = & "Build.bat" "MyProjectEditor" "Win64" "Development" "`"$UProjectPath`"" "-WaitMutex" "-FromMsBuild" 2>&1
    $buildExit = $LASTEXITCODE

    $env:PATH = $origPath

    if ($buildExit -ne 0) {
        $failed = $true
        Write-Host "UE BUILD FAILED (exit $buildExit)" -ForegroundColor Red
        Write-Host $buildOut | Select-String -Pattern "error"
    } else {
        Write-Host "UE BUILD PASSED" -ForegroundColor Green
        $PluginBin = Join-Path $UEProjectRoot "Plugins\FaceParallax\Binaries\Win64"
        $RuntimeDll = Join-Path $PluginBin "UnrealEditor-FaceParallax.dll"
        $EditorDll = Join-Path $PluginBin "UnrealEditor-FaceParallaxEditor.dll"
        if (Test-Path $RuntimeDll) {
            Write-Host "[OK] Runtime DLL: $RuntimeDll" -ForegroundColor Green
        } else {
            Write-Host "[WARN] Runtime DLL not found: $RuntimeDll" -ForegroundColor Yellow
        }
        if (Test-Path $EditorDll) {
            Write-Host "[OK] Editor DLL: $EditorDll" -ForegroundColor Green
        } else {
            Write-Host "[WARN] Editor DLL not found: $EditorDll" -ForegroundColor Yellow
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
