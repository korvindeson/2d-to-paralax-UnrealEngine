# FaceParallax — Agent Guide

## Project

2D face parallax system for Unreal Engine 5. Renders a character face that responds to camera angle — switching between 10 view states (Front, 3/4 Left/Right, Profile Left/Right, Back Left/Right, Back, Top, Bottom) with smooth crossfades, multi-layer parallax offsets, and a preset system for texture/transform assignments.

## File Map

| File | Purpose |
|---|---|---|
| `FaceParallaxTypes.h` | Shared types: `EFaceAngleState`, `FFaceTextureSet`, `FFaceArtTransform`, `FFaceArtSlot`, `FFaceViewStateLayerSet`, `FFaceJiggleSettings`, `FFaceNestedArt`, `FFaceParamBinding`, `FFaceProfile3D`, `FFacePin3D` |
| `FaceParallaxComponent.h/.cpp` | Core component — state machine, parallax offsets, material parameter push, preset application, jiggle physics, idle animation, nested art transforms, 3D pin projection, face profile detection. 145 BP-accessible functions. |
| `FaceParallaxPreset.h/.cpp` | DataAsset — stores per-state × per-layer texture + transform assignments. Now includes batch operations (BatchSetTextures, ClearAllTextures, DuplicateState, SyncLayerNestedToAllViews, etc.) and Pin3D accessors. |
| `DepthDebugVisualizerComponent.h/.cpp` | Procedural depth mesh from depth map texture |
| `FaceParallaxPreviewActor.h/.cpp` | Preview actor with scene capture, orbit camera, part transform access |
| `FaceParallaxEditorWidget.h/.cpp` | Editor widget — 17 restructured categories of bindable Blueprint functions (221 UFUNCTIONs). Includes TextureAndTransformParams (merged Material Params), batch ops, read-back accessors, DebugOverlays, Status includes former Query, 3D pin BP functions. |
| `Tests/ParallaxMathTests.cpp` | Standalone C++17 tests (no UE) — state machine, transforms, blink/expression/viseme, swoosh, parameters, nested art + jiggle, 3D pin projection, batch operations (505 tests) |
| `Tests/SyntaxValidator.py` | Python syntax validator — brace/macro balance, include guards |
| `Tests/run_tests.ps1` | Test runner — Python validator + C++ compilation/execution |

## Rules

1. **`EFaceAngleState` lives in `FaceParallaxTypes.h`** — always include that header, never redeclare.
2. **Module API macro** is `FACEPARALLAX_API`.
3. **Never break the Python syntax validator.** All `.h`/`.cpp` files must parse cleanly.
4. **Never break the C++ math tests.** The logic in the component must match the test expectations.
5. **No UE compiler available** — verify logic with the standalone tests; don't try to compile the UE project.
6. **Keep README.md in sync** with any API changes.
7. **Do not create new files unless necessary** — prefer editing existing ones.
8. **`FrameDyaw`/`FrameDpitch` are set before `PreviousFrameYaw`/`PreviousFramePitch` in TickComponent** — jiggle impulse reads frame delta, not stale previous values. Never reorder the delta-save before the overwrite.
9. **Nested material init runs independently of `bUseMaterialDrivenDepth`** — nested elements must always be discoverable.

## Verify Your Changes

Before completing any task, run:

```powershell
# Python syntax validation (always)
python Tests\SyntaxValidator.py --path .

# C++ math tests (requires clang++/g++/MSVC)
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
g++ -std=c++17 -o ParallaxMathTests.exe Tests\ParallaxMathTests.cpp -Werror -Wall -Wextra
./ParallaxMathTests.exe
```

Both must pass (exit code 0).
