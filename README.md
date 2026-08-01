# FaceParallax — 2D Face Parallax System for Unreal Engine 5

Camera-driven 2D face rendering with multi-layer parallax, depth map support, view-state transitions across 10 angles, real-time depth debug visualizer, preset asset system, and in-editor visual editor with 1044 automated tests.

---

## Overview

The system renders a 2D character face that responds to camera angle — switching between 10 view states (Front, 3/4 Left/Right, Profile Left/Right, Back Left/Right, Back, Top, Bottom) with smooth crossfades. Each state uses its own texture set (Albedo, Normal, Depth) driven by Blueprint or automated via the **Preset** system. Multi-layer parallax offsets are computed per-layer based on camera deviation and depth scale.

**Key features:**
- **Angle-driven state machine** — detects camera position relative to the head bone and selects the correct 2D view
- **Multi-layer parallax** — each art layer moves at its own rate based on depth scale
- **Vertical parallax** — Top/Bottom views produce Y-axis parallax offset
- **Continuous blending** — smooth crossfade at zone boundaries, not hard state snaps
- **Hysteresis** — prevents flickering at zone edges
- **Custom zone boundary multipliers** — adjust per-zone width for non-uniform view distribution
- **Material-driven depth** — pushes parallax offsets and blend alpha to material instances for shader-based depth effects
- **Preset system** — `UFaceParallaxPreset` DataAsset stores texture assignments per `(ViewState × LayerTag)`
- **Auto texture swap** — when a preset is active, textures swap automatically on state change (no Blueprint logic needed)
- **Async texture loading** — textures loaded via `UAssetManager::RequestAsyncLoad` with streamable delegate; no synchronous stalls
- **Texture push caching** — per-frame texture parameter sets guarded by pointer comparison; no redundant render state invalidation
- **Nested art + jiggle physics** — child art pieces with spring-damper physics, idle animation, per-view visibility, and 3D pin projection
- **Sequencer camera cache** — avoids per-frame `GetAllActorsOfClass` by caching the resolved camera actor
- **Expression system** — per-slot expression texture variants with smooth crossfade
- **Viseme system** — per-expression speech mouth shapes as frame-based flipbook animation
- **Blink animation** — multi-frame blink with random interval timing
- **Swoosh transitions** — directional blur-frame sequences triggered by camera turn speed
- **Depth Debug Visualizer** — toggleable procedural 3D mesh generated from the current depth map
- **Preview Actor** — `AFaceParallaxPreviewActor` with scene capture for in-editor 3D preview (dirty-flag optimized: capture only on orbit change)
- **Editor Widget** — `UFaceParallaxEditorWidget` with 221+ Blueprint-callable functions across 19 categories, diagnostic log overlay, auto-refresh on preset modification, and transaction-backed undo/redo

---

## Architecture

### File Map

| File | Type | Purpose |
|---|---|---|
| `FaceParallaxTypes.h` | Shared types | `EFaceAngleState`, `ECameraSource`, `ESwooshPhase`, `EExpression`, `EViseme`, `EFaceParamTarget` enums. Structs: `FFaceTextureSet`, `FFaceArtTransform`, `FFaceArtSlot`, `FFaceViewStateLayerSet`, `FFaceLayerDef`, `FFaceJiggleSettings`, `FFaceNestedArt`, `FFaceParamBinding`, `FFaceProfile3D`, `FFacePin3D`, `FFaceAppliedTextures` |
| `FaceParallaxComponent.h/.cpp` | Core component | State machine, parallax offsets, material parameter push, preset application, jiggle physics, idle animation, nested art transforms, 3D pin projection, face profile detection. 145 BP-accessible functions. |
| `FaceParallaxPreset.h/.cpp` | DataAsset | Stores per-state × per-layer texture + transform assignments. Batch operations, Pin3D accessors, swoosh art, nested elements. |
| `DepthDebugVisualizerComponent.h/.cpp` | Debug component | Procedural mesh from depth map, wireframe, color-by-depth, configurable grid resolution |
| `FaceParallaxPreviewActor.h/.cpp` | Preview actor | Skeletal mesh + scene capture + orbit controls + per-part transform access |
| `FaceParallaxEditorWidget.h` | Editor Widget header | C++ `UUserWidget` subclass providing bindable functions for every setting across 19 categories. The Blueprint-facing API surface; implementation is split across four translation units below. |
| `FaceParallaxEditorWidget.cpp` | Editor Widget core | Widget core API: targets, preset/transform/texture accessors, import, camera, blink/expression/viseme/params/swoosh/nested-art data methods, sync/override/batch ops |
| `FaceParallaxEditorWidgetUI.cpp` | Widget UI construction | `RebuildWidget()` — full Slate layout (workspace rails, preview canvas, gizmo overlay, panels) plus UI-local helper factories |
| `FaceParallaxEditorWidgetInteractions.cpp` | Widget interactions | Selection/refresh-entry, gizmo + pin math, folder import wizard, edge overlay/histogram, viseme grid, nested outliner, param table, problems panel |
| `FaceParallaxEditorWidgetPanels.cpp` | Widget panels + diagnostics | `RefreshUI()` and all `Refresh*`/`Rebuild*` panel methods, zone diagram, status matrix, cross-layer/tag/material cross-refs, outline→depth bake, snapshot, diagnostics |
| `FaceParallaxEditorWidgetShared.h` | Widget shared internals | Anonymous-namespace helpers (channel/view-state suffix parsing, `FPresetTransactionScope`, `AccentBlue`, `MakeLbl`/`MakeBtn`) + the `SFaceLayerGizmo` nested class, shared by all widget translation units |
| `FaceParallaxLayoutSpec.h` | UI design contract (Phase H) | Pure C++17 layout manifest + metrics/placement solver + P1–P15 design-principle validator over the widget tree. Self-checked in `RebuildWidget` and fully covered by `TestPhaseHUIDesign`. |
| `FaceParallaxEditorSubsystem.h/.cpp` | Editor subsystem | Registers the **Face Editor** toolbar button + **Window → Face Parallax Editor** menu entry, the `FaceParallaxOpenEditor` console command, and auto-open on editor startup (`bAutoOpenEditorOnStartup`, default ON); hosts the widget in a docked nomad tab. Asset deployment is handled entirely by `deploy.py` (repo root) |
| `FaceParallaxModule.cpp` | Module entry | `IMPLEMENT_MODULE(FDefaultModuleImpl, FaceParallax)` — required for the runtime DLL to register |
| `Tests/ParallaxMathTests.cpp` | Math tests | Standalone C++17 (no UE dep) — 1360 tests covering state machine, transforms, blink/expression/viseme, swoosh, parameters, nested art + jiggle, 3D pin projection + view-angle rotation, batch ops, zone multipliers, per-view visual hull, Phase H layout-design contract |
| `Tests/SyntaxValidator.py` | Syntax validator | Brace/macro balance, include guards — enforces clean parsing on all source files |
| `Tests/run_tests.ps1` | Test runner | Root→SAMPLES sync + Python syntax validator + C++ math tests + optional UE build test |
| `Tests/ue_build_test.ps1` | UE build test | Compiles SAMPLES project with `Build.bat`, verifies DLL output |
| `SAMPLES/MyProject/` | Standalone UE5 project copy | Used for CI/offline compilation verification |

### Component Roles

| Component | Purpose |
|---|---|
| `UFaceParallaxComponent` | Core component. Computes camera-to-head angle, manages view state machine, calculates parallax offsets per layer, drives material parameters, applies preset textures. Handles async texture loading, sequencer camera caching, texture push caching. |
| `UDepthDebugVisualizerComponent` | Optional debug tool. Reads the current depth map texture, builds a uniform-grid procedural mesh with Z = depth value, colorized by height. Texture compression validation included. |
| `UFaceParallaxPreset` | Data asset. Holds a `TMap<EFaceAngleState, FFaceViewStateLayerSet>` — one texture set per view state, with sub-keys per layer tag. |
| `AFaceParallaxPreviewActor` | Editor/runtime preview actor. Hosts the mesh, parallax component, depth debug, and a scene capture camera with orbit controls. Dirty-flag optimized: capture occurs only on orbit change. |
| `UFaceParallaxEditorWidget` | `UUserWidget` subclass with bindable Blueprint functions for every editor setting — transform sliders, view overrides, auto-fit, sync, camera, debug toggles, material params, status, nested art. Includes diagnostic log and auto-refresh on preset modification. Undo/redo via `GEditor->BeginTransaction` with `FPresetTransactionScope` RAII guard. |

