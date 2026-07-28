# FaceParallax — 2D Face Parallax System for Unreal Engine

Camera-driven 2D face rendering with multi-layer parallax, depth map support, view-state transitions, real-time depth debug visualizer, preset asset system, and preview actor for an in-editor visual editor.

---

## Overview

This system lets you render a 2D character face that responds to camera angle — switching between 10 view states (Front, 3/4 Left/Right, Profile Left/Right, Back Left/Right, Back, Top, Bottom) with smooth crossfades. Each state can use its own texture set (Albedo, Normal, Depth) driven by Blueprint or automated via the **Preset** system. Multi-layer parallax offsets are computed per-layer based on camera deviation and depth scale.

**Key features:**

- **Angle-driven state machine** — detects camera position relative to the head bone and selects the correct 2D view
- **Multi-layer parallax** — each art layer moves at its own rate based on depth scale
- **Vertical parallax** — Top/Bottom views produce Y-axis parallax offset
- **Continuous blending** — smooth crossfade at zone boundaries, not hard state snaps
- **Hysteresis** — prevents flickering at zone edges
- **Material-driven depth** — pushes parallax offsets and blend alpha to material instances for shader-based depth effects
- **Preset system** — `UFaceParallaxPreset` DataAsset stores texture assignments per `(ViewState × LayerTag)`
- **Auto texture swap** — when a preset is active, textures swap automatically on state change (no Blueprint logic needed)
- **Depth Debug Visualizer** — toggleable procedural 3D mesh generated from the current depth map
- **Preview Actor** — `AFaceParallaxPreviewActor` with scene capture for in-editor 3D preview

---

## Architecture

### File Map

| File | Type | Purpose |
|---|---|---|
| `FaceParallaxTypes.h` | Shared types | `EFaceAngleState` enum, `FFaceTextureSet`, `FFaceViewStateLayerSet`, `FFaceArtTransform`, `FFaceArtSlot` structs |
| `FaceParallaxComponent.h/.cpp` | Core component | State machine, parallax offsets, material parameters, preset application, art transform pushing |
| `FaceParallaxPreset.h/.cpp` | DataAsset | Stores texture + transform mappings per view state and layer. Created from Content Browser. |
| `DepthDebugVisualizerComponent.h/.cpp` | Debug component | Procedural mesh from depth map, wireframe, color-by-depth |
| `FaceParallaxPreviewActor.h/.cpp` | Preview actor | Skeletal mesh + scene capture + orbit controls + per-part transform access |
| `FaceParallaxEditorWidget.h/.cpp` | Editor Widget | C++ `UUserWidget` subclass providing bindable functions for every setting across 10 categories |

### Component Roles

| Component | Purpose |
|---|---|
| `UFaceParallaxComponent` | Core component. Computes camera-to-head angle, manages view state machine, calculates parallax offsets per layer, drives material parameters, applies preset textures. |
| `UDepthDebugVisualizerComponent` | Optional debug tool. Reads the current depth map texture, builds a uniform-grid procedural mesh with Z = depth value, colorized by height. Toggleable in-game and in-editor. |
| `UFaceParallaxPreset` | Data asset. Holds a `TMap<EFaceAngleState, FFaceViewStateLayerSet>` — one texture set per view state, with sub-keys per layer tag. |
| `AFaceParallaxPreviewActor` | Editor/runtime preview actor. Hosts the mesh, parallax component, depth debug, and a scene capture camera with orbit controls. Used by the Editor Widget. |
| `UFaceParallaxEditorWidget` | `UUserWidget` subclass with bindable Blueprint functions for every editor setting — transform sliders, view overrides, auto-fit, sync, camera, debug toggles, material params, status. |

### Module

