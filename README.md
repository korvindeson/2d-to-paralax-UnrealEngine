# FaceParallax — 2D Face Parallax System for Unreal Engine 5

Camera-driven 2D face rendering with multi-layer parallax, depth map support, view-state transitions across 10 angles, real-time depth debug visualizer, preset asset system, and in-editor visual editor with 774+ automated tests.

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
| `FaceParallaxEditorWidget.h/.cpp` | Editor Widget | C++ `UUserWidget` subclass providing bindable functions for every setting across 19 categories. Diagnostic log overlay, auto-refresh via `FCoreUObjectDelegates::OnObjectModified`. |
| `deploy.py` | Deployment script | Creates master material, material instances, preset data asset, and character BP inside the Unreal Editor Python console |
| `_gen_embed.py` | Maintenance script | Re-encodes all `.h`/`.cpp` files into `deploy.py`'s `EMBEDDED_SOURCES` for self-contained deployment |
| `Tests/ParallaxMathTests.cpp` | Math tests | Standalone C++17 (no UE dep) — 774 tests covering state machine, transforms, blink/expression/viseme, swoosh, parameters, nested art + jiggle, 3D pin projection, batch ops, zone multipliers |
| `Tests/SyntaxValidator.py` | Syntax validator | Brace/macro balance, include guards — enforces clean parsing on all source files |
| `Tests/run_tests.ps1` | Test runner | Syntax validator + C++ math tests + optional UE build test |
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

Include the module in your project's `Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore",
    "LevelSequence", "CinematicCamera", "AssetRegistry",
    "ProceduralMeshComponent",
});

