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

# 1. Scaffold check: the sample project stub (game module, plugin descriptor,
# build rules) is regenerated from embedded templates whenever it is missing,
# so the toolchain is self-sufficient - the stub is repo contract, not ad hoc
# files forced into SAMPLES. Regeneration only happens when files are missing;
# existing files are never overwritten.
$scaffoldProjRoot = Join-Path $root "SAMPLES\MyProject"
$scaffoldPlugin = Join-Path $scaffoldProjRoot "Plugins\FaceParallax"
$scaffoldGameSrc = Join-Path $scaffoldProjRoot "Source\MyProject"

$scaffoldTemplates = @{
    (Join-Path $scaffoldGameSrc "MyProject.Target.cs") = @'
using UnrealBuildTool;
using System.Collections.Generic;

public class MyProjectTarget : TargetRules
{
	public MyProjectTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("MyProject");
	}
}
'@
    (Join-Path $scaffoldGameSrc "MyProjectEditor.Target.cs") = @'
using UnrealBuildTool;
using System.Collections.Generic;

public class MyProjectEditorTarget : TargetRules
{
	public MyProjectEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("MyProject");
	}
}
'@
    (Join-Path $scaffoldGameSrc "MyProject.Build.cs") = @'
using UnrealBuildTool;

public class MyProject : ModuleRules
{
	public MyProject(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
	}
}
'@
    (Join-Path $scaffoldGameSrc "MyProject.cpp") = @'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultModuleImpl, MyProject, "MyProject");
'@
    (Join-Path $scaffoldPlugin "FaceParallax.uplugin") = @'
{
	"FileVersion": 3,
	"Version": 1,
	"VersionName": "1.0",
	"FriendlyName": "Face Parallax",
	"Description": "Face parallax face-layer system - runtime component, preset, preview actor and docked-tab editor.",
	"Category": "Rendering",
	"CanContainContent": false,
	"Installed": false,
	"Modules": [
		{
			"Name": "FaceParallax",
			"Type": "Runtime",
			"LoadingPhase": "PostDefault"
		},
		{
			"Name": "FaceParallaxEditor",
			"Type": "Editor",
			"LoadingPhase": "PostDefault"
		}
	],
	"Plugins": [
		{
			"Name": "EditorScriptingUtilities",
			"Enabled": true,
			"TargetAllowList": [ "Editor" ]
		},
		{
			"Name": "ProceduralMeshComponent",
			"Enabled": true
		}
	]
}
'@
    (Join-Path $scaffoldPlugin "Source\FaceParallax\FaceParallax.Build.cs") = @'
using UnrealBuildTool;

public class FaceParallax : ModuleRules
{
	public FaceParallax(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent" });
	}
}
'@
    (Join-Path $scaffoldPlugin "Source\FaceParallaxEditor\FaceParallaxEditor.Build.cs") = @'
using UnrealBuildTool;

public class FaceParallaxEditor : ModuleRules
{
	public FaceParallaxEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore",
			"UnrealEd", "Slate", "SlateCore", "UMG", "UMGEditor",
			"ToolMenus", "LevelEditor", "ContentBrowser", "ContentBrowserData", "AssetTools", "AssetRegistry",
			"EditorScriptingUtilities", "EditorSubsystem", "WorkspaceMenuStructure",
			"FaceParallax", "ProceduralMeshComponent"
		});
	}
}
'@
}