### Module

The plugin lives at `SAMPLES/MyProject/Plugins/FaceParallax/` with two modules:

- **`FaceParallax`** (Runtime) — `FaceParallax.Build.cs` public deps: `Core`, `CoreUObject`, `Engine`, `InputCore`, `ProceduralMeshComponent`. Entry point: `IMPLEMENT_MODULE(FDefaultModuleImpl, FaceParallax)` in `FaceParallaxModule.cpp`.
- **`FaceParallaxEditor`** (Editor) — depends on the runtime module plus editor modules (`Slate`, `UMG`, `UMGEditor`, `UnrealEd`, `AssetTools`, `ToolMenus`, `EditorSubsystem`, `LevelEditor`, `ContentBrowser`, `DesktopPlatform`, `EditorScriptingUtilities`, `MaterialEditor`, `LevelSequence`, `CinematicCamera`, `Kismet`). Entry point: `IMPLEMENT_MODULE(FDefaultModuleImpl, FaceParallaxEditor)` in `FaceParallaxEditorSubsystem.cpp`.

Add the plugin to your `.uproject`:

```json
"Plugins": [
    { "Name": "FaceParallax", "Enabled": true }
]
```

Plugin-level dependencies must also be declared in `FaceParallax.uplugin`:

```json
"Plugins": [
    { "Name": "ProceduralMeshComponent", "Enabled": true },
    { "Name": "EditorScriptingUtilities", "Enabled": true }
]
```

---

## View State System

### State Map

```
Pitch > +60°   → Top
Pitch < -60°   → Bottom
                ┌─────────────────────────────────────────────┐
                │  -157.5  -112.5  -67.5  -22.5  22.5  67.5   │
                │ BL      LP      3QL    Front  3QR   RP   BR │
                │←── 45° zones ──────────────────────────────→│
                └─────────────────────────────────────────────┘
```

Default zone boundaries (multiplier × HalfZoneWidth = 22.5°):

| State | Yaw Range (degrees) |
|---|---|
| Front | (-22.5, 22.5] |
| ThreeQuarterRight | (22.5, 67.5] |
| RightProfile | (67.5, 112.5] |
| BackRight | (112.5, 157.5] |
| Back | (157.5, 180] ∪ (-180, -157.5] |
| BackLeft | (-157.5, -112.5] |
| LeftProfile | (-112.5, -67.5] |
| ThreeQuarterLeft | (-67.5, -22.5] |
| Top | Pitch > TopViewPitchThreshold |
| Bottom | Pitch < BottomViewPitchThreshold |

**Custom zone boundaries**: Set `ZoneBoundaryMultipliers` (TArray<float>, indices 0-3 for Front, ThreeQuarter, Profile, Back) to adjust zone widths. Default `{1,3,5,7}`.

### Transitions

- **Crossfade**: When a state change is detected, `BlendAlpha` interpolates from 0→1 at `CrossfadeSpeed` rate.
- **Continuous Blending** (optional): When enabled, `BlendAlpha` is driven by the camera's proximity to the zone boundary, creating a smooth transition that starts before the boundary is crossed.
- **Hysteresis**: A frame count threshold prevents rapid state oscillation when the camera sits exactly on a boundary.

---

## Parallax System

### Angle Calculation

The camera angle is computed using **position-based** direction (head socket → camera location), transformed into the head's local coordinate space.

```
ToCamera = (CameraLocation - HeadLocation).normalized
LocalToCamera = HeadRotation.unrotate(ToCamera)
Yaw = atan2(LocalToCamera.Y, LocalToCamera.X)
Pitch = atan2(LocalToCamera.Z, sqrt(LocalToCamera.X² + LocalToCamera.Y²))
```

### Per-Layer Offsets

Each `FFaceLayerDef` entry defines:
- `LayerTag` — primitive component tag to match
- `DepthScale` — how much this layer moves relative to the base (0.0 = no parallax, 1.0 = full)
- `DepthMapIntensity` — per-layer depth map intensity multiplier (combined with global `DepthMapIntensity`)
- `bInvertParallax` — invert the movement direction (for background layers)

For horizontal states (Front, Profile, etc.):
```
OffsetX = (YawDeviation / HalfZoneWidth) × DepthScale × MaxParallaxOffset
OffsetY = (PitchDeviation / HalfZoneWidth) × DepthScale × MaxVerticalParallaxOffset
```

For vertical states (Top, Bottom):
```
OffsetX = (YawDeviation / 180°) × DepthScale × MaxParallaxOffset   (full circle)
OffsetY = (PitchDeviation / (90° - Threshold)) × DepthScale × MaxVerticalParallaxOffset
```

### Material Parameters

When `bUseMaterialDrivenDepth` is enabled, the component sets these parameters on all dynamic material instances:

| Parameter | Type | Description |
|---|---|---|
| `StateBlendAlpha` | Scalar | 0→1 crossfade between previous and current state |
| `ParallaxOffset` | Vector4 | (OffsetX, OffsetY, 0, 0) — per-layer parallax shift |
| `DepthIntensity` | Scalar | Global depth map intensity multiplier |
| `DebugDepth` | Scalar | 0 = normal rendering, 1 = show depth as heat map |
| `IsTopDown` | Scalar | 1 when in Top or Bottom state (for shader branching) |
| `IsTopView` | Scalar | 1 when in Top state specifically |
| `ArtPosition` | Vector4 | (PosX, PosY, 0, 0) — per-art-piece UV position offset |
| `ArtScale` | Vector4 | (ScaleX, ScaleY, 0, 0) — per-art-piece UV scale |
| `ArtRotation` | Scalar | Rotation in degrees — per-art-piece UV rotation |
| `AlbedoTexturePrev` | Texture2D | Previous state albedo (for crossfade) |
| `NormalTexturePrev` | Texture2D | Previous state normal map |
| `DepthTexturePrev` | Texture2D | Previous state depth map |

### Material Parameters (Preset System)

When a preset is active, the component sets these texture parameters on each layer's material instances:

| Property | Default | Material Parameter |
|---|---|---|
| `AlbedoParamName` | `"AlbedoTexture"` | Texture2DParameter |
| `NormalParamName` | `"NormalTexture"` | Texture2DParameter |
| `DepthParamName` | `"DepthTexture"` | Texture2DParameter |

Each material used by a layer should expose these as texture parameters so the preset system can drive them automatically.

### Dual-Texture Crossfade

During view state transitions, the component keeps both texture sets alive:
- **Current state** textures on `AlbedoTexture` / `NormalTexture` / `DepthTexture`
- **Previous state** textures on `AlbedoTexturePrev` / `NormalTexturePrev` / `DepthTexturePrev`

The `StateBlendAlpha` parameter ramps from 0→1. Your material should use it as a lerp factor:
```
Albedo = lerp(AlbedoTexturePrev, AlbedoTexture, StateBlendAlpha)
```

### Texture Push Caching

Per-frame `SetTextureParameterValue` calls are guarded by pointer comparison against `LastAppliedTextures` (a `TMap<FName, TObjectPtr<UTexture2D>>`). A texture parameter is only updated when the pointer actually changes, avoiding redundant GPU render state invalidation. Same optimization applies to nested art textures via `LastAppliedNestedTextures`.

### Async Texture Loading

`AsyncLoadSlotTextures` calls `UAssetManager::GetStreamableManager().RequestAsyncLoad()` with `FStreamableDelegate` bound to `OnAsyncTexturesLoaded`. Active handles are tracked in `ActiveTextureLoads`; existing handles are cancelled when new loads are initiated. Textures are resolved from soft object pointers and written into the slot's hard refs and cache. Only the `CurrentState` textures are resolved on load (not all 10 states). A `LoadGeneration` counter prevents stale callbacks from overwriting newer loads. No `LoadSynchronous` calls remain.

### Sequencer Camera Cache

`GetCameraLocationAndRotation` reads from `SequencerCameraCache` (updated by `RefreshSequencerCamera()` on `BeginPlay` and whenever `SetCameraSource`/`SetCustomCameraActor` is called) instead of calling `GetAllActorsOfClass` every frame. Cache invalidated via `bSequencerCacheValid` flag.