if (Target.Type == TargetRules.TargetType.Editor)
{
    PrivateDependencyModuleNames.AddRange(new string[] {
        "ContentBrowser", "AssetRegistry", "UnrealEd", "DesktopPlatform",
    });
}
```

Also add `"ProceduralMeshComponent"` to your `.uproject` plugin list:

```json
"Plugins": [
    { "Name": "ProceduralMeshComponent", "Enabled": true }
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
| `GetNestedPin3D` / `SetNestedPin3D` | Pin projection access |
| `GetSwooshArt` / `SetSwooshArt` / `HasSwooshArt` / `ClearSwooshArt` | Swoosh art access |
| `GetParamBindings` / `SetParamBindings` | Parameter binding access |
| `GetAltTextures` / `SetAltTextures` | Alt texture set access |
| `BatchSetTextures` / `BatchSetTexturesAllLayers` | Batch texture assignment |
| `DuplicateState` / `ClearAllTextures` / `SyncLayerNestedToAllViews` | Batch operations |
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

**Component API:**
| Method | Description |
|---|---|
| `ExtractSilhouettePoints(State, MaxPoints)` | Scans the state's albedo and returns (xMin, xMax) pairs per scanline |
| `SilhouetteDistanceToEdge(State, LocalPoint)` | Signed distance to the nearest silhouette edge for a state |
| `GenerateDepthBufferFromOutlines(GridSize, OutDepth, OutCellSize)` | Builds a visual-hull depth buffer over the grid |
| `SilhouetteDistanceToEdgeStatic(Points, Point)` | Pure static signed-distance math (unit-tested) |
| `VisualHullDepthStatic(Front, Right, Left, Top, Bottom, Point)` | Pure static visual-hull depth (unit-tested) |
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

**Camera Controls:** SetOrbitYaw, SetOrbitPitch, SetOrbitDistance, SetPreviewFOV, ResetCamera, SetAutoRotate.

---

## Editor Widget (UFaceParallaxEditorWidget)

The `UUserWidget` subclass constructs its entire UI via `RebuildWidget()` — no UMG Designer needed.

**Key improvements:**
- **Diagnostic log overlay** — `SMultiLineEditableTextBox` at bottom of widget; `RunDiagnostics()` reports preset status, missing states/layers
- **Auto-refresh** — bound to `FCoreUObjectDelegates::OnObjectModified`; triggers `RefreshUI()` when ActivePreset modified externally
- **Undo/redo** — `FPresetTransactionScope` RAII guard wraps 13 preset-mutating functions with `GEditor->BeginTransaction`/`EndTransaction` and `Modify()`
- **Tabbed property panel** — `SWidgetSwitcher` with 4 tabs: Transform | Import | Preview & Debug | Advanced (search, view override, sync picker, outline→depth, camera follow on Transform; import buttons + config on Import; camera/zone/blend on Preview & Debug; cross-layer overlay, param reference, nested art on Advanced)
- **Thumbnail status matrix** — 10-state × N-layer grid of aspect-correct albedo thumbnails (pixel size + click-to-jump tooltip) replacing the old 22×20 ✓/× grid
- **Camera follows view** — view-strip clicks and matrix jumps snap orbit yaw/pitch to the state's zone center when `bCameraFollowsView` is enabled (default)
- **View override mode** — transform edits write `ViewOverrides` (per-rendered-view deltas) instead of canonical transforms when `bViewOverrideMode` is on
- **Silhouette → depth** — "Generate Depth from Outlines" extracts silhouette edges from rotation-view albedo textures, builds a visual-hull depth buffer, and bakes it into every layer's depth channel
- **Batch import by channel suffix** — imports auto-assign by filename suffix (`_N`/`_Normal`/`_normalmap` → Normal, `_D`/`_Depth`/`_height`/`_displacement` → Depth, else Albedo); "Import & Assign" and toolbar "Import Art..." both use it when a layer is selected
- **Quick Actions** — Auto-Fit All, Sync All→All, Clear All Overrides, Duplicate Front→This, Fill Missing Views (copies the active slot's art to views that have no albedo yet)
- **Outline view management** — per-state checkboxes bound to the component's `OutlineViewStates` (All/None shortcuts); depth grid resolution is user-editable (8–256)
- **Toolbar search** — the filter box moved to the toolbar so it applies across all 4 property tabs
- **Live numeric camera readouts** — Yaw/Pitch/Dist values shown next to the sliders and updated on refresh
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
| `GenerateDepthFromOutlines(GridSize)` | Extracts silhouette edges from outline-state albedos, builds a visual-hull depth buffer, bakes it into all layers' depth channels |
| `SetOutlineOverlayVisible(bVisible)` / `GetOutlineOverlayVisible()` | Toggles the depth-buffer overlay on the preview image |
| `SetOutlineViewEnabled(State, bEnabled)` | Adds or removes a state from `OutlineViewStates` (per-checkbox management; `SetOutlineViewState` only adds) |

---

## Building and Testing

### Project Setup

Add the source files to your UE5 module. Ensure `Build.cs` includes the dependencies listed in the Module section above.

### Running Tests

```powershell
.\Tests\run_tests.ps1 -IncludeUEBuild
```

This runs:
1. **`_gen_embed.py` staleness check** — verifies embedded sources match disk files
2. **Python syntax validator** — braces/macros/includes on all `.h`/`.cpp` files
3. **C++ math tests** — 864+ standalone tests (g++ from msys64 ucrt64), including silhouette-edge distance, visual-hull depth, camera-snap zone-center mapping, and import channel-suffix detection
4. **UE build test** — full compilation of SAMPLES project with `Build.bat`

Optional flags:
- `-SyncSamples` — copies root `*.h`/`*.cpp` to `SAMPLES/MyProject/Source/MyProject/` before the UE build

### Maintaining deploy.py

After editing source files:
```powershell
python _gen_embed.py
```
This re-encodes all `.h`/`.cpp` into `deploy.py`'s `EMBEDDED_SOURCES` for self-contained deployment.

---

## Deployment: Step-by-Step

This section walks through the complete workflow from running `deploy.py` to opening the editor widget and adding art to your character.

### Prerequisites

- Unreal Engine 5.x project with the FaceParallax source files in a module
- `ProceduralMeshComponent` plugin enabled in `.uproject`
- Build.cs updated with all dependencies (see Module section)
- A skeletal mesh for your character (the head or body mesh)

### Step 1: Compile the C++ Code

```powershell
# From your project root
& "H:\unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat" YourProjectEditor Win64 Development "path\to\YourProject.uproject" -waitmutex
```

Verify the build succeeds with no errors. The FaceParallax C++ classes (component, preset, preview actor, editor widget, depth debug visualizer) are now available.

### Step 2: Run deploy.py Inside Unreal Editor

**Important:** `deploy.py` runs inside the Unreal Editor Python console, not as a standalone script. It creates binary assets that cannot be generated from text files.

1. Open your project in the Unreal Editor
2. Open the **Output Log** (Window → Developer Tools → Output Log)
3. Switch to the **Python** tab
4. Run:
```python
exec(open(r"D:\Projects\YourProject\deploy.py").read())
```

Alternatively, launch the editor with the script:
```powershell
UnrealEditor-Cmd.exe "D:\Projects\YourProject\YourProject.uproject" -run=pythonscript -script="deploy.py"
```

**What deploy.py creates:**

| Asset | Path | Description |
|---|---|---|
| `M_FaceParallax_Master` | `/Game/FaceParallax/Materials/` | Master material with all parameters wired (crossfade, parallax, expression blend, depth debug, nested art pivot) |
| `MI_FaceParallax_{LayerTag}` | `/Game/FaceParallax/Materials/Instances/` | One material instance per layer, parented to master |
| `DA_FaceParallax_Default` | `/Game/FaceParallax/Presets/` | Preset DataAsset with ViewAssignments for all 10 states × all layer tags |
| `BP_FaceParallaxCharacter` | `/Game/Characters/` | Character BP with FaceParallaxComponent attached, pre-configured HeadBoneName, LayerDefinitions, and skeletal mesh |
| `WBP_FaceParallaxEditor` | `/Game/FaceParallax/Blueprints/` | Editor widget BP derived from `UFaceParallaxEditorWidget` |
| `RT_FaceParallaxPreview` | `/Game/FaceParallax/Textures/` | 1024×1024 render target for the preview actor's scene capture |
| Preview actor | Editor world (spawned) | `AFaceParallaxPreviewActor` with skeletal mesh, preset, and face-layer quads applied |

**What deploy.py also does:**

- Assigns the skeletal mesh (config `CHARACTER_MESH_PATH` at the top of `deploy.py` — point it at your own mesh to skip manual assignment; falls back to the Mannequin) and sets the mesh's relative offset to `(0,0,-90)` so the mannequin stands with its feet on the ground instead of hovering at the capsule center
- Auto-adds the `FaceParallaxComponent` to the character BP if missing (via `SubobjectDataSubsystem`; the component's C++ defaults `HeadBoneName="head"`, `bAutoSpawnLayerQuads=true` apply automatically — assign `ActivePreset` in the BP or at runtime)
- Repairs an existing character BP in place (re-checks mesh, offset, and component; never deletes the asset)
- Gives the master material white albedo fallbacks so face-layer quads stay visible in the preview even before any art is imported (unassigned layers sample UE's built-in 1x1 white `DefaultTexture` and render as white patches instead of invisible black)
- Spawns the face-layer quads on the preview actor (plane meshes attached to the head bone, tagged with each layer tag, material instance per layer applied, Front-state nested art included)

### Step 3: Spawn Face-Layer Quads (Automated)

Face-layer quads are plane meshes tagged with the layer tag (e.g. `Eyes`, `Brows`, `Mouth`, `Hair`) that the component discovers and drives via dynamic material instances.

- **Preview actor** — deploy.py spawns quads automatically; the widget's **Spawn Quads** toolbar button re-runs this at any time
- **Character/pawn at runtime** — set `bAutoSpawnLayerQuads = true` (default) on the component: at BeginPlay it spawns quads for every `LayerDefinitions` entry that has no tagged primitive already present on the actor, then applies the active preset's textures. Hand-placed quads are never duplicated (skips tags that already have a tagged primitive)

Spawn settings on the component (category `Face Parallax|Layer Quads`):

| Property | Default | Description |
|---|---|---|
| `bAutoSpawnLayerQuads` | true | Auto-spawn missing quads at BeginPlay |
| `LayerMaterialPathRoot` | `/Game/FaceParallax/Materials/Instances/MI_FaceParallax_` | Material instance path root; `LayerTag` is appended. Falls back to the master material with a warning if the instance is missing |
| `LayerQuadWorldWidth` | 100.0 | World-space quad width; height derives from the preset `CanvasSize` aspect |
| `LayerQuadLocalOffset` | (10,0,0) | Quad offset relative to the head bone (slightly forward of the face plane) |

Nested art elements from the Front state spawn as child quads (tag `{LayerTag}_{ElementName}`, stacked forward to avoid z-fighting).

### Step 4: Import and Assign Art Textures

With the widget open (type `FaceParallaxOpenEditor` in the console, or click the **Face Editor** toolbar button — it opens as a dockable tab in the main editor window):

> **Note:** the preview camera frames the mannequin's head (where the face layers live), and unassigned layers render as white patches until you assign art textures — the character itself stays a default mannequin until you import art and assign it via the widget.

**Troubleshooting the editor widget:**

- **Widget target properties (`PreviewActor`, `ActivePreset`) are `Transient`** — they are runtime bindings and are never serialized into the widget asset. This prevents `Illegal TEXT reference ... Import failed` warnings and the PreviewActor "resets to none" issue.
- **There is exactly one preview actor** — `deploy.py` destroys stale preview actors before spawning the current one.
- **The widget must open as a docked tab** inside the main editor window. If you get a separate floating window, the old C++ module is still loaded (Live Coding patches do not survive editor restarts — run `deploy.py` again in the new session). Verify the build with the log markers: `[FaceParallax] EditorSubsystem initialized — DOCKED-TAB BUILD v3` and `[FaceParallax] OpenEditorWidget — invoking nomad tab 'FaceParallaxEditor'`. Every button click logs `[FaceParallaxWidget] CLICK '...'`, and every widget rebuild logs `[FaceParallaxWidget] REBUILD DOCKED-TAB v3`.
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
- **Occlusion Pins**: Use 3D pin projection on nested elements for perspective-aware positioning

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
FaceParallaxEditorWidget.h/.cpp          — Editor widget
deploy.py                                — UE Python asset creation script
_gen_embed.py                            — Source re-encoder for deploy.py
AGENTS.md                                — Agent guide with rules and test info

Tests/
  ParallaxMathTests.cpp                  — 774 standalone C++ tests
  SyntaxValidator.py                     — Python syntax validation
  run_tests.ps1                          — Test runner
  ue_build_test.ps1                      — UE build test

SAMPLES/MyProject/                       — Standalone UE5 project for CI builds
```