The API macro is `FACEPARALLAX_API`. Include the module in your project's `Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore"
});
PrivateDependencyModuleNames.AddRange(new string[] {
    "ProceduralMeshComponent",   // required for DepthDebugVisualizerComponent
    "Blutility"                  // required for UFaceParallaxEditorWidget if used as Editor Utility Widget base
});
```

Also add `"ProceduralMeshComponent"` to your `.uproject` plugin list if it isn't there already:

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
                │  -157.5  -112.5  -67.5  -22.5  22.5  67.5  │
                │ BL      LP      3QL    Front  3QR   RP   BR │
                │←── 45° zones ──────────────────────────────→│
                └─────────────────────────────────────────────┘
```

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
| `StateBlendAlpha` | Scalar | 0→1 crossfade between previous and current state. Material should lerp between `AlbedoTexturePrev/NormalTexturePrev/DepthTexturePrev` (previous state) and the non-prev counterparts (current state). |
| `ParallaxOffset` | Vector4 | (OffsetX, OffsetY, 0, 0) — per-layer parallax shift |
| `DepthIntensity` | Scalar | Global depth map intensity multiplier |
| `DebugDepth` | Scalar | 0 = normal rendering, 1 = show depth as heat map |
| `IsTopDown` | Scalar | 1 when in Top or Bottom state (for shader branching) |
| `IsTopView` | Scalar | 1 when in Top state specifically |
| `ArtPosition` | Vector4 | (PosX, PosY, 0, 0) — per-art-piece UV position offset (canonical + override + optional yaw-driven dynamic offset) |
| `ArtScale` | Vector4 | (ScaleX, ScaleY, 0, 0) — per-art-piece UV scale |
| `ArtRotation` | Scalar | Rotation in degrees — per-art-piece UV rotation |
| `AlbedoTexturePrev` | Texture2D | Previous state albedo (for crossfade). Material lerps between this and `AlbedoTexture` using `StateBlendAlpha`. |
| `NormalTexturePrev` | Texture2D | Previous state normal map |
| `DepthTexturePrev` | Texture2D | Previous state depth map |
| `ExpressionBlendAlpha` | Scalar | 0→1 crossfade between previous and current expression. Material should lerp `Expression*Prev` ↔ main params. |
| `ExpressionAlbedoPrev` | Texture2D | Previous expression albedo |
| `ExpressionNormalPrev` | Texture2D | Previous expression normal map |
| `ExpressionDepthPrev` | Texture2D | Previous expression depth map |

### Viseme System (Speech Mouth Shapes)

Each `FFaceArtSlot` stores per-expression, per-viseme animation frame sequences. Visemes represent mouth shapes for different phonemes (speech sounds). When a viseme is triggered, the component plays through the frame sequence as a flipbook animation.

**Supported visemes:**

| Value | Display Name |
|---|---|
| `Uh` | Uh |
| `Ah` | Ah |
| `Ee` | Ee |
| `D` | D |
| `S` | S |
| `F` | F |
| `M` | M |
| `L` | L |
| `WOO` | WO-o |
| `Oh` | Oh |
| `R` | R |

**Properties:**

| Property | Default | Description |
|---|---|---|
| `bVisemeEnabled` | true | Enable viseme playback |
| `VisemeFrameDuration` | 0.04s | Seconds per viseme frame |

**Data structure:** `Slot.VisemeFrameSets[Expression][Viseme] = TArray<FFaceTextureSet>`

Each expression (Neutral, Smile, Frown) can have its own viseme frame set for each of the 11 visemes. This allows different mouth shapes depending on the character's expression — e.g., a smile-Ah looks different from a frown-Ah.

**Behavior:**
- Call `PlayViseme(EViseme::Ah)` to start a viseme animation
- The component plays through the frame sequence at `VisemeFrameDuration` speed
- When all frames are exhausted, the viseme auto-completes and mouth returns to the expression-default texture
- Call `StopViseme()` to interrupt playback early
- Events: `OnVisemeStarted`, `OnVisemeCompleted` (both broadcast `CurrentState, PreviousState`)
- Viseme texture frames override the main texture params (`AlbedoTexture`/etc.) while playing, similar to blink

**Usage from Blueprint:**
```
// Play the "Ah" viseme on the current expression
PlayViseme(EViseme::Ah)

// Check if currently playing
IsVisemePlaying()

// Stop early
StopViseme()
```

### Dual-Texture Crossfade

During view state transitions, the component keeps both texture sets alive:
- **Current state** textures on `AlbedoTexture` / `NormalTexture` / `DepthTexture`
- **Previous state** textures on `AlbedoTexturePrev` / `NormalTexturePrev` / `DepthTexturePrev`

The `StateBlendAlpha` parameter ramps from 0→1. Your material should use it as a lerp factor:
```
Albedo = lerp(AlbedoTexturePrev, AlbedoTexture, StateBlendAlpha)
```

This enables smooth crossfades between eye directions, not just fade-ins of the new art.

### Blink Animation

Each `FFaceArtSlot` stores an ordered `TArray<FFaceTextureSet> BlinkFrames`. The component plays through these frames sequentially at a configurable rate, allowing multi-frame blink animations (e.g., open → slightly closed → half → closed → pushed → closed → half → open).