---

## Preset System

The `UFaceParallaxPreset` DataAsset stores all texture assignments for a character face:

```
Preset → {
    Front  → {
        "Eyes"  → { Albedo: T_Front_Eyes_A,  Normal: T_Front_Eyes_N,  Depth: T_Front_Eyes_D },
        "Hair"  → { Albedo: T_Front_Hair_A,  Normal: T_Front_Hair_N,  Depth: T_Front_Hair_D },
    },
    RightProfile → {
        "Eyes"  → { Albedo: T_Profile_Eyes_A, Normal: T_Profile_Eyes_N, Depth: T_Profile_Eyes_D },
    },
    ...
}
```

### Preset API

| Method | Description |
|---|---|
| `GetSlot(State, LayerTag)` | Returns the full `FFaceArtSlot`. Logs warning on fallback to default. |
| `SetSlot(State, LayerTag, Slot)` | Assigns an entire slot |
| `GetTexturesForSlot(State, LayerTag)` | Returns the `FFaceTextureSet` |
| `SetTexturesForSlot(State, LayerTag, Textures)` | Assigns textures to a slot (auto-fits if enabled) |
| `GetEffectiveTransform(State, LayerTag)` | Combined (canonical + view-override) transform |
| `SetCanonicalTransform(State, LayerTag, Transform)` | Sets canonical transform |
| `SetViewOverride(State, LayerTag, OverrideView, Transform)` | Adds a view-specific transform override |
| `HasViewOverride` / `ClearViewOverride` | Override management |
| `ClearAllOverridesForSlot` / `ClearAllOverrides` | Bulk override removal |
| `ComputeAutoFitTransform(Textures)` | Computes uniform scale to fit texture to `CanvasSize` |
| `ApplyAutoFitToSlot(State, LayerTag)` | Auto-fits a slot's canonical transform |
| `SyncCanonicalToAllViews(State, LayerTag)` | Copies canonical transform to all views |
| `SyncTexturesToAllViews(State, LayerTag)` | Copies slot textures (incl. alt textures) to all views |
| `HasState` / `HasSlot` / `GetAssignedStates` | Query methods |
| `ClearState` / `ClearAll` | Removal methods |
| `GetAllLayerTags(State)` | List all layer tags for a state |
| `GetNestedElement` / `SetNestedElement` / `AddNestedElement` / `RemoveNestedElement` | Nested art access |
| `PinRotationFromYawDev(YawDev, HalfZoneWidth, MinRot, MaxRot, Sens)` | Static view-angle→rotation mapping (wrap, normalize, lerp, sensitivity) — unit-mirrored |
| `PinRotationFromViewAngles(YawDev, PitchDev, HalfZoneWidth, MinRot, MaxRot, Sens)` | Phase 5 pitch-aware variant — byte-identical to the yaw-only mapping at `PitchDev = 0`, eases to center rotation at the pitch zone edge (unit-mirrored by `TestPrimaryLayerPin`) |
| `PinScaleFromView(YawDev, PitchDev, MinScale)` | Phase 5 view-angle scale: `Lerp(1, MinScale, 1 − \|Cos(Yaw)·Cos(Pitch)\|)` (unit-mirrored) |
| `ProjectPinToUV(Pin3D, ViewState)` / `ProjectPinToUVForState(Pin3D, ViewState)` | Project a 3D pin to UV using the zone-center camera angle of a view state |
| `GetNestedPin3D` / `SetNestedPin3D` | Pin projection access |
| `GetSwooshArt` / `SetSwooshArt` / `HasSwooshArt` / `ClearSwooshArt` | Swoosh art access |
| `GetParamBindings` / `SetParamBindings` | Parameter binding access |
| `GetAltTextures` / `SetAltTextures` | Alt texture set access |
| `BatchSetTextures` / `BatchSetTexturesAllLayers` | Batch texture assignment |
| `DuplicateState` / `ClearAllTextures` / `SyncLayerNestedToAllViews(Layer, Elem, El, bSyncPins=true)` | Batch operations; `bSyncPins=false` propagates art/jiggle/pivot but keeps each view's own pin (new elements get a fresh unpinned pin) |
| `SetNestedAltTextures` | Alt textures for nested elements |

---

## Expression & Viseme Systems

### Expression System

Each `FFaceArtSlot` stores expression-specific texture variants in `TMap<EExpression, FFaceTextureSet> ExpressionTextures`. Changing the expression triggers a smooth crossfade controlled by `ExpressionBlendAlpha`.

**Supported expressions:** Neutral, Smile, Frown.

**Properties:** `CurrentExpression`, `ExpressionCrossfadeDuration` (0.3s), expression param names.

### Viseme System (Speech Mouth Shapes)

Each slot stores per-expression, per-viseme animation frame sequences (flipbook). **11 visemes:** Uh, Ah, Ee, D, S, F, M, L, WOO, Oh, R.

**Properties:** `bVisemeEnabled`, `VisemeFrameDuration` (0.04s).

### Blink Animation

Each slot stores `TArray<FFaceTextureSet> BlinkFrames`. Multi-frame blink at random intervals.

**Properties:** `bBlinkingEnabled`, `BlinkIntervalMin` (3.0s)/Max (7.0s), `BlinkFrameDuration` (0.03s).

---

## Nested Art & Jiggle System

Attach child art pieces to existing layers with independent transforms, spring-damper jiggle physics driven by camera angular velocity, idle animation flipbook, per-view visibility, and 3D pin projection.

**Primitive Tag Convention:** Nested primitives tagged `LayerTag_ElementName` (e.g., `HairLayer_Wig`). Precomputed FName state key cache avoids per-frame string formatting — cache built via `BuildNestedArtCache()` on dirty flag.

**Key concepts:** Relative transform, pivot point (normalized UV), jiggle (Stiffness/Damping/ImpulseScale/JiggleAxis), idle animation (looping flipbook), per-view visibility, static nesting (arbitrary depth, jiggle elements are leaf nodes).

### 3D Pins & View-Angle Rotation

`FFacePin3D` holds the attachment point and optional rotation behavior:

| Field | Meaning |
|---|---|
| `bPinned` | Enables 3D pin projection: the element's pivot is the projected UV of `Position3D` (face-local `-1..1` per axis) at the live camera angle, so the element stays attached to its 3D point as the head turns |
| `Position3D` | Face-local pin position (X left/right, Y up/down, Z toward the nose) |
| `bEnableViewAngleRotation` | Enables rotation + scale driven by view angle |
| `MinRotation` / `MaxRotation` | Rotation sweep in degrees (defaults ±30) |
| `RotationSensitivity` | Multiplier on the mapped rotation |
| `MinScale` | View-angle scale floor (default 0.5): at the zone center scale = 1; at 90° deviation it eases to `MinScale` |

**Rotation math:** `PinRotationFromYawDev` (static, unit-tested) wraps the yaw deviation from the rendered state's zone center to `[-180,180]`, normalizes it by `HalfZoneWidth` (clamped to `[-1,1]`), lerps `MinRotation → MaxRotation` across that range, and multiplies by `RotationSensitivity`. The result is added to the element's rotation in `ComputeNestedEffectiveTransform` — which rotates *around the pin*, because a pinned element's effective pivot is the projected pin UV. Example: symmetric `Min=-30 / Max=30` gives 0° at the view center and ±30° at the zone edge. Symmetric min/max make the eyebrow stay level at the front view and tilt as the head turns toward the profile.

**Pitch-aware variant (Phase 5):** `PinRotationFromViewAngles(YawDev, PitchDev, ...)` drives the same mapping with the driver `NormYaw × (1 − |NormPitch|)` — pitch deviation eases the rotation to the center value at the pitch zone edge, and at `PitchDev = 0` the result is **byte-identical** to `PinRotationFromYawDev` (regression-mirrored by `TestPrimaryLayerPin`). `PinScaleFromView(YawDev, PitchDev, MinScale)` computes `Lerp(1, MinScale, 1 − |Cos(Yaw)·Cos(Pitch)|)`; both are applied to nested pins and to the whole layer via `FFaceArtSlot::LayerPin3D` (a pin on the layer slot itself: the primary art follows a projected 3D point, with optional view-angle rotation/scale).

**Authoring:** `SetNestedPinFromUV` converts a click position into `Position3D` (Back-view clicks mirror the X axis so round-trips stay consistent); `ProjectPinToUVForState`/`ProjectPinToUV` project using the state's zone-center camera angle.

