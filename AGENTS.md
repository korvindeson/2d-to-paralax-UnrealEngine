# FaceParallax — Agent Guide

## Project

2D face parallax system for Unreal Engine 5. Renders a character face that responds to camera angle — switching between 10 view states (Front, 3/4 Left/Right, Profile Left/Right, Back Left/Right, Back, Top, Bottom) with smooth crossfades, multi-layer parallax offsets, and a preset system for texture/transform assignments.

## File Map

| File | Purpose |
|---|---|---|
| `FaceParallaxTypes.h` | Shared types: `EFaceAngleState`, `FFaceTextureSet`, `FFaceArtTransform`, `FFaceArtSlot`, `FFaceViewStateLayerSet` |
| `FaceParallaxComponent.h/.cpp` | Core component — state machine, parallax offsets, material parameter push, preset application |
| `FaceParallaxPreset.h/.cpp` | DataAsset — stores per-state × per-layer texture + transform assignments |
| `DepthDebugVisualizerComponent.h/.cpp` | Procedural depth mesh from depth map texture |
| `FaceParallaxPreviewActor.h/.cpp` | Preview actor with scene capture, orbit camera, part transform access |
| `FaceParallaxEditorWidget.h/.cpp` | Editor widget — 13 categories of bindable Blueprint functions for every setting |
| `Tests/ParallaxMathTests.cpp` | Standalone C++17 tests (no UE) — state determination, transforms, edge cases |
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

## Verify Your Changes

Before completing any task, run:

```powershell
# Python syntax validation (always)
python Tests\SyntaxValidator.py --path .

# C++ math tests (requires clang++/g++/MSVC)
cd Tests
clang++ -std=c++17 -o ParallaxMathTests.exe ParallaxMathTests.cpp -Werror -Wall -Wextra
.\ParallaxMathTests.exe
```

Both must pass (exit code 0).