**Properties:**

| Property | Default | Description |
|---|---|---|
| `bBlinkingEnabled` | true | Enable automatic blinking |
| `BlinkIntervalMin` | 3.0s | Minimum seconds between blinks |
| `BlinkIntervalMax` | 7.0s | Maximum seconds between blinks |
| `BlinkFrameDuration` | 0.03s | Seconds per blink frame |

**Behavior:**
- Blinks occur at random intervals between `BlinkIntervalMin` and `BlinkIntervalMax`
- Each frame is shown for `BlinkFrameDuration` seconds
- When blinking, current texture params (`AlbedoTexture`/`NormalTexture`/`DepthTexture`) are overridden with the current blink frame
- Call `ForceBlink()` to trigger a blink immediately
- Events: `OnBlinkStarted`, `OnBlinkCompleted` (both broadcast `CurrentState, PreviousState`)

### Expression System

Each `FFaceArtSlot` stores expression-specific texture variants in `TMap<EExpression, FFaceTextureSet> ExpressionTextures`. Changing the expression triggers a smooth crossfade controlled by the material parameter `ExpressionBlendAlpha`.

**Supported expressions:**

| Value | Display Name |
|---|---|
| `Neutral` | Neutral |
| `Smile` | Smile |
| `Frown` | Frown |

**Properties:**

| Property | Default | Description |
|---|---|---|
| `CurrentExpression` | Neutral | Active expression |
| `ExpressionCrossfadeDuration` | 0.3s | Transition duration in seconds |
| `ExpressionBlendParamName` | `"ExpressionBlendAlpha"` | Scalar for expression crossfade |
| `ExpressionAlbedoPrevParamName` | `"ExpressionAlbedoPrev"` | Previous expression albedo texture |
| `ExpressionNormalPrevParamName` | `"ExpressionNormalPrev"` | Previous expression normal map |
| `ExpressionDepthPrevParamName` | `"ExpressionDepthPrev"` | Previous expression depth map |

**Behavior:**
- When `SetExpression(NewExpression)` is called, current textures are captured into `Expression*Prev` params
- New expression textures are loaded into the main texture params (`AlbedoTexture`/etc.)
- `ExpressionBlendAlpha` ramps from 0→1 over `ExpressionCrossfadeDuration`
- Material should lerp between `Expression*Prev` and main params using `ExpressionBlendAlpha`
- If no expression textures exist for the current expression, falls back to the slot's base `Textures`
- Expression variants can be assigned per-state × per-layer, just like base textures

### Dynamic Art Offset (Eye Tracking)

When `bDriveArtPositionFromYaw` is enabled on the component, the `ArtPosition` is automatically offset by the yaw/pitch deviation within the current view zone (clamped by `MaxYawArtOffset`). This creates the illusion that the eyes track the camera without needing sub-zone texture variants:

- **Horizontal states** (Front, Profile, etc.): `ArtPosition.X` offset by normalized yaw deviation
- **Vertical states** (Top, Bottom): `ArtPosition.Y` offset by normalized pitch deviation

```
Horizontal: ArtPosition.X = Canonical.X + (YawDeviation / HalfZoneWidth) × MaxYawArtOffset
Vertical:   ArtPosition.Y = Canonical.Y + (PitchDeviation / HalfZoneWidth) × MaxYawArtOffset
```

### Material Texture Parameters (Preset System)

When a preset is active, the component sets these texture parameters on each layer's material instances, using the `AlbedoParamName`, `NormalParamName`, and `DepthParamName` properties:

| Property | Default | Material Parameter |
|---|---|---|
| `AlbedoParamName` | `"AlbedoTexture"` | Texture2DParameter |
| `NormalParamName` | `"NormalTexture"` | Texture2DParameter |
| `DepthParamName` | `"DepthTexture"` | Texture2DParameter |

Each material used by a layer should expose these as texture parameters so the preset system can drive them automatically.

---

## Preset System

The `UFaceParallaxPreset` DataAsset stores all texture assignments for a character face as a map:

```
Preset → {
    Front  → {
        "Eyes"  → { Albedo: T_Front_Eyes_A,  Normal: T_Front_Eyes_N,  Depth: T_Front_Eyes_D },
        "Hair"  → { Albedo: T_Front_Hair_A,  Normal: T_Front_Hair_N,  Depth: T_Front_Hair_D },
        "Skin"  → { Albedo: T_Front_Skin_A,  Normal: T_Front_Skin_N,  Depth: T_Front_Skin_D },
    },
    RightProfile → {
        "Eyes"  → { Albedo: T_Profile_Eyes_A, Normal: T_Profile_Eyes_N, Depth: T_Profile_Eyes_D },
        ...
    },
    ...
}
```