**Editor notes:**
- The pin section in the editor widget edits the **selected** top-level element (stepper `</>` or click a row in the Nested Elements outliner to select; the selected row is highlighted). Children are listed read-only.
- **Pin gizmo:** when the selected element is pinned, the preview-canvas gizmo switches to pin mode — a yellow handle appears at the pin's projected UV in the active view; drag it to move the pin (writes through the same `SetNestedPinFromUV` authoring path as clicking). Deselect/select an unpinned element to return to layer-transform editing.
- Controls are disabled when the active state/layer has no elements. Slider normalization guards zero/inverted ranges (`PinSliderNorm`, mirrored in tests), so `MinRotation == MaxRotation` never produces a NaN slider position.
- Design semantics: at the zone center the rotation equals the **midpoint** of `MinRotation`/`MaxRotation` — asymmetric ranges shift the rest pose (e.g. `Min=0 / Max=60` rotates the element 30° at the front view). `RotationSensitivity > 1` overshoots past `Min`/`Max` (no clamp, by design); negative sensitivity inverts the direction. The view-angle rotation is **added on top of** the parent + relative rotation composition in `ComputeNestedEffectiveTransform` (regression-mirrored in tests).

---

## Zone Boundary Multipliers

The `ZoneBoundaryMultipliers` property (`TArray<float>`) on `UFaceParallaxComponent` lets you customize the width of each view zone relative to `HalfZoneWidth`:

| Index | Zone | Default Multiplier | Boundary at HW=22.5 |
|---|---|---|---|
| 0 | Front | 1.0 | 22.5° |
| 1 | ThreeQuarter | 3.0 | 67.5° |
| 2 | Profile | 5.0 | 112.5° |
| 3 | Back | 7.0 | 157.5° |

Set `ZoneBoundaryMultipliers = {2.0, 4.0, 6.0, 8.0}` for wider front and 3/4 zones. Values use `GetBoundaryOrDefault()` static helper with fallback to defaults.

---

## Depth Debug Visualizer

The `UDepthDebugVisualizerComponent` creates a procedural 3D mesh from the current depth map. Grid mesh with Z = depth value × `HeightScale`, vertex-colored blue→red gradient.

**Texture compression validation:** `SampleDepthMap()` validates `Texture->CompressionSettings` and warns if not `TC_EditorIcon`, `TC_VectorDisplacementmap`, or `TC_HDR`.

**Controls:** GridResolution (8-256), MeshSize, HeightScale, LocalOffset, bShowWireframe, bUseVertexColors, LowColor/HighColor.

---

## Outline → Depth (Silhouette Extraction)

The edges of the rotation views drive depth-map generation. `UFaceParallaxComponent` scans the assigned albedo texture of each `OutlineViewStates` entry (alpha-aware, falling back to luma/color thresholds) and produces per-scanline silhouette point pairs `(xMin, y), (xMax, y)` normalized to `[-1, 1]`, cached in `OutlinePointCache`.

**Visual hull:** `VisualHullDepthStatic` combines the view silhouettes — Front defines the 2D shape, Right/LeftProfile constrain depth per height, Top/Bottom constrain depth per width — so every rotation-view edge contributes depth info. `SilhouetteDistanceToEdgeStatic` returns signed distance to the nearest edge (interpolated between scanlines), positive inside, negative outside.

**View-consistent variant:** the yaw/pitch overload of `VisualHullDepthStatic` evaluates the hull in the *target view's* camera frame: it binary-searches the max depth along the view ray whose front-space point stays inside every silhouette prism, then takes the min with a dome falloff measured against the front silhouette foreshortened into that view (skipped when the projection is degenerate — profile yaw or top/bottom pitch). At yaw 0 / pitch 0 it reduces exactly to the front-view hull. Back views (yaw > 90°) mirror the projected rows to keep the (xMin, xMax) convention; top/bottom views bound depth by the front silhouette's vertical extent. The art-camera angle per state is the default zone-center table (`VisualHullYawForState` / `VisualHullPitchForState`).

**Component API:**
| Method | Description |
|---|---|
| `ExtractSilhouettePoints(State, MaxPoints)` | Scans the state's albedo and returns (xMin, xMax) pairs per scanline |
| `SilhouetteDistanceToEdge(State, LocalPoint)` | Signed distance to the nearest silhouette edge for a state |
| `GenerateDepthBufferFromOutlines(GridSize, OutDepth, OutCellSize)` | Builds a visual-hull depth buffer over the grid (front view) |
| `GenerateDepthBufferFromOutlinesForView(GridSize, YawDegrees, PitchDegrees, OutDepth, OutCellSize)` | Builds the hull buffer in an arbitrary view's camera frame (editor tooling) |
| `SilhouetteDistanceToEdgeStatic(Points, Point)` | Pure static signed-distance math (unit-tested) |
| `VisualHullDepthStatic(Front, Right, Left, Top, Bottom, Point)` | Pure static visual-hull depth (unit-tested) |
| `VisualHullDepthStatic(Front, Right, Left, Top, Bottom, Point, YawDegrees, PitchDegrees)` | Pure static per-view visual-hull depth (unit-tested) |
| `VisualHullYawForState(State)` / `VisualHullPitchForState(State)` | Art-camera yaw/pitch per view state (default zone-center table) |
| `ClearOutlinePointCache()` | Drops the cached scan results |
| `SetOutlineViewState(State)` / `SetOutlineViewEnabled(State, bEnabled)` | Adds a state to / sets membership in `OutlineViewStates` |
| `ClearOutlineViewStates()` / `IsOutlineViewState(State)` / `GetOutlineViewStates()` | Manage and query the outline-view set |

`DetectFaceProfileFromPreset` also uses the true silhouette bounds (not just canvas bounding boxes) to refine `FFaceProfile3D` dimensions.

---

## Preview Actor

The `AFaceParallaxPreviewActor` provides orbit-controlled preview with dirty-flag optimization:

- `bOrbitDirty` flag — `UpdateCaptureTransform()` only runs when yaw/pitch/distance/FOV changed
- `bCaptureDirty` flag — `SceneCapture->CaptureScene()` only called after transform updates
- `bCaptureEveryFrame = false`, `bCaptureOnMovement = false` — zero GPU cost when idle
- `CreateRenderTarget()` / `GetRenderTarget()` / `RequestCapture()` — the editor widget provisions a 512×512 scene-capture target when a preview actor is selected and shows it in the canvas; without an actor the canvas falls back to the selected layer's albedo so it is never blank

**Camera Controls:** SetOrbitYaw, SetOrbitPitch, SetOrbitDistance, SetPreviewFOV, ResetCamera, SetAutoRotate.

---

## Editor Widget (UFaceParallaxEditorWidget)

The `UUserWidget` subclass constructs its entire UI via `RebuildWidget()` — no UMG Designer needed. Implementation is split into four translation units (`FaceParallaxEditorWidget.cpp` core API, `FaceParallaxEditorWidgetUI.cpp` construction, `FaceParallaxEditorWidgetInteractions.cpp`, `FaceParallaxEditorWidgetPanels.cpp`) sharing internal helpers through `FaceParallaxEditorWidgetShared.h`; the header is the single Blueprint-facing surface.

