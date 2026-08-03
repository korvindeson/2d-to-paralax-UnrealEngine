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
| `FaceParallaxEditorWidgetShared.h` | Widget shared internals | Anonymous-namespace helpers (channel/view-state suffix parsing, `FPresetTransactionScope` (backup-point only), `FWidgetUndoScope` (undo-stack push + transaction), `AccentBlue`, `MakeLbl`/`MakeBtn`) + the `SFaceLayerGizmo` nested class, shared by all widget translation units |
| `FaceParallaxLayoutSpec.h` | UI design contract (Phase H) | Pure C++17 layout manifest + metrics/placement solver + P1–P23 design-principle validator over the widget tree (P22 NoHorizontalOverflow: no non-flex child may exceed the clip-parent's `FixedW` margin box — rails never scroll horizontally; P23 AspectRatioBroken: `bAspectRatio` nodes must resolve to `FaceAspectRatio`). Self-checked in `RebuildWidget` and fully covered by `TestPhaseHUIDesign`. |
| `FaceParallaxSchematic.h` | Part schematic manifest (redesign) | Pure C++17 (synced into the editor module) — 17 part glyphs with depth classes (Front/Base/Back), the 10-layer tag table `FPTagClassForTag`, part-name coverage aliases `FPSchematicLayerAlias` (Teeth→Mouth, Chin/Neck→Head), the hair system contract `FPHairLayerSet`/`FPSchematicIsHairLayer` (Bangs = front hair, Hair/BackHair = back hair), the canvas filter mirror `FPSchematicFilterAllows`, the group-colored edge map contract `FPEdgeGroup`/`FPEdgeGroupForPartName`/`FPEdgeGroupForTag`/`FPHairLevelForTag`/`FPHairLevelLuminance`/`FPEdgeLuminanceForClass`/`FPEdgeGroupColor`/`FPEdgeColorForPart`/`FPEdgeMapShows` (eyes/mouth/hair/surface groups, front lighter than back, hair detailed levels distinct + toggleable), and the front/base/back yaw rules `FPYawRule` that `deploy.py`'s base preset and the component's `SyncLayerDefinitionsFromPreset` both consult. Covered by `TestSchematicParts`/`TestYawRule`/`TestSchematicCoverage`/`TestHairSystem`/`TestSchematicFilters`/`TestEdgeMapMirrors`. |
| `FaceParallaxEditorSubsystem.h/.cpp` | Editor subsystem | Registers the **Face Editor** toolbar button + **Window → Face Parallax Editor** menu entry, the `FaceParallaxOpenEditor` console command, and auto-open on editor startup (`bAutoOpenEditorOnStartup`, default ON); hosts the widget in a docked nomad tab. Asset deployment is handled entirely by `deploy.py` (repo root) |
| `FaceParallaxModule.cpp` | Module entry | `IMPLEMENT_MODULE(FDefaultModuleImpl, FaceParallax)` — required for the runtime DLL to register |
| `Tests/ParallaxMathTests.cpp` | Math tests | Standalone C++17 (no UE dep) — 1764 tests covering state machine, transforms, blink/expression/viseme, swoosh, parameters, nested art + jiggle, 3D pin projection + view-angle rotation, batch ops, zone multipliers, per-view visual hull, Phase B–H mirror suites + Phase H layout-design contract, part schematic + yaw rules + coverage/aliases + hair system + canvas filters + midpoint jiggle ramp + edge-map group mirrors |
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
| `UFaceParallaxEditorWidget` | `UUserWidget` subclass with bindable Blueprint functions for every editor setting — transform sliders, view overrides, auto-fit, sync, camera, debug toggles, material params, status, nested art. Includes diagnostic log and auto-refresh on preset modification. Undo/redo via a 32-entry preset-duplicate undo stack (`FWidgetUndoScope` pushes a pre-mutation copy and wraps it in a `GEditor->BeginTransaction`) plus a single-slot manual Backup Point. |

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
| `SyncCanonicalAxisToAllViews(State, LayerTag, Axis)` | Per-axis sync: rewrites only the chosen canonical axis (0 PosX, 1 PosY, 2 ScaleX, 3 ScaleY, 4 Rotation) across all views, preserving untouched axes (mirrors `FPLayout::SyncAxisDelta`) |
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

**Midpoint chain split (hair-end swing):** `FFaceJiggleSettings` adds `Midpoint` (0..1, default 1.0 = disabled) plus `EndStiffness`/`EndDamping`/`EndImpulseScale`. Chain progress (accumulated local-offset distance from each chain root to its tip, normalized per chain, computed in `UpdateNestedArtTick`) feeds `FPHairSegmentRamp` (pure C++, math-mirrored): below/at the midpoint the base spring values apply; past it they smoothstep-blend toward the End* values. Defaults are identity — the legacy uniform spring is untouched unless you set a midpoint and different end values (e.g. low EndStiffness + high EndImpulseScale for a bigger swing at hair ends). The Nested Art / Pins pane exposes a **Jiggle** checkbox and sliders for all seven fields (Stiff, Damp, Imp, Mid, End Stiff/Damp/Imp) on the selected nested element.

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
- **Pin gizmo:** the preview-canvas gizmo paints the pin target at the selected pin's projected UV in the active view. **P3: pin mode is now EXPLICIT** — the canvas strip's **Pin Mode** toggle (amber when on; no more silently switching the click model whenever a pinned element was selected). With Pin Mode ON, **click-drag on the canvas is the primary pin interaction**: drag a pin handle to move it, click empty canvas to **place** a pin on the selected element (or the whole-layer pin when no element is selected) — both write through the `SetNestedPinFromUV`/`LayerPinFromUV` authoring paths (`PlacePinAtUV`). Off = one-map part selection
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
- **Undo/redo (real stack)** — every preset mutation wraps in `FWidgetUndoScope`, which pushes a pre-mutation preset duplicate onto the 32-entry `UndoStack` (`/Temp/FaceParallaxUndoStack` transient package) and wraps the edit in `GEditor->BeginTransaction`/`Modify()`. Undo/Redo pop the stack, copy the whole preset back (`RestoreFromBackup` — a full copy-back, never a delta), and refresh; a new mutation clears the redo branch. Toolbar Undo/Redo buttons drive it, and the **P6 History ▾ toolbar menu** (`TB-History`) lists the full undo/redo stacks — click an entry to revert/re-apply to that point — plus the Snapshot/Restore pair (the old single-slot snapshot survived as **Backup Point**/**Restore Backup** and does not touch the undo stack; `bIsRestoringUndo` guards restores). Mirror tests: `TestUndoStackSemantics`, `TestUndoRedoClearsOnNewMutation`, `TestUndoPreservesUntouchedViews`, `TestUndoStackCap`
- **3-pane workspace (Phase A)** — left rail column + `SWidgetSwitcher` rail panels, center preview canvas (display-mode row + overlay stack), right selected-slot properties pane. Old T0–T3 tabs were re-homed into the rails (rail-local Quick Actions/Sync/Alignment; Camera Follow/zones/blend; import/config/outline→depth; cross-layer/params/nested/dev tools)
- **Top-level tab bar + rail regroup (Phase B, P6 merge)** — the old single-char icon column is replaced by a labeled 5-group tab row (`FPLayout::TopTabs`: TT-Tab0..4 + TT-Spacer, widths 82/34/116/82/100, `TabBarHeight` 26, `AccentBlue` active highlight): **View & Layer** (Layers + Status Detail + All Layers), **Art** (Quick Actions, Cross-View Transform, Import, Outline→Depth, Bulk Assign, Assign Ops), **Nested & Animated** (Nested Art/Pins + Viseme Frames + Hull Review — the old separate **Animated Variants** and **Nested Elements & Pins** rails merged into one accordion in P6), **Camera/Preview** (Camera Follow, Camera, Blend Preview), **Diagnostics** (the Diagnostics group leads — Tag Validator, Material Cross-Reference, Param Reference, Edge Analysis — then Config, Param Bindings, Depth Debug, Problems; the old History group moved out into the toolbar's History ▾ menu). Param Reference + Param Bindings were re-homed from Nested & Pins into the Diagnostics rail (review grouping: params live with the diagnostics); the section registry (`FPLayout::RailSectionTitles`) mirrors the moves. Every rail keeps its chip row + section registry; the tab bar sets `ActiveRailIndex` exactly like the old icons, so chips, search jumps, and `SetActiveRailIndex` all stay in sync
- **View strip with status dots (Phase A)** — per-state tabs + colored dots (green complete / amber missing albedo / orange per-view overrides) via `GetStateDotColor`, plus a "v" context menu per state: Clear overrides (this slot) + the apply-to-views picker (P2)
- **Apply-to-views picker (P2)** — ONE picker component (`BuildApplyToViewsContent`), mounted in the state-tab "v" menu, Art rail Quick Actions, and the Bulk Assign ops row. It shows the 10 view picks (the same `SyncViewCheckBoxes` state the state-strip pick mode toggles; the active view reads "This" and is excluded), then applies the ACTIVE slot via the canonical API: **Apply to picked views** (`SyncLayerToSelectedViews`, transform + textures), **All views** (`SyncLayerToAllViews` + `SyncTexturesLayerToAllViews`), **Copy from Front → This** (`DuplicateState`, hidden on the Front view), **Fill missing views from This** (`FillMissingViewsFromActiveSlot`). This replaced the old sprawl: the "v" menu's Sync-layer-all/Sync-textures-all/Fill-missing/9-button Duplicate-from list, Quick Actions' Duplicate Front→This + Fill Missing Views, and Assign Ops' Fill Missing + Slot → All
- **Per-part status chips (P2)** — the schematic glyphs and the 17-part legend chips now show per-part slot completeness in the ACTIVE view (green = full A/N/D, amber = partial, red = missing): the glyph layer paints a status dot at each part's centroid (painted after outlines so it always sits on top), and `RebuildPartsStrip` adds a matching dot + "A/N/D: ..." tooltip line to every legend chip
- **Sync drift indicator (Phase C)** — `RefreshSyncDriftIndicator` compares the active state's canonical transform against the other 9; shows "Synced" or "Drifted: n/9"
- **Folder import wizard (Phase C, P4)** — `OpenImportFolderWizard` opens a modal: pick a folder, Scan (`FindFilesRecursive` for png/jpg/jpeg/tga), part cards parsed from `{Part}_{View}_{Map}` names via `MatchStateSuffix`/`StripChannelSuffix` (full names before short codes), per-view×channel coverage preview with green/miss dots, Apply assigns to the active layer then closes. **P4: the folder wizard is THE one and only import entry point** — toolbar "Import Art...", the pinned quick-actions "Import Art…", the Art rail "Import Folder...", the Problems-panel Import, and clicking an artless schematic glyph all route here; the whole folder row is a **drop zone** (drag image files straight in — they are parsed exactly like a Scan), and the Art rail Import section also accepts file drops into the wizard
- **Display modes (Phase D)** — `SetDisplayMode` maps Textured/Depth/Wireframe/Split to the preview toggles; the mode row sits above the canvas (P5: superseded as the primary selector by the large Preview: segmented row; see Unified inspect mode)
- **Depth Debug knobs (Phase D)** — live sliders for the visualizer's `GridResolution`, `MeshSize`, `HeightScale`, `LocalOffset.Z` plus hex Low/High color edits and a Rebuild button that re-feeds the active slot's depth texture
- **Edge overlay + histogram (Phase D)** — `BuildEdgeOverlay` downsamples the active albedo, runs a Sobel edge pass (`EdgeDensity` static), and renders green edges over the preview; a 16-bin luminance histogram (`BuildLumaHistogram` static) draws as bars with density/mean stats
- **Hull Review (Phase D)** — orbit-3D auto-rotate/speed/snap controls plus a 2×5 thumbnail grid of every state's albedo for the active layer (click to jump)
- **Viseme Frames grid (Phase E)** — `RebuildVisemeGrid` renders the active layer's viseme timeline: one row per viseme (plus named visemes) with one cell per frame, filled cells = assigned art; clicking a filled cell plays that viseme; each row shows its fill percentage and the currently playing viseme is highlighted
- **Nested-art outliner (Phase E)** — `RebuildNestedOutliner` lists the active layer's nested elements with per-view visibility checkboxes, [Pin]/[Jiggle] badges, a Del button, and indented child rows; edits write back through `SetNestedElement`; the selected row is highlighted
- **Jiggle editor controls (Phase 7)** — the Nested Art / Pins pane adds a **Jiggle** checkbox plus Stiff / Damp / Imp / Mid / End Stiff / End Damp / End Imp sliders on the selected nested element (writes via `SetNestedJiggleEnabled`/`SetNestedJiggleSettings`); the sliders enable only when a nested element is selected and jiggle is on, and disable for whole-layer pins
- **Pin manager (Phase E, P3)** — the Nested Art / Pins section has an **[Elements] / [Pins]** pane switcher (`SetNestedPaneMode`); the Pins pane (`RebuildPinManager`) is the central pin overview the review asked for: the header reads **"Pins on `<Layer>`: N"**, and an **Add Pin** button creates a new element pinned at front-center (undo-scoped; canvas Pin Mode then places it, the outliner renames it). One row per pinned item (whole-layer pin, every pinned element, pinned children), each with a visibility toggle, jump-to-element (selects the element and flips back to its controls), and an Unpin button; a Copy row duplicates the selected pin to another element (pin data copied via `FFacePin3D`, undo-scoped). Rows are counted by `FPLayout::FPPinnedRowCount`
- **Pin controls (advanced, P3)** — element `<` `>` stepper (no more hardcoded element 0), live Pinned checkbox, then three clearly separated sub-sections: **PLACE** (Pin X/Y/Z as **0–100% of the layer's frame**, not raw −2..2 units), **PHYSICS** ("Rotate w/ view angle" checkbox + Min Rot / Max Rot / Sens sliders, plus a Min Scale slider), **MOTION** (jiggle checkbox + Stiff/Damp/Imp/Mid/End Stiff/End Damp/End Imp sliders, plus Idle Frame duration and Idle Speed sliders on nested elements); all refreshed by `RefreshPinControls()`; pins are also draggable directly on the canvas in Pin Mode (see above)
- **Param bindings table (Phase E)** — `RebuildParamTable` shows every binding of the active state/layer: editable param name, target cycle button (PosX→PosY→SclX→SclY→Rot→Blend), Invert checkbox, and X remove; the Add row above appends new bindings via `SetParamBindings`
- **Problems panel (Phase F)** — `RebuildProblemsPanel` validates all 10 states × every layer for missing albedo/normal/depth and warns on blink-frame and viseme-frame count mismatches across layers; issues are deduplicated (sorted-unique), color-coded (red error / amber warning), and clicking a row jumps to that state
- **Problems panel upgrades (Phase 4)** — the panel now has a quick-actions bar (rail jump chips + Import + Clear Stale), a Layout Group section that live-runs the manifest's `ValidateDesign(BuildSpec())` and lists any P1–P21 violations (green "Design contract OK" when clean), a rail-width control (`RailWidthPx`, 180–360 px via `FPLayout::ClampRailWidth`, default 180 — rebuilds the layout), an issue search box that filters rows live with a match count in the summary, and a summary line on the Problems accordion header (`SFaceAccordion::SetSectionSummary`)
- **Show Pins (Phase 3, P5)** — the **Canvas Options** overflow menu's "Show Pins" checkbox paints every nested-element marker on the gizmo color-coded: amber = static pin, cyan = rotation pin, purple = jiggle element, red ring = plain pivot anchor (unpinned); the selected pin handle stays amber as before
- **Sync drift indicator (Phase C)** — `RefreshSyncDriftIndicator` compares the active state's canonical transform against the other 9; shows "Synced" or "Drifted: n/9" (exact-equality semantics mirrored by `FPPinDriftCount`/`TestPinDriftMirror`); now wired into the Art rail's Quick Actions section and refreshed from `RefreshUI()`
- **Alignment tools (Phase B)** — canvas gizmo (`SFaceLayerGizmo`, drag body=move / corner=scale / top handle=rotate), onion-skin ghost of the adjacent state with opacity slider, and Cross-View Transform copy-from combo + Link checkbox (`ApplyCanonicalTransformWithLink`)
- **Thumbnail status matrix** — 10-state × N-layer grid of aspect-correct albedo thumbnails (pixel size + click-to-jump tooltip) replacing the old 22×20 ✓/× grid
- **Camera follows view** — view-strip clicks and matrix jumps snap orbit yaw/pitch to the state's zone center when `bCameraFollowsView` is enabled (default)
- **View override mode** — transform edits write `ViewOverrides` (per-rendered-view deltas) instead of canonical transforms when `bViewOverrideMode` is on
- **Silhouette → depth** — "Generate Depth from Outlines" extracts silhouette edges from rotation-view albedo textures, builds a visual-hull depth buffer, and bakes it into every layer's depth channel
- **Batch import by channel suffix** — imports auto-assign by filename suffix (`_N`/`_Normal`/`_normalmap` → Normal, `_D`/`_Depth`/`_height`/`_displacement` → Depth, else Albedo); every import path (wizard Scan/Apply, wizard drop zone) uses it when a layer is selected
- **Quick Actions** — rail-local batch ops: Apply to views... (the P2 picker: 10 views + All + copy-from-Front); the canonical batch actions (Auto-Fit All, Sync All→All, Clear All Overrides) live only in the pinned strip (P21)
- **Outline view management** — per-state checkboxes bound to the component's `OutlineViewStates` (All/None shortcuts); depth grid resolution is user-editable (8–256)
- **Outline → depth with scope + confirm** — "Generate Depth from Outlines" is arm/confirm-guarded (first click arms, second click commits; Detect Profile runs the combined flow unarmed) and offers a bake scope (Front only / 8 h-states / all 10). Each target view receives its own per-view visual-hull map computed in that view's camera frame instead of a verbatim copy of the front map, and the bake only touches the depth channel of the target states' layers
- **Toolbar search** — the filter box moved to the toolbar so it applies across all 6 property panels (right pane + 5 rails)
- **Rail accessibility (Phase 4b)** — the big-scroll remediation follow-up, all mirrored by `TestAccessibilityMirrors` (`FPLayout::RailSectionTitles` / `FindRailSectionByTitle` / `ConfigSummary` / `VisemeSummary` / `RailWidthAfterDrag` / `QuickActionLabels`):
  - **Progressive disclosure below section level** — the Diagnostics rail's 8-checkbox Config section and the Animated rail's Viseme Frames grid each collapse into a one-line summary ("K of 8 on" / "N viseme rows") behind a clickable `SFaceDisclosure` header; summaries update live from `RefreshConfigCheckboxes` / `RebuildVisemeGrid`
  - **Persistent quick-actions bar** — Import Art…, Sync All→All, Auto-Fit All, Clear All Overrides sit in a full-width pinned strip row between the zone diagram and the main row — never inside a scroll viewport (P21 `PinnedActionsNeverInScroll`: the manifest's `PinnedStrip` node holds exactly these four, flagged `bPinnedAction`; the Python validator flags any canonical label built in a rail or scrolled panel). Button set mirrors `FPLayout::QuickActionLabels()`
   - **Cross-rail search jump** — pressing Enter in the toolbar search runs `OnRailSearchCommitted`, finds the first section title containing the query across all 5 rails, switches rail, expands the accordion section, and scrolls it into view (`SScrollBox::ScrollDescendantIntoView`); a "no match" status message is shown otherwise
   - **Drag-resize rail** — `SFaceRailResizer` handle between the rail column and canvas: drag updates `RailWidthPx` live (clamped to `FPLayout::ClampRailWidth` 180–360 via `SetRailWidthLive`, no rebuild during the drag; commit rebuilds so the Diagnostics rail-width spinbox stays in sync)
  - **Section-jump chips + scroll indicator** — every rail has a pinned chip row above its scroll viewport (`BuildRailSectionChips`): one chip per registered section (`RegisterRailSection` / `RegisterAccordionSections`), click to jump (cross-rail jumps rebuild + auto-expand + scroll), the last-jumped chip highlights; the registry is the single source of truth for chips and search
- **Live numeric camera readouts** — Yaw/Pitch/Dist values shown next to the sliders and updated on refresh
- **Spatial part picking (Phase 1/2/4 → P1 one-map)** — the canvas click model was consolidated to ONE map: `SFaceSchematicLayer`'s 17 part glyphs are the only pick surface (the old hotspot-region outlines and the layer-art quad click layer are gone — no Alt/Ctrl modifier paths remain):
  - **One interaction model** — left-click a glyph (or its legend chip) resolves the part to a layer (`ResolveHotspotLayer`: the preset's persisted `HotspotLayerMap` first, then `FPLayout::FPHotspotLayerMatch` exact → plural → L/R-collapse → prefix derivation, so the default Eyes/Brows/Mouth/Hair layer set resolves EyeL→Eyes, BrowR→Brows, Mouth→Mouth out of the box), selects it, jumps to the Art rail with tweak controls open, and — when the layer has no art — opens the Import Folder Wizard preselected on that part; layers with art are selected for review only. Right-click (glyph or chip) opens the in-tool part→layer remap menu (persisted in the preset, "Auto (derived)" resets)
  - **Legend strip** — the chips under the canvas mirror the glyph map exactly (same 17-part set, same per-layer hue colors, dark gray = unmapped) with the visible `Part → Layer` mapping; a click on a chip does exactly what a click on its glyph does
  - **Breadcrumb + click pulse** — picking a part sets the 'Front → Eyes' breadcrumb (`SetBreadcrumb`; alias parts show `Front → Mouth (Teeth)`) beside the layer label and pulses the glyph bright for 0.5 s (`SchematicFlashPart`/`SchematicFlashTimestamp`, invalidated by `NativeTick`) — feedback at the point of action
  - **Per-view bounds from stored transforms** — `RefreshHotspotRegions` transforms each region's outline by its layer's effective transform for the active view state (`FPHotspotTransformRegion` mirrors the master material's UV chain: pivot subtract → art position → scale → pivot add → rotate), so outlines hug the art's real position/scale on Profile/Back/¾/Top/Bottom instead of a fixed front template; unmapped regions keep the template pose
  - **Cycle Preview (Phase 2)** — 8-second tour sequencing the live systems one at a time: blink 2s → expression 2s → viseme 2s → orbit sweep 2s
   - **Live Preview (Phase 4b)** — the assembled-result check: blink + Smile expression + re-triggered viseme (2.5s cadence) + continuous orbit sweep (8s period) all run TOGETHER; the two modes are mutually exclusive (starting one stops the other). Both mode machines are mirrored by `TestPreviewModesMirror` (`FPLayout::PreviewSystems` / `PreviewCyclePhaseDuration` / `LivePreviewVisemeCadence` / `LivePreviewOrbitPeriod` / `PreviewModeSystemFlags`)
- **Central-canvas redesign (schematic default view + interactivity + front/base/back yaw rules)** — the canvas shows the 17-part schematic as THE map (P1 one-map); glyphs are transformed by the layer's effective transform so the map hugs the rendered head:
  - **Part schematic source of truth** — `FaceParallaxSchematic.h` (pure C++17, synced into the editor module like the layout spec) defines 17 part glyphs (13 anatomical hotspot regions + Bangs/Hair/BackHair/Head) with `FPDepthClass` (Front/Base/Back) and the 10-layer tag table `FPTagClassForTag`; glyph fidelity was re-authored for recognizability (almond eyes, arc brows, nostrilled nose, lip-shaped mouth with cupid's bow, ear shells, cheek arcs, chin-rounded head silhouette, hair fringe + full silhouette + curtain); math mirror tests `TestSchematicParts`/`TestYawRule` assert the glyphs, classes, hit-testing, and rule formula
  - **Full part→layer coverage** — every one of the 17 parts resolves to a base-preset layer: `FPHotspotLayerMatch` derivation handles CheekL/R→Cheeks and EarL/R→Ears once those layers exist (plus EyeL→Eyes, BrowL→Brows), and `FPSchematicLayerAlias` covers the rest (Teeth→Mouth, Chin→Head, Neck→Head); `TestSchematicCoverage` pins all 17 resolutions
  - **Hair system (Phase 2)** — `FPHairLayerSet` (Bangs/Hair/BackHair) + `FPSchematicIsHairLayer` pin the hair contract: **Bangs = FRONT hair** (moves WITH yaw, amber), **Hair + BackHair = BACK hair** (move AGAINST yaw, cyan); every hair layer rides the normal per-layer pipeline (camera-sync, auto-fit, bulk-assign, nested pins, visibility, problems panel) — the set only declares which layers are hair and what class they carry (`TestHairSystem`)
  - **Art replaces the outline — but the map stays** — `RefreshSchematic` keeps EVERY glyph (P1 one-map); parts whose mapped layer has Front albedo render SOLID instead of dashed (`SetArtFlags` — the art replaces the outline on the live preview, the map stays complete and clickable); remaining glyphs are transformed by the layer's effective transform in the active view (`FPHotspotTransformPoint` chain), so the schematic hugs the same positions the assigned art will paint; unmapped parts keep the template pose
  - **The canvas is fully clickable (Phase 0 / P1)** — Slate routes mouse events to exactly one topmost hit-test-visible leaf plus its ancestors, so the old overlay stack had dead clicks (the gizmo SBox swallowed everything whenever a layer was selected, and nothing below it was ever reachable). Now the gizmo is **paint-only** (`EVisibility::SelfHitTestInvisible` on the SBox and the leaf), and `SFaceHotspotLayer` — the topmost interactive widget — is THE click router with one fixed order: **pin-drag (pin mode, moved out of the gizmo) → schematic glyph (left select/import, right remap) → miss** (misses are swallowed so clicks are never dead; the named-region and layer-art-quad click steps were deleted in P1). Hover and the Hand cursor are forwarded to `SFaceSchematicLayer` lens- and filter-aware, so what you see is exactly what you can click
  - **Click-to-assign** — one core (`SelectPartOrImport`) drives both the canvas glyph and the legend chip: resolves the part to its layer (derivation → alias), selects it, sets the breadcrumb, pulses the glyph; empty layers open the Import Folder Wizard preselected on that part; layers with art are selected for review only
  - **Selection emphasis (Phase 3)** — the selected layer's glyphs render at 3px with full alpha and a soft translucent fill halo while every other glyph dims to 25% alpha; hover still highlights at 2.5px
  - **Filter row (Phase 3)** — under the canvas mode row (toggled by the Canvas Options "Filter row" checkbox): a depth radio (All/Front/Base/Back), one toggle chip per base-preset layer (10 chips colored amber/grey/cyan by depth class), the **Focus** zoom, and **Clear**. The row is the pure mirror of `FPSchematicFilterAllows` (empty layer filter = all layers; depth radio 1/2/3; AND semantics) — the math tests pin the mirror
  - **Focus toggle (Phase 3)** — `ToggleSchematicFocus` zooms the selected layer's glyphs to fit: `RefreshSchematic` builds a lens (uniform scale clamped to [1,8], 90% fit margin) from the selected layer's view-transformed glyph bounds, and the schematic paints + hit-tests through the same lens (`SetFocus`/`FocusPoint`/`InverseFocusUV`)
  - **Base preset yaw rules** — the 10-layer base preset (`deploy.py` `LAYERS`, grouped Front/Base/Back with the hair layers commented) carries a per-layer `depth_class`; `SyncLayerDefinitionsFromPreset` consults `FPSchematic::FPYawRule::DepthScaleForTag`/`InvertsParallaxForTag` (Front = scale 1.0, no invert; Base = scale 0.15; Back = scale 1.0, inverted; max offset 5.0) for newly-created layers, mirroring `ComputeOffsetForState`
  - **Depth overlay checkbox** — the **Canvas Options** overflow's "Depth overlay" toggle (`ToggleDepthOverlay`/`BuildDepthOverlay`) composites the selected layer's depth map over the live preview at 55% opacity
  - **Group-colored edge map (Phase I)** — the part edge map is now group-colored **by default** (the canvas's default look): every glyph paints in its `FPEdgeGroup` color — eyes green, mouth red, hair violet, everything else grey-blue — with the depth class scaling the LUMINANCE so **front reads lighter than back** (`FPEdgeLuminanceForClass`: Front 1.0 > Base 0.72 > Back 0.45). The **hair system** is its own group with three detailed levels, each a distinct luminance step of the violet family (`FPHairLevelLuminance`: Bangs 1.0 > Hair 0.72 > BackHair 0.45 — level drives the brightness, the depth class never dims hair), so the three hair layers read as separate edges, all distinct from every other group. Edge-map outlines paint at full alpha (the legacy tint stays subdued), and a **legend strip under the canvas** keys every group + hair level to its color while the map is on. The **Canvas Options** overflow's **Edge map** checkbox (`SetSchematicEdgeMap`) reverts to the legacy depth-class tint (amber/grey/cyan), and **Hair edges** (`SetEdgeMapHairEdges`) hides the hair system's detailed edge levels wholesale while every other group stays (`FPEdgeMapShows`). Pure mirrors: `FPEdgeGroupForPartName`/`FPEdgeGroupForTag`/`FPEdgeColorForPart`/`FPHairLevelLuminance`, pinned by `TestEdgeMapMirrors`
  - **Drag-resize canvas + post-assign flash** — `SFaceCanvasResizer` below the canvas drags the preview height (220–900 px, clamped, default stays the 450 px design constant via a `HeightOverride` lambda — no manifest change); after any albedo assignment, `SetSlotTextures` arms a 1.5 s fading green pulse ring on the layer's canvas quad (`AssignFlashLayer`/`AssignFlashTimestamp`, invalidated by `NativeTick`)
- **Bulk Assign + Assign Ops (Phase P3, on the Art rail)** — a 10-state × 3-row bulk-assign grid, each cell colored by coverage (green = fully assigned, amber = partial, dark = empty; mirror `AssignCellState`) with a "Filled X/30" coverage label (`AssignCoverageText`); clicking a cell selects state+layer. Ops row: Clear Row (`ClearAllOverridesForSlot` across the row's states) and Apply to views... (the P2 picker). Below: Performance tier combo (Low/Medium/High → `MaxAsyncTextureCacheSize` via `PerformanceTierCacheSize`, grid 32/64/128 via `PerformanceTierGridSize`) and camera-source combo (`CameraSourceLabels`: PlayerCamera0/PlayerCamera1/SpecifiedActor/SequencerCamera/PreviewActor/Custom → `SetCameraSource`)
- **Per-axis sync (Phase P3)** — Cross-View Transform gains a "Sync axis" row (X / Y / SX / SY / R) calling `SyncLayerAxisToAllViews` → preset `SyncCanonicalAxisToAllViews`: only the chosen axis is rewritten across all 9 other views, untouched axes keep their per-view overrides (mirror `SyncAxisDelta`)
- **Drag-drop on every texture slot (P4)** — every visible texture slot accepts direct drops from the Content Browser or the OS: the SELECTED SLOT panel's three channel thumbs (drop targets assign to that channel), and the hull-review grid's per-state cells (drop assigns that state's albedo; clicking the cell still jumps to the state). `SFaceDropTarget` (Shared.h, one shared drop contract) routes asset drops (`FAssetDragDropOp`/`FContentBrowserDataDragDropOp`) and image-file drops (import + assign), both undo-scoped
- **Import completion (Phase P3)** — the folder wizard's Apply button appends a post-import coverage summary to the status line (`| albedo A/10, normal N/10, depth D/10` via `ImportCoverageSummary`)
- **Selection outline (Phase C)** — the selected layer's art quad is outlined in AccentBlue at 2px, **always visible** (not hover-only), and rebuilt from the active view state's stored transforms on every `RefreshUI` — so selection hugs the art in Profile/Back/¾/Top/Bottom too (the cross-view outline constraint). The quad hit-test/cycle click layer itself was removed in P1 (one map — the schematic glyph is the only pick surface); the `FPLayout::FPHitTopmostQuad`/`FPCycleQuadHit` helpers remain in the manifest library and stay covered by the math tests
- **Unified inspect mode (Phase C, P5)** — the canvas's segmented row is now a single **large, clearly-labeled 5-mode primary selector**: **Preview: Textured / Outline / Depth / Wireframe / Heatmap** (replaces the old Textured/Depth/Wireframe/Split display row; the legacy Split combo still works via the Config checks). **P5 demotion:** onion-skin (checkbox + opacity slider), Show Pins, Depth overlay and the Filter row moved out of the primary row into a collapsed **Canvas Options** overflow menu (`SMenuAnchor`, rebuilt fresh on every open so states always reflect the model; the manifest's `CN-ModeRow` mirrors the new 5-button + options row). The Diagnostics rail Config checks (Show Textures / Depth Mesh / Wireframe / Outline overlay / Color by Depth) stay the single source of truth: the row applies canonical combos (`SetInspectMode` → `FPLayout::InspectComboForMode`), and the highlight re-derives from the five toggles on refresh (`FPLayout::DeriveInspectMode`; any other combo — e.g. textures+depth — shows no highlight)
- **Grouped sync control + linked editing to chosen views (Phase D)** — the sync row is one grouped control: an explicit **Transform / Textures / Both** op selector (`FPLayout::SyncOpLabel`, `SyncOpHasTransform`/`SyncOpHasTextures`) plus **Sync → Selected** (applies the chosen op to the views picked on the state strip, via `SyncLayerToSelectedViews`/`SyncTexturesToSelectedViews`) and **Sync Both → All** (the canonical everything-everywhere action). The Link checkbox now broadcasts live edits to the *picked* views only (`FPLayout::FPLinkDestCount`/`FPLinkDestIsPicked` through `GetLinkTargetsForEditing`); with nothing picked it falls back to all 9 views — the Phase B contract
- **Thumbnail-first layer rows + completeness badge (Phase E)** — every layer-tree row now leads with an 18×14 albedo thumbnail of the layer in the active view (gray block when the view has none), followed by a green/amber/red completeness dot (Assigned = albedo+normal+depth, Partial, Missing — `FPLayout::AssignCellState`/`AssignCellLabel`, tooltip shows the exact A/N/D presence per state)
- **Undo/redo keyboard shortcuts (Phase F)** — the ring-buffer undo (`FWidgetUndoScope` → 32-entry stack, `Undo`/`Redo`/`CanUndo`/`CanRedo`) now answers **Ctrl+Z** (undo), **Ctrl+Shift+Z** and **Ctrl+Y** (redo) while the panel holds focus — the tab-activation hook hands focus to the widget (re-entrancy-guarded: `SetKeyboardFocus` inside `OnTabActivated` can re-trigger tab activation and recurse to a stack overflow, so the handler skips re-entrant calls via a shared flag), and the key table is mirrored by `FPLayout::UndoShortcutAction`; focus elsewhere leaves the editor's own global undo untouched
- **World-bound tab lifecycle** — the editor widget and its preview actor live in the current editor world; when that world is discarded (level switch, Live Coding compile), `FWorldDelegates::OnWorldCleanup` closes the tab and drops `EditorWidgetInstance` so the old world's package is GC-able (otherwise the engine fatals with "World Memory Leaks"). PIE world cleanups are ignored — the tab survives play sessions; reopening after a world change re-creates the widget against the new world
- **Undo/Redo toolbar buttons + History menu (P6)** — Undo/Redo sit in the toolbar next to Save (buttons 2/3, `TB-Undo`/`TB-Redo` in the Phase H manifest); the **History ▾** menu (`TB-History`) next to Redo opens the full undo/redo stack (click an entry to revert/re-apply to that point, 32-entry cap, `MaxUndoEntries`) plus Snapshot current state / Restore snapshot; the old single-slot "Snapshot" pair in the bottom bar was relabeled **Backup Point**/**Restore Backup** with an explanatory tooltip clarifying it is a manual safety copy, not multi-step undo. **Action-point flashes (P6)** — `SFaceFlashButton` (Shared.h) is a click-confirmation button that flashes green with a ✓ for ~0.7 s at the point of action: it backs **+ Add Layer**, **Add Pin**, the wizard's **Apply to Active Layer** (which now closes 0.6 s after the flash so the confirmation is seen), and the Param Bindings **Add** button; Clear State/Clear All require a confirming second click; "Log: ON/OFF" collapses the diagnostic log
- **Drag-and-drop texture thumbs (Phase A)** — each Albedo/Normal/Depth thumbnail is a drop target (`SFaceDropTarget`): dropping a Content Browser texture (legacy `FAssetDragDropOp` or `FContentBrowserDataDragDropOp`) assigns it directly to that channel (undo-scoped, auto-fit aware); dropping Explorer image files imports them via `ImportTexturesFromFiles` and assigns the ones whose name suffix matches that channel (`ChannelFromTextureName`), reporting "imported X, assigned Y to <channel>" in the status line
- **Preview FOV slider (Phase A)** — the Camera section now has a 10–90° FOV slider wired to `SetPreviewFOV`/`GetPreviewFOV` with a live readout
- **Duplicate nested element (Phase A)** — nested-outliner rows gain a Dup button that appends a copy of the row's element and selects it (undo-scoped); `DuplicateNestedElement` now supports append destinations (`DestIndex >= count`); `SyncTexturesLayerToAllViews` is BlueprintCallable
- **Dev tools relocated (Phase A)** — Tag Validator and Material Cross-Reference moved out of the bottom bar into the Diagnostics rail as accordion sections (`Sec-TagValidator`/`Sec-MatCrossRef` in the Phase H manifest); the bottom bar now holds only workflow actions

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
| `SyncTexturesToSelectedViews(State, LayerTag, DestViews)` | Phase D: textures-only variant — copies albedo/normal/depth to the picked destination views, leaving all canonical transforms untouched |
| `SyncLayerAxisToAllViews(State, LayerTag, Axis)` | Per-axis sync: rewrites only the chosen canonical axis (0 PosX, 1 PosY, 2 ScaleX, 3 ScaleY, 4 Rotation) across all views, preserving untouched axes; undo-scoped (mirrors `FPLayout::SyncAxisDelta`) |
| `SetCameraFollowsView(bEnabled)` / `GetCameraFollowsView()` | Enables/disables orbit camera snapping to zone centers on view switch; `bCameraFollowsView` property |
| `SnapCameraToActiveView()` | Snaps orbit yaw/pitch to the active state's zone center via `GetZoneCenterYaw/Pitch` |
| `ImportTexturesFromFiles(Files)` | Imports image files into `/Game/FaceParallax/Imported` via `FAssetToolsModule` and returns the created `UTexture2D`s |
| `AssignImageDropToSlot(State, Tag, DragEvent)` | Phase F shared drop pipeline: assigns all dropped Content-Browser/OS textures to the scoped slot by channel suffix (Normal/Depth else Albedo); returns whether any were assigned |
| `AssignImageDropToBlinkFrame(State, Tag, FrameIdx, DragEvent)` | Phase F shared drop pipeline: assigns dropped textures to one blink frame of the scoped slot by channel suffix to `SetBlinkFrameTextures` |
| `AssignTextureToSlot(Tex, State, LayerTag, Channel)` | Assigns a texture to Albedo/Normal/Depth of a slot, auto-fits if enabled, refreshes UI |
| `OpenImportArtDialog()` | Legacy single-file import dialog (superseded by the folder wizard in P4; kept as API — no UI entry points wire it) |
| `GenerateDepthFromOutlines(GridSize)` | Arm/confirm-guarded silhouette → depth bake: builds the visual-hull buffer and writes the depth channel of the scoped view states (per-view reprojection) |
| `SetOutlineDepthScope(Scope)` / `GetOutlineDepthScope()` | Bake scope: 0 = front only, 1 = 8 horizontal states, 2 = all 10 states |
| `GetOutlineDepthArmed()` | True between the arm click and the confirming click |
| `SetOutlineOverlayVisible(bVisible)` / `GetOutlineOverlayVisible()` | Toggles the depth-buffer overlay on the preview image |
| `SetOutlineViewEnabled(State, bEnabled)` | Adds or removes a state from `OutlineViewStates` (per-checkbox management; `SetOutlineViewState` only adds) |
| `SetActiveRailIndex(Index)` | Switches the left rail panel (0 View & Layer, 1 Art, 2 Nested & Animated, 3 Camera/Preview, 4 Diagnostics) |
| `FillMissingViewsFromActiveSlot()` | Copies the active slot's albedo (and other channels) to views that lack art; returns the number filled |
| `SetDisplayMode(Mode)` | 0 Textured, 1 Depth mesh, 2 Wireframe, 3 Split — maps to the preview toggles (legacy; the canvas row now uses `SetInspectMode`) |
| `SetInspectMode(Mode)` | Phase C: applies one canonical inspect combo (0 Textured, 1 Outline, 2 Depth, 3 Wireframe, 4 Depth Heatmap) to the five Config toggles; highlight re-derives from the toggles |
| `GetAdjacentState(State, Offset)` | Static wrap-around state offset (onion-skin ghost source) |
| `GetLinkTargets(Active)` | Static list of the 9 link-broadcast targets for a state (legacy all-views contract; `GetLinkTargetsForEditing` applies the Phase D pick set) |
| `CopyTransformFromView(Src, Dst)` | Copies a state's canonical transform to another, guarding src==dst |
| `ToggleOnionSkin(bEnable)` / `SetOnionSkinOpacity(Opacity)` | Shows/hides the adjacent-state ghost overlay on the canvas |
| `OpenImportFolderWizard()` | Opens the folder-scan import wizard (Phase C) |
| `BuildEdgeOverlay()` | Rebuilds the Sobel edge overlay + 16-bin histogram for the active slot's albedo (Phase D) |
| `BuildLumaHistogram(Luma, Grid, OutBins)` / `EdgeDensity(Luma, Grid, Threshold)` | Static pure helpers for histogram/edge metrics (unit-mirrored) |
| `RebuildVisemeGrid()` | Rebuilds the viseme frame grid (Phase E) — rows per viseme/named viseme, filled-frame cells, click to play |
| `RebuildNestedOutliner()` | Rebuilds the nested-element outliner (Phase E) — visibility checkboxes, badges, delete |
| `RebuildParamTable()` | Rebuilds the param-binding table (Phase E) — rename, cycle target, invert, remove |
| `RebuildProblemsPanel()` | Rebuilds the validation problems list (Phase F) — missing channels, frame-count mismatches, click-to-jump |
| `RebuildPinManager()` | Phase E: rebuilds the pin-manager pane — one row per pinned item (layer pin, elements, pinned children) with visibility toggle, jump, unpin, and copy-pin-to-element |
| `SetNestedPaneMode(Mode)` | Phase E: switches the Nested Art / Pins section pane — 0 = element controls, 1 = pin manager |
| `FrameFillRatio(Occupied)` / `ClampGridCols(MaxFrames)` / `AppendSortedUnique(Out, Line)` / `VisemeFramesMismatch(A, B)` | Static pure helpers for the timeline/problems systems (unit-mirrored) |
| `RefreshPinControls()` | Refreshes pin slider values, readouts, and Pinned/rotation checkbox state from the selected nested element |
| `SetNestedPinFromUV(State, LayerTag, Index, FromViewState, UV)` | Converts a canvas click to `Position3D` (Back-view clicks mirror the X axis) |
| `RegisterRailSection(RailIdx, Title, Target, Accordion?, AccordionIdx?)` | Registers a rail section for chips + search jump; accordion sections pass the accordion + index so jumps auto-expand |
| `RegisterAccordionSections(RailIdx, Accordion)` | Bulk-registers every accordion section in visual order (Art 2, Nested & Animated 3, Diagnostics 8) |
| `JumpToRailSection(RailIdx, SectionIdx)` | Switches rail if needed (pending jump consumed at rebuild end), expands the section, scrolls its header into view, highlights its chip |
| `OnRailSearchCommitted(Query)` | Enter-in-search handler: `FPLayout::FindRailSectionByTitle` across all rails → jump or status message |
| `UpdateDisclosureSummaries()` | Refreshes the Config disclosure "K of 8 on" summary from component/local toggle state |
| `GetRailWidthPx()` / `SetRailWidthLive(W)` / `ApplyRailWidthDelta(Delta)` | Rail width accessor, live clamped resize (width box only), and drag commit (clamp + rebuild) |
| `HandleHotspotClick(RegionName)` | Legend-chip pick (P1 one-map): resolves the region to a layer, selects it, opens the Art rail, sets the breadcrumb, pulses the glyph, and opens the Import Folder Wizard preselected on that part |
| `HandleSchematicPartClick(PartName)` | Canvas glyph pick (P1 router): identical to `HandleHotspotClick` — both delegate to `SelectPartOrImport` (one map, one interaction model) |
| `SelectPartOrImport(PartName)` | P1 one-map core: resolve part → layer (derivation → alias), select, Art rail, `Front → Eyes` breadcrumb (`SetBreadcrumb`), 0.5 s glyph click pulse, and Import Folder Wizard when the layer has no art |
| `ToggleSchematicLayerFilter(LayerTag)` / `SetSchematicDepthFilter(Depth)` / `ClearSchematicFilters()` | Phase 3: layer-chip multi-select toggle, depth radio (0 all / 1 Front / 2 Base / 3 Back), and full reset — each rebuilds the filter row and repaints the schematic |
| `ToggleSchematicFocus()` | Phase 3: zooms the selected layer's glyphs to fit (lens clamped to [1,8]); no-op without a selection |
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
3. **C++ math tests** — 1764 standalone tests (g++ from msys64 ucrt64), including silhouette-edge distance, visual-hull depth (front view + per-view yaw/pitch variant), camera-snap zone-center mapping, import channel-suffix detection, Phase B–G mirror suites (gizmo mapping both directions + round trip, link broadcast, suffix parser, sync drift, luminance histogram, Sobel edge density, fill ratio, grid columns, sorted-unique dedupe, frame-count mismatch, outline-depth bake quantization, depth-scope targeting), Phase C canvas selection + inspect mode (quad hit-test topmost/cycling, `DeriveInspectMode`/`InspectModeLabel`), Phase D sync integration (op labels/channels, link-target count and membership with the no-picks fallback), Phase E layer badges + pin manager + undo shortcuts (`AssignCellLabel` 2/1/0 mapping, `FPPinnedRowCount` layer/element/child row totals, `UndoShortcutAction` Ctrl+Z/Ctrl+Shift+Z/Ctrl+Y key table), pin view-angle rotation (mapping, clamping, wrapping, sensitivity, back-view authoring round-trip, slider normalization, 8-state projection sweep, effective-transform rotation accumulation, cross-view sync pin preservation), Phase H UI design contract (P1–P21 over the layout manifest: zero violations, the exact 6-rail scroll-viewport set (rails 180×560 clipped in nested horizontal+vertical SScrollBoxes), mirrored design constants, anchor-node presence, negative controls proving every principle fires, P14 props-pane right-edge gap and P15 scroll-content right inset, plus section slots auto-stacked so sections never paint over each other), and Phase I edge-map mirrors (part/tag group classification, hair detail levels, front-lighter-than-back luminance, pairwise-distinct group colors, hair toggle)
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

**Plugin self-install (fresh projects):** if the FaceParallax plugin is missing from the project entirely (fresh project, or a copy that lost the `Plugins\` folder), `deploy.py` installs it automatically — it writes `FaceParallax.uplugin`, the two module `Build.cs` files and the canonical sources (copied from the repo root) into `<Project>\Plugins\FaceParallax`, and enables the plugin in the `.uproject`. It then reports that the project must be built (`Tests\run_tests.ps1 -IncludeUEBuild` or `Build.bat`) and the editor restarted before deployment can complete. The install is idempotent and never overwrites existing plugin files.

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
- **Main-window vertical scroll** — the layout is designed at 840 px tall (Phase A slimmed the bottom bar to workflow actions only); when the docked tab is pinned/shortened below that, the widget's root scrolls vertically (the main-window container, the sole exemption to the panel scroll-bar ban), so the bottom sections (timeline, bottom bar, diagnostic log) stay reachable instead of being clipped invisible.
- **Auto-open on startup** — the subsystem opens the docked tab once per editor session (default ON). To disable, add to `Config/DefaultEditor.ini`: `[/Script/FaceParallaxEditor.FaceParallaxEditorSubsystem]` with `bAutoOpenEditorOnStartup=false`. The tab is only auto-opened once per process, so a Live Coding re-init never force-reopens a tab you closed.
- **`FaceParallaxOpenEditor` is a registered console command** (not just a `UFUNCTION(Exec)`) — the subsystem registers it with `IConsoleManager` at initialization, so it dispatches even if Live Coding has left the exec binding stale. If typing it still does nothing, restart the editor and run `deploy.py` again — the command is only registered by the freshly built module.

1. **Select a View State** — click one of the 10 state buttons (Front, 3/4R, ProR, etc.)
2. **Select a Layer** — click a layer name in the Layers panel
3. **Assign Textures** — use the texture slots in the Properties panel:
   - Click the **Albedo** thumbnail to pick a texture from the Content Browser
   - Click the **Normal** thumbnail to assign the normal map
   - Click the **Depth** thumbnail to assign the depth map
   - Each assignment auto-updates the preview

Alternatively, use the **Import Art...** button to batch-import texture files from disk (opens the Import Folder Wizard — the single import entry point).

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
FaceParallaxLayoutSpec.h                 — Phase H layout manifest + P1..P23 design validator
FaceParallaxEditorSubsystem.h/.cpp       — Editor subsystem (toolbar, tab; deployment is deploy.py)
deploy.py                                — THE deployment script: creates every binary asset in-editor
FaceParallaxModule.cpp                   — Runtime module entry (IMPLEMENT_MODULE)
AGENTS.md                                — Agent guide with rules and test info

Tests/
  ParallaxMathTests.cpp                  — 1764 standalone C++ tests
  SyntaxValidator.py                     — Python syntax validation
  run_tests.ps1                          — Test runner

SAMPLES/MyProject/                       — Standalone UE5 project (plugin copy) for CI builds
```