### Creating a Preset

1. In the Content Browser, right-click → **Miscellaneous → DataAsset**.
2. Pick `UFaceParallaxPreset` as the asset class.
3. Name it (e.g., `PA_MyCharacter_Face`).
4. Open it and populate the `ViewAssignments` map — one entry per view state, with layers matching your component's `LayerDefinitions`.

### Using a Preset

Assign the preset to the `ActivePreset` property on `UFaceParallaxComponent` in your character Blueprint. When `bAutoApplyPreset` is true (default), textures swap automatically when the view state changes. No Blueprint event binding is needed.

You can also apply a preset manually at any time:

```cpp
FaceParallaxComponent->ApplyPreset(MyPreset);
// or per-state:
FaceParallaxComponent->SetStateTextures(EFaceAngleState::Front);
```

### Preset API

| Method | Description |
|---|---|
| `GetSlot(State, LayerTag)` | Returns the full `FFaceArtSlot` (textures + transforms) |
| `SetSlot(State, LayerTag, Slot)` | Assigns an entire slot |
| `GetTexturesForSlot(State, LayerTag)` | Returns the `FFaceTextureSet` for a given state and layer |
| `SetTexturesForSlot(State, LayerTag, Textures)` | Assigns textures to a slot (auto-fits if enabled) |
| `GetEffectiveTransform(State, LayerTag)` | Returns the combined (canonical + view-override) transform for the current view |
| `SetCanonicalTransform(State, LayerTag, Transform)` | Sets the canonical (default) transform for a slot |
| `SetViewOverride(State, LayerTag, OverrideView, Transform)` | Adds a view-specific transform override |
| `HasViewOverride(State, LayerTag, OverrideView)` | Checks for a view override |
| `ClearViewOverride(State, LayerTag, OverrideView)` | Removes a specific override |
| `ClearAllOverridesForSlot(State, LayerTag)` | Removes all overrides for a slot |
| `ClearAllOverrides()` | Removes all overrides in the entire preset |
| `ComputeAutoFitTransform(Textures)` | Computes a uniform scale to fit texture to `CanvasSize` |
| `ApplyAutoFitToSlot(State, LayerTag)` | Auto-fits a slot's canonical transform |
| `SyncCanonicalToAllViews(State, LayerTag)` | Copies a slot's canonical transform to same-named slots in all other views |
| `HasState(State)` | Whether the preset has any assignments for a given state |
| `HasSlot(State, LayerTag)` | Whether a specific slot is assigned |
| `GetAssignedStates()` | Array of states that have any assignments |
| `GetTotalAssignedSlots()` | Count of all assigned `(State, LayerTag)` pairs |
| `ClearState(State)` | Remove all assignments for a state |
| `ClearAll()` | Remove all assignments |

---

## Depth Debug Visualizer

The `UDepthDebugVisualizerComponent` creates a procedural 3D mesh from the current view state's depth map texture. This lets you see how the depth data is interpreted — a rough relief of the face geometry derived purely from the 2D depth map.

### What it shows

- A uniform grid mesh (default 48×48 vertices)
- Z-axis displacement = depth map value × `HeightScale`
- Vertex colors: blue (deep/far) → red (shallow/near) gradient
- Positions itself at the head bone location with a configurable local offset

### Controls

| Property | Default | Description |
|---|---|---|
| `bStartEnabled` | false | Enable on BeginPlay |
| `GridResolution` | 48 | Grid vertices per axis (8–256) |
| `MeshSize` | 30.0 | World-space size of the debug quad |
| `HeightScale` | 10.0 | Amplification of depth values |
| `LocalOffset` | (0,0,25) | Position relative to owner root |
| `bShowWireframe` | false | Wireframe overlay |
| `bUseVertexColors` | true | Color mesh by depth |
| `LowColor` / `HighColor` | Blue / Red | Gradient endpoints |

### Usage

1. Add the component to your character actor.
2. When the view state changes, call `RebuildMeshFromDepthMap(YourDepthMapTexture)`.
3. Toggle visibility with `ToggleVisualizer()` or `SetVisualizerEnabled(bool)`.