$scaffoldMissing = @()
foreach ($f in $scaffoldTemplates.Keys) {
    $needsWrite = -not (Test-Path -LiteralPath $f)
    if (-not $needsWrite) {
        $existingContent = (Get-Content -LiteralPath $f -Raw).TrimEnd("`r", "`n")
        $tplContent = ([string]$scaffoldTemplates[$f]).TrimEnd("`r", "`n")
        if ($existingContent -ne $tplContent) { $needsWrite = $true }
    }
    if ($needsWrite) { $scaffoldMissing += $f }
}
# The four plugin source dirs must exist for the sync step to copy into.
$scaffoldDirs = @(
    (Join-Path $scaffoldPlugin "Source\FaceParallax\Public"),
    (Join-Path $scaffoldPlugin "Source\FaceParallax\Private"),
    (Join-Path $scaffoldPlugin "Source\FaceParallaxEditor\Public"),
    (Join-Path $scaffoldPlugin "Source\FaceParallaxEditor\Private")
)
foreach ($d in $scaffoldDirs) {
    if (-not (Test-Path -LiteralPath $d)) {
        New-Item -ItemType Directory -Path $d -Force | Out-Null
        Write-Host "  Recreated dir $d"
    }
}
if ($scaffoldMissing.Count -gt 0) {
    Write-Host "--- SAMPLES Scaffold ---" -ForegroundColor Yellow
    Write-Host "  Regenerating missing sample-project stub:" -ForegroundColor Cyan
    foreach ($f in $scaffoldMissing) {
        $dir = Split-Path -Parent $f
        if (-not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        Set-Content -LiteralPath $f -Value $scaffoldTemplates[$f] -Encoding UTF8
        Write-Host "  Recreated $f"
    }
    # Enable the plugin in the project descriptor (idempotent).
    $upPath = Join-Path $scaffoldProjRoot "MyProject.uproject"
    if (Test-Path -LiteralPath $upPath) {
        try {
            $uproj = Get-Content -LiteralPath $upPath -Raw | ConvertFrom-Json
            $known = @($uproj.Plugins | ForEach-Object { $_.Name })
            if ($known -notcontains "FaceParallax") {
                $entry = [pscustomobject]@{ Name = "FaceParallax"; Enabled = $true }
                if (-not $uproj.Plugins) { $uproj.Plugins = @() }
                $uproj.Plugins += $entry
                $uproj | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $upPath -Encoding UTF8
                Write-Host "  Enabled FaceParallax plugin in MyProject.uproject"
            }
        } catch {
            Write-Host "[FAIL] Could not update MyProject.uproject: $_" -ForegroundColor Red
            $failed = $true
        }
    } else {
        Write-Host "[FAIL] MyProject.uproject missing at $upPath" -ForegroundColor Red
        $failed = $true
    }
    Write-Host ""
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

    $runtimePublicFiles = @("FaceParallaxTypes.h","FaceParallaxComponent.h","FaceParallaxPreset.h","FaceParallaxPreviewActor.h","DepthDebugVisualizerComponent.h","FaceParallaxSchematic.h","FaceParallaxSvgParse.h","FaceParallaxVectorArt.h")
    $runtimePrivateFiles = @("FaceParallaxComponent.cpp","FaceParallaxPreset.cpp","FaceParallaxPreviewActor.cpp","DepthDebugVisualizerComponent.cpp","FaceParallaxModule.cpp","FaceParallaxVectorArt.cpp")
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
        Write-Host "[FAIL] Plugin source dirs missing in SAMPLES - project scaffolding is incomplete:" -ForegroundColor Red
        Write-Host "       $runtimePub" -ForegroundColor Red
        Write-Host "       $editorPrv" -ForegroundColor Red
        Write-Host "       Recreate the project stub (Source\MyProject\*.cs, Plugins\FaceParallax\*.uplugin + Build.cs)" -ForegroundColor Red
        $failed = $true
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

# 1c. Silhouette geometry validator (E11: hair-ribbon separation, ahoge at
# every yaw, canthus preservation through the foreshortened eye cards)
if (-not $NoPython) {
    Write-Host "--- Silhouette Validator ---" -ForegroundColor Yellow
    try {
        $silOut = python "$testDir\validator_silhouette.py" --path "$root" 2>&1
        $silExit = $LASTEXITCODE
        Write-Host $silOut
        if ($silExit -ne 0) {
            $failed = $true
            Write-Host "SILHOUETTE VALIDATION FAILED" -ForegroundColor Red
        } else {
            Write-Host "SILHOUETTE VALIDATION PASSED" -ForegroundColor Green
        }
    } catch {
        Write-Host "Python not available, skipping silhouette validation" -ForegroundColor DarkYellow
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

# Harden cleanup: purge regenerated test artifacts on every exit path.
foreach ($artifact in @("ParallaxMathTests.exe", "compile.log", "compile_errors.txt")) {
    $path = Join-Path $testDir $artifact
    if (Test-Path $path) { Remove-Item -Path $path -Force }
}

if ($failed) {
    Write-Host "SOME TESTS FAILED" -ForegroundColor Red
    exit 1
} else {
    Write-Host "ALL TESTS PASSED" -ForegroundColor Green
    exit 0
}
