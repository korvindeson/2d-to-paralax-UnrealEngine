# FaceParallax — Agent Guide

## Project

2D face parallax system for Unreal Engine 5. Renders a character face that responds to camera angle — switching between 10 view states (Front, 3/4 Left/Right, Profile Left/Right, Back Left/Right, Back, Top, Bottom) with smooth crossfades, multi-layer parallax offsets, and a preset system for texture/transform assignments.

## File Map

| File | Purpose |
|---|---|
| `FaceParallaxTypes.h` | Shared types: `EFaceAngleState`, `FFaceTextureSet`, `FFaceArtTransform`, `FFaceArtSlot`, `FFaceViewStateLayerSet`, `FFaceJiggleSettings`, `FFaceNestedArt`, `FFaceParamBinding`, `FFaceProfile3D`, `FFacePin3D`, `FFaceAppliedTextures` |
| `FaceParallaxComponent.h/.cpp` | Core component — state machine, parallax offsets, material parameter push, preset application, jiggle physics, idle animation, nested art transforms, 3D pin projection, face profile detection. 145 BP-accessible functions. |
| `FaceParallaxPreset.h/.cpp` | DataAsset — stores per-state × per-layer texture + transform assignments. Includes batch operations and Pin3D accessors. |
| `DepthDebugVisualizerComponent.h/.cpp` | Procedural depth mesh from depth map texture |
| `FaceParallaxPreviewActor.h/.cpp` | Preview actor with scene capture, orbit camera, part transform access |
| `FaceParallaxEditorWidget.h/.cpp` | Editor widget — 221 UFUNCTIONs across 17 categories. Slate `RebuildWidget()` with inline lambdas (no string-dispatch). |
| `FaceParallaxDataModel.h/.cpp` | Shared data model — extracts preset mutation state from the widget for use by other systems. 90+ UFUNCTIONs. |
| `FaceParallaxEditorSubsystem.h/.cpp` | Editor subsystem — toolbar registration, asset creation (material, MI, preset, BP), deploy pipeline. `UEditorSubsystem` subclass. |
| `Tests/ParallaxMathTests.cpp` | Standalone C++17 tests (no UE dep) — state machine, transforms, blink/expression/viseme, swoosh, parameters, nested art + jiggle, 3D pin projection, batch ops, zone multipliers, FName exp/viseme resolution, GetBoundaryOrDefault (774 tests) |
| `Tests/SyntaxValidator.py` | Python syntax validator — brace/macro balance, include guards |
| `Tests/run_tests.ps1` | Master test runner — syntax validator + C++ math tests + optional UE build test |
| `Tests/ue_build_test.ps1` | UE build test — compiles SAMPLES project with Build.bat, verifies DLL |
| `SAMPLES/build_and_test.bat` | Batch-file UE build runner for SAMPLES project |
| `SAMPLES/MyProject/` | Standalone UE5 project copy for CI/offline compilation |
| `deploy.py` | Deploy script — writes C++ sources, compiles via Live Coding, creates assets |
| `_gen_embed.py` | Re-encodes `.h`/`.cpp` files into `deploy.py`'s `EMBEDDED_SOURCES` |

## Rules

1. **`EFaceAngleState` lives in `FaceParallaxTypes.h`** — always include that header, never redeclare.
2. **Module API macro** is `FACEPARALLAX_API`.
3. **Never break the Python syntax validator.** All `.h`/`.cpp` files must parse cleanly.
4. **Never break the C++ math tests.** The logic in the component must match the test expectations.
5. **UE5.8 compiler is available at `H:\unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat`** — wired into test runner via PATH. Always run `run_tests.ps1 -IncludeUEBuild` before completing a task to verify.
6. **Keep README.md in sync** with any API changes.
7. **Do not create new files unless necessary** — prefer editing existing ones.
8. **`FrameDyaw`/`FrameDpitch` are set before `PreviousFrameYaw`/`PreviousFramePitch` in TickComponent** — jiggle impulse reads frame delta, not stale previous values. Never reorder the delta-save before the overwrite.
9. **Nested material init runs independently of `bUseMaterialDrivenDepth`** — nested elements must always be discoverable.
10. **After editing C++ files, run `python _gen_embed.py`** to re-encode sources into `deploy.py`'s `EMBEDDED_SOURCES`. Keeps deployer self-contained.
11. **Widget naming in Slate code uses direct member pointers** (`SliderOrbitYaw`, `PreviewImageWidget`) — no string-based dispatch. Button/slider/checkbox lambdas call UFUNCTIONs directly.
12. **After a full UBT build** (not LiveCoding), clean `Intermediate/` in the SAMPLES project before next build to avoid stale build graph.
13. **`GetBoundaryOrDefault` helper** is a `static` function on `UFaceParallaxComponent` — used by `DetermineStateFromAngles` and `GetZoneCenterYaw` to read from `ZoneBoundaryMultipliers` array with fallback to defaults `{1,3,5,7}`.

## Known Test Gaps

| Gap | Impact |
|---|---|
| **Slate `RebuildWidget()` UI** — compiles but never rendered/clicked in editor | Button/slider UFUNCTION bindings, preview image, layout are untested at runtime |
| **`deploy.py` asset creation** — Python logic never run by tests | Material/instance/BP/preset creation, module registration, toolbar registration may regress |
| **Runtime `TickComponent`** — requires UE session | Crossfades, parallax offsets, material param pushes, jiggle physics, idle animation untested |
| **`SpawnLayerQuads`/`RemoveSpawnedQuads`/auto-spawn** — compiles, headless deploy invokes the spawn path once, but quad placement/attach/material resolution verified only via log asserts | Editor-visible quad positioning, nested quad stacking, BeginPlay auto-spawn untested at runtime |
| **Crossfade animation timings** — no timing assertions | Fade curve precision, sync, or clipping could regress |
| **`FaceParallaxEditorSubsystem` asset creation** — C++ compiled but never called in test | Master material, MI, preset, BP creation, toolbar registration path untested at runtime |
| **`UFaceParallaxDataModel`** — compiles but never instantiated in test | Preset mutation, delegate broadcast, async load enqueue untested |

## Verify Your Changes

Before completing any task, run:

```powershell
.\Tests\run_tests.ps1 -IncludeUEBuild
```