### How the depth map is read

The component reads the texture's **source data** (the original imported pixel data, not GPU-compressed):
- Works in editor and PIE
- In packaged builds, ensure textures have **TC_Editor** or **TC_VectorDisplacementmap** compression
- For reliable runtime reading, use a render target approach or set compression to `TC_HDR`

---

## Preview Actor

The `AFaceParallaxPreviewActor` provides a self-contained, orbit-controlled preview of the character face. It is used by the Editor Utility Widget but can also be spawned at runtime.

### Components

| Component | Purpose |
|---|---|
| `PreviewRoot` | Scene component at origin |
| `PreviewMesh` | Skeletal mesh of the character head |
| `FaceParallax` | Core parallax component (preset-ready) |
| `DepthDebug` | Debug visualizer (toggleable) |
| `SceneCapture` | Captures the preview to a render target |

### Camera Controls

| Method | Range | Description |
|---|---|---|
| `SetOrbitYaw(deg)` | 0–360 | Horizontal orbit angle |
| `SetOrbitPitch(deg)` | -89–89 | Vertical orbit angle |
| `SetOrbitDistance(dist)` | 10+ | Camera distance from head |
| `SetPreviewFOV(fov)` | 1–160 | Camera field of view |
| `ResetCamera()` | — | Restore default orbit |
| `SetAutoRotate(bool)` | — | Continuous yaw rotation |

### Preview Controls

| Method | Description |
|---|---|
| `ShowTextures(bool)` | Show/hide the textured mesh |
| `ShowDepthMesh(bool)` | Toggle depth debug mesh |
| `ShowWireframe(bool)` | Toggle wireframe on debug mesh |
| `ColorByDepth(bool)` | Toggle depth-color mode |
| `ApplyPreset(Preset)` | Apply a preset to the preview |
| `AssignSkeletalMesh(Mesh)` | Set the skeletal mesh |
| `SetRenderTarget(RT)` | Set the scene capture target |
| `GetEffectivePartTransform(State, LayerTag)` | Returns the combined canonical + override transform for a part in a given view |
| `GetPartSourceSize(State, LayerTag)` | Returns the source pixel dimensions (albedo width/height) of a part |
| `RefreshPreview()` | Re-applies the current preset to refresh textures and transforms |

---

## Editor Utility Widget (Blueprint Setup)

The editor tool is built as an **Editor Utility Widget** (`.uasset` Blueprint) that uses the C++ systems above. It provides an RPG-style character menu interface for browsing, assigning, and verifying face art.

### UI Layout

```
┌─────────────────────────────────────────────────────────────────────┐
│ [Preset: PA_MyCharacter]  [Save] [Save As] [New Preset]            │
├─────────────────────────┬───────────────────────────────────────────┤
│ VIEW CATEGORY TABS      │  3D PREVIEW (from Scene Capture)         │
│ [Front][3/4R][ProR][Bk] │                                           │
│ [3/4L][ProL][Top][Bot]  │       ┌───────────────────────┐          │
│                          │       │                       │          │
│ ART PIECES BY LAYER     │       │   Character Preview   │          │
│ ┌──────────────────┐    │       │                       │          │
│ │ Foreground       │    │       └───────────────────────┘          │
│ │ ☑ Eyes: [thumb] │    │                                           │
│ │ ☑ Nose: [thumb] │    │  CONTROLS                                │
│ │ ☑ Mouth:[thumb] │    │  ┌─Yaw:   ◄══════════► 45°──┐            │
│ │──────────────────│    │  ├─Pitch: ◄══════════► 15°──┤            │
│ │ Midground        │    │  ├─Zoom:  ◄═══►────────────┤            │
│ │ ☑ Hair: [thumb] │    │  ├─☐ Auto-rotate           │            │
│ │ ☑ Ears: [thumb] │    │  └──────────────────────────┘            │
│ │──────────────────│    │                                           │
│ │ Background       │    │  OVERLAYS                                │
│ │ ☑ Outline:[thmb]│    │  ┌─☑ Show Textures                      │
│ └──────────────────┘    │  ├─☐ Show Depth Mesh                    │
│                          │  ├─☐ Wireframe                          │
│ SLOT DETAILS             │  ├─☐ Color by Depth                     │
│ Albedo: T_Front_Eyes    │  └──────────────────────────────────────┘│
│ Normal: T_Front_Eyes_N  │                                           │
│ Depth:  T_Front_Eyes_D  │                                           │
│ [Assign...] [Clear]     │                                           │
├─────────────────────────┴───────────────────────────────────────────┤
│ Status: 8/10 states assigned | 3/4 layers active | 24 total slots  │
└─────────────────────────────────────────────────────────────────────┘
```