**Key improvements:**
- **Diagnostic log overlay** — `SMultiLineEditableTextBox` at bottom of widget; `RunDiagnostics()` reports preset status, missing states/layers
- **Auto-refresh** — bound to `FCoreUObjectDelegates::OnObjectModified`; triggers `RefreshUI()` when ActivePreset modified externally
- **Undo/redo** — `FPresetTransactionScope` RAII guard wraps 13 preset-mutating functions with `GEditor->BeginTransaction`/`EndTransaction` and `Modify()`
- **3-pane workspace (Phase A)** — left icon rail (Layers | Transform | Camera | Debug | Advanced) + `SWidgetSwitcher` rail panels, center preview canvas (display-mode row + overlay stack), right selected-slot properties pane. Old T0–T3 tabs were re-homed into the rails (Quick Actions/Sync/Alignment→Transform; Camera Follow/zones/blend→Camera; import/config/outline→depth→Debug; cross-layer/params/nested→Advanced)
- **View strip with status dots (Phase A)** — per-state tabs + colored dots (green complete / amber missing albedo / orange per-view overrides) via `GetStateDotColor`, plus a "v" context menu per state: Sync layer to all, Sync textures to all, Clear overrides, Fill Missing Views, Duplicate-from list
- **Sync drift indicator (Phase C)** — `RefreshSyncDriftIndicator` compares the active state's canonical transform against the other 9; shows "Synced" or "Drifted: n/9"
- **Folder import wizard (Phase C)** — `OpenImportFolderWizard` opens a modal: pick a folder, Scan (`FindFilesRecursive` for png/jpg/jpeg/tga), part cards parsed from `{Part}_{View}_{Map}` names via `MatchStateSuffix`/`StripChannelSuffix` (full names before short codes), per-view×channel coverage preview with green/miss dots, Apply assigns to the active layer then closes
- **Display modes (Phase D)** — `SetDisplayMode` maps Textured/Depth/Wireframe/Split to the preview toggles; the mode row sits above the canvas
- **Depth Debug knobs (Phase D)** — live sliders for the visualizer's `GridResolution`, `MeshSize`, `HeightScale`, `LocalOffset.Z` plus hex Low/High color edits and a Rebuild button that re-feeds the active slot's depth texture
- **Edge overlay + histogram (Phase D)** — `BuildEdgeOverlay` downsamples the active albedo, runs a Sobel edge pass (`EdgeDensity` static), and renders green edges over the preview; a 16-bin luminance histogram (`BuildLumaHistogram` static) draws as bars with density/mean stats
- **Hull Review (Phase D)** — orbit-3D auto-rotate/speed/snap controls plus a 2×5 thumbnail grid of every state's albedo for the active layer (click to jump)
- **Viseme Frames grid (Phase E)** — `RebuildVisemeGrid` renders the active layer's viseme timeline: one row per viseme (plus named visemes) with one cell per frame, filled cells = assigned art; clicking a filled cell plays that viseme; each row shows its fill percentage and the currently playing viseme is highlighted
- **Nested-art outliner (Phase E)** — `RebuildNestedOutliner` lists the active layer's nested elements with per-view visibility checkboxes, [Pin]/[Jiggle] badges, a Del button, and indented child rows; edits write back through `SetNestedElement`; the selected row is highlighted
- **Pin controls (advanced)** — element `<` `>` stepper (no more hardcoded element 0), live Pinned checkbox, Pin X/Y/Z sliders with numeric readouts, plus view-angle rotation controls: "Rotate w/ view angle" checkbox and Min Rot / Max Rot / Sens sliders; all refreshed by `RefreshPinControls()`
- **Param bindings table (Phase E)** — `RebuildParamTable` shows every binding of the active state/layer: editable param name, target cycle button (PosX→PosY→SclX→SclY→Rot→Blend), Invert checkbox, and X remove; the Add row above appends new bindings via `SetParamBindings`
- **Problems panel (Phase F)** — `RebuildProblemsPanel` validates all 10 states × every layer for missing albedo/normal/depth and warns on blink-frame and viseme-frame count mismatches across layers; issues are deduplicated (sorted-unique), color-coded (red error / amber warning), and clicking a row jumps to that state
- **Problems panel upgrades (Phase 4)** — the panel now has a quick-actions bar (rail jump chips + Import + Clear Stale), a Layout Group section that live-runs the manifest's `ValidateDesign(BuildSpec())` and lists any P1–P16 violations (green "Design contract OK" when clean), a rail-width control (`RailWidthPx`, 180–360 px via `FPLayout::ClampRailWidth`, default 180 — rebuilds the layout), an issue search box that filters rows live with a match count in the summary, and a summary line on the Problems accordion header (`SFaceAccordion::SetSectionSummary`)
- **Show Pins (Phase 3)** — the canvas mode row's "Pins" checkbox paints every nested-element marker on the gizmo color-coded: amber = static pin, cyan = rotation pin, purple = jiggle element, red ring = plain pivot anchor (unpinned); the selected pin handle stays amber as before
- **Sync drift indicator (Phase C)** — `RefreshSyncDriftIndicator` compares the active state's canonical transform against the other 9; shows "Synced" or "Drifted: n/9" (exact-equality semantics mirrored by `FPPinDriftCount`/`TestPinDriftMirror`); now wired into the Transform rail's Quick Actions section and refreshed from `RefreshUI()`
- **Alignment tools (Phase B)** — canvas gizmo (`SFaceLayerGizmo`, drag body=move / corner=scale / top handle=rotate), onion-skin ghost of the adjacent state with opacity slider, and Cross-View Transform copy-from combo + Link checkbox that broadcasts edits to all states (`ApplyCanonicalTransformWithLink`)
- **Thumbnail status matrix** — 10-state × N-layer grid of aspect-correct albedo thumbnails (pixel size + click-to-jump tooltip) replacing the old 22×20 ✓/× grid
- **Camera follows view** — view-strip clicks and matrix jumps snap orbit yaw/pitch to the state's zone center when `bCameraFollowsView` is enabled (default)
- **View override mode** — transform edits write `ViewOverrides` (per-rendered-view deltas) instead of canonical transforms when `bViewOverrideMode` is on
- **Silhouette → depth** — "Generate Depth from Outlines" extracts silhouette edges from rotation-view albedo textures, builds a visual-hull depth buffer, and bakes it into every layer's depth channel
- **Batch import by channel suffix** — imports auto-assign by filename suffix (`_N`/`_Normal`/`_normalmap` → Normal, `_D`/`_Depth`/`_height`/`_displacement` → Depth, else Albedo); "Import & Assign" and toolbar "Import Art..." both use it when a layer is selected
- **Quick Actions** — Auto-Fit All, Sync All→All, Clear All Overrides, Duplicate Front→This, Fill Missing Views (copies the active slot's art to views that have no albedo yet)
- **Outline view management** — per-state checkboxes bound to the component's `OutlineViewStates` (All/None shortcuts); depth grid resolution is user-editable (8–256)
- **Outline → depth with scope + confirm** — "Generate Depth from Outlines" is arm/confirm-guarded (first click arms, second click commits; Detect Profile runs the combined flow unarmed) and offers a bake scope (Front only / 8 h-states / all 10). Each target view receives its own per-view visual-hull map computed in that view's camera frame instead of a verbatim copy of the front map, and the bake only touches the depth channel of the target states' layers
- **Toolbar search** — the filter box moved to the toolbar so it applies across all 6 property panels (right pane + 5 rails)
- **Rail accessibility (Phase 4b)** — the big-scroll remediation follow-up, all mirrored by `TestAccessibilityMirrors` (`FPLayout::RailSectionTitles` / `FindRailSectionByTitle` / `ConfigSummary` / `VisemeSummary` / `RailWidthAfterDrag` / `QuickActionLabels`):
  - **Progressive disclosure below section level** — the Debug rail's 8-checkbox Config section and the Viseme Frames grid each collapse into a one-line summary ("K of 8 on" / "N viseme rows") behind a clickable `SFaceDisclosure` header; summaries update live from `RefreshConfigCheckboxes` / `RebuildVisemeGrid`
  - **Persistent quick-actions bar** — Import Art…, Sync All→All, Auto-Fit All, Clear All Overrides sit in a pinned strip above the rail switcher, visible on every rail tab (button set mirrors `FPLayout::QuickActionLabels()`)
  - **Cross-rail search jump** — pressing Enter in the toolbar search runs `OnRailSearchCommitted`, finds the first section title containing the query across all 5 rails, switches rail, expands the accordion section, and scrolls it into view (`SScrollBox::ScrollDescendantIntoView`); a "no match" status message is shown otherwise
  - **Drag-resize rail** — `SFaceRailResizer` handle between the rail column and canvas: drag updates `RailWidthPx` live (clamped to `FPLayout::ClampRailWidth` 180–360 via `SetRailWidthLive`, no rebuild during the drag; commit rebuilds so the Debug rail-width spinbox stays in sync)
  - **Section-jump chips + scroll indicator** — every rail has a pinned chip row above its scroll viewport (`BuildRailSectionChips`): one chip per registered section (`RegisterRailSection` / `RegisterAccordionSections`), click to jump (cross-rail jumps rebuild + auto-expand + scroll), the last-jumped chip highlights; the registry is the single source of truth for chips and search
- **Live numeric camera readouts** — Yaw/Pitch/Dist values shown next to the sliders and updated on refresh
- **Spatial part picking (Phase 1/2/4)** — the preview canvas carries a hotspot overlay (`SFaceHotspotLayer`) with 13 anatomical UV regions (brow/eye/nose/cheek/mouth/teeth/chin/ear/neck) plus a matching parts-strip of chips under the canvas:
  - **Select vs import are separate actions** — a plain click resolves the region to a layer (`ResolveHotspotLayer`: the preset's persisted `HotspotLayerMap` first, then `FPLayout::FPHotspotLayerMatch` exact → plural → L/R-collapse → prefix derivation, so the default Eyes/Brows/Mouth/Hair layer set resolves EyeL→Eyes, BrowR→Brows, Mouth→Mouth out of the box) and selects it, jumping to the Transform rail with tweak controls open; Alt+click (or an unmapped region) routes to the Import Folder Wizard preselected on that part instead; right-click on a chip opens the in-tool region→layer remap menu (persisted in the preset, "Auto (derived)" resets)
  - **Per-view bounds from stored transforms** — `RefreshHotspotRegions` transforms each mapped region's outline by its layer's effective transform for the active view state (`FPHotspotTransformRegion` mirrors the master material's UV chain: pivot subtract → art position → scale → pivot add → rotate), so outlines hug the art's real position/scale on Profile/Back/¾/Top/Bottom instead of a fixed front template; unmapped regions keep the template pose
  - **Cycle Preview (Phase 2)** — 8-second tour sequencing the live systems one at a time: blink 2s → expression 2s → viseme 2s → orbit sweep 2s
  - **Live Preview (Phase 4b)** — the assembled-result check: blink + Smile expression + re-triggered viseme (2.5s cadence) + continuous orbit sweep (8s period) all run TOGETHER; the two modes are mutually exclusive (starting one stops the other). Both mode machines are mirrored by `TestPreviewModesMirror` (`FPLayout::PreviewSystems` / `PreviewCyclePhaseDuration` / `LivePreviewVisemeCadence` / `LivePreviewOrbitPeriod` / `PreviewModeSystemFlags`)
- **Restore Snapshot** — renamed from the misleading "Undo" and given an explanatory tooltip; Clear State/Clear All require a confirming second click; "Log: ON/OFF" collapses the diagnostic log

**Function categories:** Preset, ViewState, Transform, ViewOverride (+ mode toggle), Textures, Import, Camera (+ follow/snap), DebugOverlays, Outline→Depth, Status, DynamicArt, TextureAndTransformParams, Blink, Expression, Viseme, Parameter, ParamBinding, Swoosh, UI, NestedArt, Targets.

**Widget API:**
| Method | Description |
|---|---|
| `SetLayerVisibility(LayerTag, bVisible)` | Toggles visibility of primitives tagged with `LayerTag`; updates persistent `LayerVisibilityOverrides` map |
| `GetLayerVisibility(LayerTag)` | Returns `false` if overridden hidden, `true` otherwise |
| `ColorByDepth(bEnabled)` | Toggles depth heat-map visualization on the preview mesh; guarded against redundant rebuilds |
| `ApplySearchFilter(Filter)` | Filters property sections by title substring match; also shows all sections when filter matches any layer name |
| `RunDiagnostics()` | Prints preset status, missing states/layers, and error counts to the diagnostic log overlay |
| `SetStatus(Msg, Color)` | Sets the status-line text and color; replaces log-only warnings |
| `SetViewOverrideMode(bEnabled)` / `GetViewOverrideMode()` | Toggles editing `ViewOverrides` (per-rendered-view deltas) instead of canonical transforms; `bViewOverrideMode` property |
| `SyncLayerToSelectedViews(State, LayerTag, DestViews, bIncludeTextures)` | Copies a slot's canonical transform (and optionally textures) to a picked set of destination views |
| `SetCameraFollowsView(bEnabled)` / `GetCameraFollowsView()` | Enables/disables orbit camera snapping to zone centers on view switch; `bCameraFollowsView` property |
| `SnapCameraToActiveView()` | Snaps orbit yaw/pitch to the active state's zone center via `GetZoneCenterYaw/Pitch` |
| `ImportTexturesFromFiles(Files)` | Imports image files into `/Game/FaceParallax/Imported` via `FAssetToolsModule` and returns the created `UTexture2D`s |
| `AssignTextureToSlot(Tex, State, LayerTag, Channel)` | Assigns a texture to Albedo/Normal/Depth of a slot, auto-fits if enabled, refreshes UI |
| `OpenImportArtDialog()` | File dialog + import + status feedback (toolbar "Import Art..." and Import tab) |
| `GenerateDepthFromOutlines(GridSize)` | Arm/confirm-guarded silhouette → depth bake: builds the visual-hull buffer and writes the depth channel of the scoped view states (per-view reprojection) |
| `SetOutlineDepthScope(Scope)` / `GetOutlineDepthScope()` | Bake scope: 0 = front only, 1 = 8 horizontal states, 2 = all 10 states |
| `GetOutlineDepthArmed()` | True between the arm click and the confirming click |
| `SetOutlineOverlayVisible(bVisible)` / `GetOutlineOverlayVisible()` | Toggles the depth-buffer overlay on the preview image |
| `SetOutlineViewEnabled(State, bEnabled)` | Adds or removes a state from `OutlineViewStates` (per-checkbox management; `SetOutlineViewState` only adds) |
| `SetActiveRailIndex(Index)` | Switches the left rail panel (0 Layers, 1 Transform, 2 Camera, 3 Debug, 4 Advanced) |
| `FillMissingViewsFromActiveSlot()` | Copies the active slot's albedo (and other channels) to views that lack art; returns the number filled |
| `SetDisplayMode(Mode)` | 0 Textured, 1 Depth mesh, 2 Wireframe, 3 Split — maps to the preview toggles |
| `GetAdjacentState(State, Offset)` | Static wrap-around state offset (onion-skin ghost source) |
| `GetLinkTargets(Active)` | Static list of the 9 link-broadcast targets for a state |
| `CopyTransformFromView(Src, Dst)` | Copies a state's canonical transform to another, guarding src==dst |
| `ToggleOnionSkin(bEnable)` / `SetOnionSkinOpacity(Opacity)` | Shows/hides the adjacent-state ghost overlay on the canvas |
| `OpenImportFolderWizard()` | Opens the folder-scan import wizard (Phase C) |
| `BuildEdgeOverlay()` | Rebuilds the Sobel edge overlay + 16-bin histogram for the active slot's albedo (Phase D) |
| `BuildLumaHistogram(Luma, Grid, OutBins)` / `EdgeDensity(Luma, Grid, Threshold)` | Static pure helpers for histogram/edge metrics (unit-mirrored) |
| `RebuildVisemeGrid()` | Rebuilds the viseme frame grid (Phase E) — rows per viseme/named viseme, filled-frame cells, click to play |
| `RebuildNestedOutliner()` | Rebuilds the nested-element outliner (Phase E) — visibility checkboxes, badges, delete |
| `RebuildParamTable()` | Rebuilds the param-binding table (Phase E) — rename, cycle target, invert, remove |
| `RebuildProblemsPanel()` | Rebuilds the validation problems list (Phase F) — missing channels, frame-count mismatches, click-to-jump |
| `FrameFillRatio(Occupied)` / `ClampGridCols(MaxFrames)` / `AppendSortedUnique(Out, Line)` / `VisemeFramesMismatch(A, B)` | Static pure helpers for the timeline/problems systems (unit-mirrored) |
| `RefreshPinControls()` | Refreshes pin slider values, readouts, and Pinned/rotation checkbox state from the selected nested element |
| `SetNestedPinFromUV(State, LayerTag, Index, FromViewState, UV)` | Converts a canvas click to `Position3D` (Back-view clicks mirror the X axis) |
| `RegisterRailSection(RailIdx, Title, Target, Accordion?, AccordionIdx?)` | Registers a rail section for chips + search jump; accordion sections pass the accordion + index so jumps auto-expand |
| `RegisterAccordionSections(RailIdx, Accordion)` | Bulk-registers every accordion section in visual order (Debug 8, Advanced 4) |
| `JumpToRailSection(RailIdx, SectionIdx)` | Switches rail if needed (pending jump consumed at rebuild end), expands the section, scrolls its header into view, highlights its chip |
| `OnRailSearchCommitted(Query)` | Enter-in-search handler: `FPLayout::FindRailSectionByTitle` across all rails → jump or status message |
| `UpdateDisclosureSummaries()` | Refreshes the Config disclosure "K of 8 on" summary from component/local toggle state |
| `GetRailWidthPx()` / `SetRailWidthLive(W)` / `ApplyRailWidthDelta(Delta)` | Rail width accessor, live clamped resize (width box only), and drag commit (clamp + rebuild) |
| `HandleHotspotClick(RegionName)` | Canvas/parts-strip pick: resolves the region to a layer, selects it, opens the Transform rail, and opens the Import Folder Wizard preselected on that part |
| `ImportHotspotRegion(RegionName)` | Alt+click path: opens the Import Folder Wizard preselected on that part |
| `ResolveHotspotLayer(RegionName)` / `RemapHotspotLayer(RegionName, Tag)` | Explicit `HotspotLayerMap` first, then `FPHotspotLayerMatch` derivation; persists explicit region→layer mappings |
| `OpenHotspotRemapMenu(RegionName, Ev)` | Right-click chip context menu: "Auto (derived)" reset + one entry per layer tag |
| `StartCyclePreview()` / `StopCyclePreview()` | Sequential 8s tour: blink → expression → viseme → orbit sweep (2s each) |
| `StartLivePreview()` / `StopLivePreview()` | Combined preview: blink + expression + viseme + orbit running together (mutually exclusive with Cycle Preview) |

---

## Building and Testing

### Project Setup

The plugin is a standalone UE5.8 plugin — copy `SAMPLES/MyProject/Plugins/FaceParallax/` into your project's `Plugins/` directory and enable it in the `.uproject`. During development, the root `*.h`/`*.cpp` copies are canonical; `Tests\run_tests.ps1` syncs them into the plugin before every UE build.

### Running Tests

```powershell
.\Tests\run_tests.ps1 -IncludeUEBuild
```

This runs:
1. **SAMPLES sync** — copies the 19 root source files into `SAMPLES/MyProject/Plugins/FaceParallax/Source/` (runtime Public/Private, editor Public/Private)
2. **Python syntax validator** — braces/macros/includes on all `.h`/`.cpp` files
3. **C++ math tests** — 1082 standalone tests (g++ from msys64 ucrt64), including silhouette-edge distance, visual-hull depth (front view + per-view yaw/pitch variant), camera-snap zone-center mapping, import channel-suffix detection, Phase B–G mirror suites (gizmo mapping both directions + round trip, link broadcast, suffix parser, sync drift, luminance histogram, Sobel edge density, fill ratio, grid columns, sorted-unique dedupe, frame-count mismatch, outline-depth bake quantization, depth-scope targeting), pin view-angle rotation (mapping, clamping, wrapping, sensitivity, back-view authoring round-trip, slider normalization, 8-state projection sweep, effective-transform rotation accumulation, cross-view sync pin preservation), and Phase H UI design contract (P1–P15 over the layout manifest: zero violations, the exact 5-rail scroll-viewport set (rails 180×560 clipped in nested horizontal+vertical SScrollBoxes), mirrored design constants, anchor-node presence, negative controls proving every principle fires, P14 props-pane right-edge gap and P15 scroll-content right inset, plus section slots auto-stacked so sections never paint over each other)
4. **UE build test** — full compilation of `SAMPLES/MyProject` with `Build.bat` (must produce `UnrealEditor-FaceParallax.dll` and `UnrealEditor-FaceParallaxEditor.dll`)

Optional flags:
- `-SyncSamples` — copies root `*.h`/`*.cpp` to the plugin source dirs without building

---

## Deployment: Step-by-Step

This section walks through the complete workflow from running the deployment script (`deploy.py`) to opening the editor widget and adding art to your character. **All binary assets are created by `deploy.py`** — the one and only deployment mechanism (there is no C++ deploy pipeline).

### Prerequisites

- Unreal Engine 5.x project with the FaceParallax plugin enabled
- A skeletal mesh for your character (the head or body mesh)

### Step 1: Compile the C++ Code

```powershell
# From your project root
& "H:\unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat" YourProjectEditor Win64 Development "path\to\YourProject.uproject" -waitmutex
```

Verify the build succeeds with no errors. The FaceParallax C++ classes (component, preset, preview actor, editor widget, depth debug visualizer) are now available.

### Step 2: Run the Deployment Script (deploy.py)

From the Python console in the editor (this is the canonical command):

```python
py "G:\tailedstories\paralax\deploy.py"
```

Or headlessly via commandlet:

```powershell
UnrealEditor-Cmd.exe "D:\Projects\YourProject\YourProject.uproject" -run=pythonscript -script="deploy.py"
```

`deploy.py` verifies the C++ plugin classes are loaded, deletes stale legacy assets, then creates everything.

**What deploy.py creates:**

| Asset | Path | Description |
|---|---|---|
| `M_FaceParallax_Master` | `/Game/FaceParallax/Materials/` | Master material with all parameters wired (crossfade, parallax, expression blend, depth debug, nested art pivot) — this is the name the runtime component loads |
| `MI_FaceParallax_{LayerTag}` | `/Game/FaceParallax/Materials/Instances/` | One material instance per layer, parented to master, with all runtime parameter defaults |
| `DA_FaceParallaxPreset` | `/Game/FaceParallax/Presets/` | Preset DataAsset with ViewAssignments for all 10 states × all layer tags |
| `BP_FaceParallaxCharacter` | `/Game/FaceParallax/Blueprints/` | Character BP with FaceParallaxComponent attached, skeletal mesh assigned, wired in place |
| `WBP_FaceParallaxEditor` | `/Game/FaceParallax/Blueprints/` | Editor widget BP derived from `UFaceParallaxEditorWidget` (existing asset is deleted and recreated with a clean CDO — no stale imports) |
| `RT_FaceParallaxPreview` | `/Game/FaceParallax/Textures/` | Render target wired to the preview actor |

**What deploy.py also does:**

- Gives the master material white albedo fallbacks so face-layer quads stay visible in the preview even before any art is imported (unassigned layers sample UE's built-in 1x1 white `DefaultTexture` and render as white patches instead of invisible black)
- Cleans the widget BP CDO's `PreviewActor`/`ActivePreset` references so the asset never bakes stale level-actor/preset imports
- Deletes legacy/wrong-named assets from older pipelines: `DA_FaceParallax_Default` (stale class import — "its class does not exist"), `M_FaceParallaxMaster` (wrong name), `Materials/MI_Face_*` (wrong path)
- Spawns exactly one `AFaceParallaxPreviewActor` in the level (stale actors removed first), assigns mesh + render target + preset, and spawns layer quads so art is visible immediately
- The widget's fallback preset chain is: actor's component preset → `DA_FaceParallaxPreset` → AssetRegistry scan for any `UFaceParallaxPreset` → in-memory default (4 standard layers)
- Opening the editor tab auto-spawns a `AFaceParallaxPreviewActor` when none exists in the level; the actor combo lists all existing preview actors for manual re-selection

### Step 3: Spawn Face-Layer Quads (Automated)

Face-layer quads are plane meshes tagged with the layer tag (e.g. `Eyes`, `Brows`, `Mouth`, `Hair`) that the component discovers and drives via dynamic material instances.

- **Preview actor** — the widget's **Spawn Quads** toolbar button spawns quads automatically; at runtime, set `bAutoSpawnLayerQuads = true` (default) on the component: at BeginPlay it spawns quads for every `LayerDefinitions` entry that has no tagged primitive already present on the actor, then applies the active preset's textures. Hand-placed quads are never duplicated (skips tags that already have a tagged primitive)

Spawn settings on the component (category `Face Parallax|Layer Quads`):

| Property | Default | Description |
|---|---|---|
| `bAutoSpawnLayerQuads` | true | Auto-spawn missing quads at BeginPlay |
| `LayerMaterialPathRoot` | `/Game/FaceParallax/Materials/Instances/MI_FaceParallax_` | Material instance path root; `LayerTag` is appended. Falls back to the master material with a warning if the instance is missing |
| `LayerQuadWorldWidth` | 100.0 | World-space quad width; height derives from the preset `CanvasSize` aspect |
| `LayerQuadLocalOffset` | (10,0,0) | Quad offset relative to the head bone (slightly forward of the face plane) |

Nested art elements from the Front state spawn as child quads (tag `{LayerTag}_{ElementName}`, stacked forward to avoid z-fighting).

### Step 4: Import and Assign Art Textures

With the widget open (**it auto-opens as a dockable tab when the editor starts**, or open it manually: **Window → Face Parallax Editor** menu entry, the **Face Editor** toolbar button, or type `FaceParallaxOpenEditor` in the console):

> **Note:** the preview camera frames the mannequin's head (where the face layers live), and unassigned layers render as white patches until you assign art textures — the character itself stays a default mannequin until you import art and assign it via the widget.

**Troubleshooting the editor widget:**

- **Widget target properties (`PreviewActor`, `ActivePreset`) are `Transient`** — they are runtime bindings and are never serialized into the widget asset. This prevents `Illegal TEXT reference ... Import failed` warnings and the PreviewActor "resets to none" issue.
- **The widget must open as a docked tab** inside the main editor window. If you get a separate floating window, the old C++ module is still loaded (Live Coding patches do not survive editor restarts — restart the editor and run `deploy.py` again in the new session). Verify the build with the log markers: `[FaceParallax] EditorSubsystem initialized - DOCKED-TAB BUILD v3 (marker 0xV3)`, `[FaceParallax] Auto-open on startup — invoking tab` (startup auto-open) and `[FaceParallax] OpenEditorWidget — invoking nomad tab 'FaceParallaxEditor'`. Every button click logs `[FaceParallaxWidget] CLICK '...'`, and every widget rebuild logs `[FaceParallaxWidget] REBUILD DOCKED-TAB v3`.
- **Auto-open on startup** — the subsystem opens the docked tab once per editor session (default ON). To disable, add to `Config/DefaultEditor.ini`: `[/Script/FaceParallaxEditor.FaceParallaxEditorSubsystem]` with `bAutoOpenEditorOnStartup=false`. The tab is only auto-opened once per process, so a Live Coding re-init never force-reopens a tab you closed.
- **`FaceParallaxOpenEditor` is a registered console command** (not just a `UFUNCTION(Exec)`) — the subsystem registers it with `IConsoleManager` at initialization, so it dispatches even if Live Coding has left the exec binding stale. If typing it still does nothing, restart the editor and run `deploy.py` again — the command is only registered by the freshly built module.

1. **Select a View State** — click one of the 10 state buttons (Front, 3/4R, ProR, etc.)
2. **Select a Layer** — click a layer name in the Layers panel
3. **Assign Textures** — use the texture slots in the Properties panel:
   - Click the **Albedo** thumbnail to pick a texture from the Content Browser
   - Click the **Normal** thumbnail to assign the normal map
   - Click the **Depth** thumbnail to assign the depth map
   - Each assignment auto-updates the preview

Alternatively, use the **Import Art...** button to batch-import texture files from disk (opens a file dialog for selecting PNG/TGA/EXR files).

### Step 5: Configure Each View State

Repeat Step 4 for all 10 view states. The widget's **Status** panel shows which states/layers are still missing textures. The **Diagnostic Log** at the bottom inlines validation results.

**Pro tips:**
- Use **Auto-Fit** to auto-scale textures to the canvas size
- Use **Sync Layer to All Views** to copy a transform from one state to all others
- Use **Duplicate State** to copy an entire state's assignments
- Use **Snapshot/Undo** for safe experimentation — all edits are transaction-backed

### Step 6: Configure Per-Layer Settings

In the component's `LayerDefinitions` array, adjust per-layer:
- `DepthScale` — parallax movement amount
- `DepthMapIntensity` — depth effect strength
- `bInvertParallax` — for background layers

### Step 7: Save and Use the Preset

1. Click **Save Preset** in the widget to persist the preset data asset
2. Assign the preset to your runtime character's `FaceParallaxComponent.ActivePreset`
3. The character will now automatically switch textures as the camera orbits around it

### Post-Deployment Customization

- **Zone Boundaries**: Adjust `ZoneBoundaryMultipliers` on the component to customize view zone widths
- **Camera Source**: Set `CameraSource` to PlayerCamera0, SpecifiedActor, or Sequencer
- **Continuous Blending**: Enable `bUseContinuousBlending` for smooth crossfades at zone boundaries
- **Jiggle Physics**: Add nested art elements with jiggle for dynamic secondary motion
- **Occlusion Pins**: Use 3D pin projection on nested elements for perspective-aware positioning; with `bEnableViewAngleRotation` the element also rotates around the pin as the camera turns (Min/Max rotation sweep + sensitivity, see "3D Pins & View-Angle Rotation")

---

## Default Property Values

| Property | Default | Category |
|---|---|---|
| `HeadBoneName` | "head" | Skeletal Mesh |
| `bAutoSpawnLayerQuads` | true | Layer Quads |
| `LayerMaterialPathRoot` | "/Game/FaceParallax/Materials/Instances/MI_FaceParallax_" | Layer Quads |
| `LayerQuadWorldWidth` | 100.0 | Layer Quads |
| `LayerQuadLocalOffset` | (10,0,0) | Layer Quads |
| `TopViewPitchThreshold` | 60.0 | View Angles |
| `BottomViewPitchThreshold` | -60.0 | View Angles |
| `HalfZoneWidth` | 22.5 | View Angles |
| `ZoneBoundaryMultipliers` | {1,3,5,7} | View Angles |
| `CrossfadeSpeed` | 15.0 | Transitions |
| `HysteresisFrames` | 3 | Transitions |
| `MaxParallaxOffset` | 5.0 | Parallax |
| `MaxVerticalParallaxOffset` | 3.0 | Parallax |
| `DepthMapIntensity` | 1.0 | Depth Maps |
| `bUseMaterialDrivenDepth` | true | Depth Maps |
| `bUseContinuousBlending` | true | Transitions |
| `BlendWindowWidth` | 5.0 | Transitions |
| `bAutoApplyPreset` | true | Preset |
| `bBlinkingEnabled` | true | Blink |
| `BlinkIntervalMin/Max` | 3.0/7.0 | Blink |
| `BlinkFrameDuration` | 0.03 | Blink |
| `CurrentExpression` | Neutral | Expression |
| `ExpressionCrossfadeDuration` | 0.3 | Expression |
| `bVisemeEnabled` | true | Viseme |
| `VisemeFrameDuration` | 0.04 | Viseme |
| `bDriveArtPositionFromYaw` | false | Art Transform |
| `MaxYawArtOffset` | 0.05 | Art Transform |
| `bNestedArtEnabled` | true | Nested Art |
| `ArtPivotParamName` | "ArtPivot" | Nested Art |

---

## Project Files

```
FaceParallaxTypes.h                      — Shared types and structs
FaceParallaxComponent.h/.cpp             — Core parallax component
FaceParallaxPreset.h/.cpp                — Preset DataAsset
DepthDebugVisualizerComponent.h/.cpp     — Depth debug visualizer
FaceParallaxPreviewActor.h/.cpp          — Preview actor
FaceParallaxEditorWidget.h               — Editor widget header (Blueprint-facing API)
FaceParallaxEditorWidget.cpp             — Widget core API (targets, accessors, batch ops)
FaceParallaxEditorWidgetUI.cpp           — Widget Slate construction (RebuildWidget)
FaceParallaxEditorWidgetInteractions.cpp — Widget selection, gizmo/pin math, wizard, grids
FaceParallaxEditorWidgetPanels.cpp       — Widget refresh, panels, diagnostics
FaceParallaxEditorWidgetShared.h         — Shared widget internals (helpers + SFaceLayerGizmo)
FaceParallaxLayoutSpec.h                 — Phase H layout manifest + P1..P15 design validator
FaceParallaxEditorSubsystem.h/.cpp       — Editor subsystem (toolbar, tab; deployment is deploy.py)
deploy.py                                — THE deployment script: creates every binary asset in-editor
FaceParallaxModule.cpp                   — Runtime module entry (IMPLEMENT_MODULE)
AGENTS.md                                — Agent guide with rules and test info

Tests/
  ParallaxMathTests.cpp                  — 1082 standalone C++ tests
  SyntaxValidator.py                     — Python syntax validation
  run_tests.ps1                          — Test runner

SAMPLES/MyProject/                       — Standalone UE5 project (plugin copy) for CI builds
```