### Creating the EUW

1. Right-click in Content Browser → **Editor Utilities → Editor Utility Widget**.
2. Name it `EUW_FaceParallaxEditor`.
3. Add a `UFaceParallaxPreset` property to the widget Blueprint.
4. In the widget's `Construct` event, spawn an `AFaceParallaxPreviewActor` in the current editor world.
5. Connect the preview actor's render target to an `Image` widget for the 3D preview.
6. Build the category tabs, layer lists, and assignment buttons.
7. Wire the controls to the preview actor's camera and debug methods.

### Widget API Reference

The `UFaceParallaxEditorWidget` C++ class exposes every setting as a bindable Blueprint function, organized into 13 categories:

| Category | Functions | Maps To |
|---|---|---|
| **Preset** | `ApplyPresetToPreview`, `CreateNewPreset`, `SavePreset`, `SetCanvasSize`, `GetCanvasSize`, `SetAutoFitOnAssign`, `GetAutoFitOnAssign` | `UFaceParallaxPreset` on `ActivePreset` |
| **View State** | `SetActiveViewState`, `GetActiveViewState`, `GetAssignedStates`, `HasState`, `GetLayerTagsForState`, `GetLayerCount` | Active tab selection, state enumeration |
| **Transform** | `GetLayerCanonicalTransform`, `SetLayerPosition`, `SetLayerScale`, `SetLayerRotation`, `SetLayerTransform`, `ResetLayerTransform`, `GetEffectiveLayerTransform`, `ApplyAutoFit`, `ApplyAutoFitToAllSlots`, `SyncLayerToAllViews`, `SyncAllLayersToAllViews` | Per-layer sliders → `FFaceArtTransform` on `ActivePreset` |
| **View Override** | `HasViewOverride`, `GetViewOverride`, `SetViewOverride`, `ClearViewOverride`, `ClearAllOverridesForSlot`, `ClearAllOverrides`, `GetOverrideViewsForSlot` | Per-state transform overrides on `FFaceArtSlot` |
| **Textures** | `GetSlotTextures`, `SetSlotTextures`, `GetSlotSourceSize`, `GetSlotAlbedo`, `GetSlotDepth` | `FFaceTextureSet` on preset slots |
| **Camera** | `SetOrbitYaw`, `GetOrbitYaw`, `SetOrbitPitch`, `GetOrbitPitch`, `SetOrbitDistance`, `GetOrbitDistance`, `SetPreviewFOV`, `GetPreviewFOV`, `SetAutoRotate`, `GetAutoRotate`, `SetAutoRotateSpeed`, `GetAutoRotateSpeed`, `ResetCamera` | Preview actor camera controls |
| **Debug Overlays** | `ShowTextures`, `ShowDepthMesh`, `ShowWireframe`, `ColorByDepth` | Preview actor + depth debug toggles |
| **Status** | `GetAssignedStateCount`, `GetTotalAssignedSlots`, `GetActiveLayerCount`, `GetStatusString` | Preset slot queries |
| **Dynamic Art** | `SetDriveArtPositionFromYaw`, `GetDriveArtPositionFromYaw`, `SetMaxYawArtOffset`, `GetMaxYawArtOffset` | Eye tracking via yaw-driven ArtPosition X |
| **Blink** | `SetBlinkingEnabled`, `GetBlinkingEnabled`, `SetBlinkInterval`, `GetBlinkIntervalMin`, `GetBlinkIntervalMax`, `ForceBlink`, `IsBlinking`, `SetBlinkFrameDuration`, `GetBlinkFrameDuration`, `GetBlinkFrameCount`, `SetBlinkFrameTextures`, `GetBlinkFrameTextures` | Blink animation toggle, timing, frame assignment |
| **Expression** | `SetExpression`, `GetExpression`, `SetExpressionCrossfadeDuration`, `GetExpressionCrossfadeDuration`, `IsExpressionTransitioning`, `SetExpressionTextures`, `GetExpressionTextures`, `HasExpressionTextures`, `GetAssignedExpressions`, `GetExpressionBlendParamName`, `GetExpressionAlbedoPrevParamName`, `GetExpressionNormalPrevParamName`, `GetExpressionDepthPrevParamName` | Expression crossfade, texture assignment per expression |
| **Viseme** | `SetVisemeEnabled`, `GetVisemeEnabled`, `PlayViseme`, `StopViseme`, `IsVisemePlaying`, `GetCurrentViseme`, `SetVisemeFrameDuration`, `GetVisemeFrameDuration`, `GetVisemeFrameCount`, `SetVisemeFrameTextures`, `GetVisemeFrameTextures`, `GetAssignedVisemes` | Speech mouth shape animation per expression × viseme |
| **Material Params** | `GetAlbedoParamName`, `GetNormalParamName`, `GetDepthParamName`, `GetAlbedoPrevParamName`, `GetNormalPrevParamName`, `GetDepthPrevParamName`, `GetArtPositionParamName`, `GetArtScaleParamName`, `GetArtRotationParamName` | Component material parameter names (dual-texture crossfade + transform) |
| **Query** | `HasSlot`, `IsSlotFullyAssigned`, `ClearState`, `ClearAll` | Preset slot state queries |

### Creating the EUW Blueprint

1. Create a Blueprint class derived from `UFaceParallaxEditorWidget` (or from `UEditorUtilityWidget` with the C++ class as a parent).
2. In the widget's `Construct` event, spawn an `AFaceParallaxPreviewActor` and assign it to the `PreviewActor` property.
3. Assign a `UFaceParallaxPreset` DataAsset to the `ActivePreset` property.
4. Build the UI layout matching the diagram above, calling the widget's functions from Blueprint nodes.

**All function categories are BlueprintCallable** — search for "Face Editor" in the Blueprint picker.

### Prerequisites for the EUW

- `Blutility` plugin enabled (for Editor Utility Widgets) — add to `Build.cs` if using the C++ widget as a base
- `Editor Scripting Utilities` plugin enabled (for asset operations)
- `Python Editor Scripting Plugin` (optional, for advanced automation)
- The `AFaceParallaxPreviewActor` must be spawnable (place a Blueprint subclass or spawn from class)

---

## Integration Walkthrough

### 1. Add component to character

In your character Blueprint:
- Add `UFaceParallaxComponent`
- Add `UDepthDebugVisualizerComponent` (optional)

### 2. Configure Skeletal Mesh

Set `HeadBoneName` to the socket/bone name where the face is located (e.g., "head").

### 3. Tag your face layer primitives

Each flat plane / quad that renders a face layer needs a component tag matching a `FFaceLayerDef.LayerTag` entry (default: `"FaceLayer"`).

### 4. Set up materials

Create a master material that uses these parameters:
- `StateBlendAlpha` → lerp between texture sets
- `ParallaxOffset` → UV offset for parallax
- `DepthIntensity` → POM or bump offset intensity
- `AlbedoTexture` → Texture2DParameter (for preset system)
- `NormalTexture` → Texture2DParameter (for preset system)
- `DepthTexture` → Texture2DParameter (for preset system)
- `DebugDepth` → switch output to depth heat map (optional)

Use material instances per state with different Albedo/Normal/Depth textures.

### 5. Create and assign a preset

1. Create a `UFaceParallaxPreset` DataAsset in the Content Browser.
2. Populate it with texture assignments for each view state and layer.
3. Assign it to your character's `UFaceParallaxComponent.ActivePreset`.

The component handles texture swapping automatically. No Blueprint scripting is required for basic operation.

### 6. (Optional) Handle advanced logic in Blueprint

Bind to `OnFaceStateChanged` for custom effects, sounds, or non-texture state reactions.

### 7. (Optional) Set up the debug visualizer

Bind a keyboard input (e.g., `V` key) to `ToggleVisualizer()`. Call `RebuildMeshFromDepthMap()` in the `OnFaceStateChanged` event.

### 8. (Optional) Set up the Editor Utility Widget

Create the EUW as described above to provide a visual editor for browsing and assigning art pieces.

---

## Material Setup Example

```
                            ┌──────────────────────┐
                            │  Master Material      │
                            │  (Unlit or Lit)       │
                            │                       │
                            │  Parameters:          │
                            │  - StateBlendAlpha    │
                            │  - ParallaxOffset     │
                            │  - DepthIntensity     │
                            │  - AlbedoTexture      │
                            │  - NormalTexture      │
                            │  - DepthTexture       │
                            │  - DebugDepth         │
                            │  - IsTopDown          │
                            └──────────┬────────────┘
                                       │
                ┌──────────────────────┼──────────────────────┐
                │                      │                      │
         ┌──────▼──────┐       ┌──────▼──────┐       ┌──────▼──────┐
         │  MI_Front    │       │  MI_Profile  │       │  MI_Top      │
         │  (textures   │       │  (textures   │       │  (textures   │
         │   come from  │       │   come from  │       │   come from  │
         │   preset)    │       │   preset)    │       │   preset)    │
         └──────────────┘       └──────────────┘       └──────────────┘
```

With the preset system, you no longer need separate material instances per state — one material instance per layer is sufficient, and the preset drives which textures are loaded for each view angle.

---

## Default Property Values

| Property | Default | Category |
|---|---|---|
| `HeadBoneName` | "head" | Skeletal Mesh |
| `TopViewPitchThreshold` | 60.0 | View Angles |
| `BottomViewPitchThreshold` | -60.0 | View Angles |
| `HalfZoneWidth` | 22.5 | View Angles |
| `CrossfadeSpeed` | 15.0 | Transitions |
| `HysteresisDegrees` | 2.0 | Transitions |
| `MaxParallaxOffset` | 5.0 | Parallax |
| `MaxVerticalParallaxOffset` | 3.0 | Parallax |
| `DepthMapIntensity` | 1.0 | Depth Maps |
| `bUseMaterialDrivenDepth` | true | Depth Maps |
| `bUseContinuousBlending` | true | Transitions |
| `BlendWindowWidth` | 5.0 | Transitions |
| `bAutoApplyPreset` | true | Preset |
| `AlbedoParamName` | "AlbedoTexture" | Material Texture Params |
| `NormalParamName` | "NormalTexture" | Material Texture Params |
| `DepthParamName` | "DepthTexture" | Material Texture Params |
| `AlbedoPrevParamName` | "AlbedoTexturePrev" | Material Texture Params |
| `NormalPrevParamName` | "NormalTexturePrev" | Material Texture Params |
| `DepthPrevParamName` | "DepthTexturePrev" | Material Texture Params |
| `bBlinkingEnabled` | true | Blink |
| `BlinkIntervalMin` | 3.0 | Blink |
| `BlinkIntervalMax` | 7.0 | Blink |
| `BlinkFrameDuration` | 0.03 | Blink |
| `CurrentExpression` | Neutral | Expression |
| `ExpressionCrossfadeDuration` | 0.3 | Expression |
| `bVisemeEnabled` | true | Viseme |
| `VisemeFrameDuration` | 0.04 | Viseme |
| `bDriveArtPositionFromYaw` | false | Art Transform Params |
| `MaxYawArtOffset` | 0.05 | Art Transform Params |
| `ArtPositionParamName` | "ArtPosition" | Art Transform Params |
| `ArtScaleParamName` | "ArtScale" | Art Transform Params |
| `ArtRotationParamName` | "ArtRotation" | Art Transform Params |
| `GridResolution` (debug) | 48 | Debug Visualizer |
| `HeightScale` (debug) | 10.0 | Debug Visualizer |

---

## Build Dependencies

- **Unreal Engine 5.x** (tested with 5.3+)
- **Modules**: `Core`, `CoreUObject`, `Engine`, `InputCore`
- **Plugin** (required for debug visualizer): `ProceduralMeshComponent`
- **Plugin** (recommended for editor utility widget): `Editor Scripting Utilities`

---

## Project Files

```
FaceParallaxTypes.h             — Shared types (EFaceAngleState, FFaceTextureSet, FFaceViewStateLayerSet, FFaceArtTransform, FFaceArtSlot)
FaceParallaxComponent.h/.cpp    — Core parallax component with preset + transform support
FaceParallaxPreset.h/.cpp       — DataAsset for storing texture + transform assignments per view state × layer
DepthDebugVisualizerComponent.h/.cpp  — Procedural depth mesh visualizer
FaceParallaxPreviewActor.h/.cpp       — Preview actor with scene capture, orbit camera, and part transform access
FaceParallaxEditorWidget.h/.cpp — Editor widget with 10 categories of bindable Blueprint functions for every setting

Tests/
  ParallaxMathTests.cpp       — Standalone C++ tests for state machine, transforms, edge cases (no UE)
  SyntaxValidator.py          — Python script validating all .h/.cpp for brace/macro/syntax issues
  run_tests.ps1               — PowerShell runner that compiles C++ tests + runs Python validator
```
