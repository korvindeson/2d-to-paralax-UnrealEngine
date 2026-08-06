# FaceParallax — 2D Face Parallax System for Unreal Engine 5

Camera-driven 2D face rendering with multi-layer parallax, depth map support, view-state transitions across 10 angles, real-time depth debug visualizer, preset asset system, and in-editor visual editor with 2668 automated tests.

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
| `FaceParallaxEditorWidgetUI.cpp` | Widget UI construction | `RebuildWidget()` — full Slate layout (context-panel pages, preview canvas, gizmo overlay, panels) plus UI-local helper factories |
| `FaceParallaxEditorWidgetInteractions.cpp` | Widget interactions | Selection/refresh-entry, gizmo + pin math, folder import wizard, edge overlay/histogram, viseme grid, nested outliner, param table, problems panel |
| `FaceParallaxEditorWidgetPanels.cpp` | Widget panels + diagnostics | `RefreshUI()` and all `Refresh*`/`Rebuild*` panel methods, zone diagram, status matrix, cross-layer/tag/material cross-refs, outline→depth bake, snapshot, diagnostics |
| `FaceParallaxEditorWidgetShared.h` | Widget shared internals | Anonymous-namespace helpers (channel/view-state suffix parsing, `FPresetTransactionScope` (backup-point only), `FWidgetUndoScope` (undo-stack push + transaction), `AccentBlue`, `MakeLbl`/`MakeBtn`) + the `SFaceLayerGizmo` nested class, shared by all widget translation units |
| `FaceParallaxLayoutSpec.h` | UI design contract (Phase H) | Pure C++17 layout manifest + metrics/placement solver + P1–P24 design-principle validator over the widget tree (P22 NoHorizontalOverflow: no non-flex child may exceed the clip-parent's `FixedW` margin box — rails never scroll horizontally; P23 AspectRatioBroken: `bAspectRatio` nodes must resolve to `FaceAspectRatio`; P24 NoTerminalOverlap: every MainRow column + center-column row must resolve inside the `MainRowHeight` band so nothing slides under the terminal output). Self-checked in `RebuildWidget` and fully covered by `TestPhaseHUIDesign`. |
| `FaceParallaxSchematic.h` | Part schematic manifest (redesign) | Pure C++17 (synced into the editor module) — 17 part glyphs with depth classes (Front/Base/Back), the 10-layer tag table `FPTagClassForTag`, part-name coverage aliases `FPSchematicLayerAlias` (Teeth→Mouth, Chin/Neck→Head), the hair system contract `FPHairLayerSet`/`FPSchematicIsHairLayer` (Bangs = front hair, Hair/BackHair = back hair), the canvas filter mirror `FPSchematicFilterAllows`, the group-colored edge map contract `FPEdgeGroup`/`FPEdgeGroupForPartName`/`FPEdgeGroupForTag`/`FPHairLevelForTag`/`FPHairLevelLuminance`/`FPEdgeLuminanceForClass`/`FPEdgeGroupColor`/`FPEdgeColorForPart`/`FPEdgeMapShows` (eyes/mouth/hair/surface groups, front lighter than back, hair detailed levels distinct + toggleable), the front/base/back yaw rules `FPYawRule` that `deploy.py`'s base preset and the component's `SyncLayerDefinitionsFromPreset` both consult, and the billboard smooth-turn orientation contract `FPOrientationOutline` — authored 2D layout ramps at the state centers (0/45/90/135/180, `FPRampEval` smoothstep) that flip at each state so per-view 2D art stays in sync, PLUS the five-level Z-depth camera-translation parallax (`FPZDepth`/`FPZDepthForPart`/`FPYawSlidePeak`/`FPYawSlideAt`, closest Z slides furthest, the Z-5 backdrop never slides; `FPSilhouetteWidthAt`/`FPNearFeatureWidthAt`/`FPFarFeatureWidthAt`/`FPFeatureAlphaAt` yaw foreshortening + far-side fold that stays folded through the back + walk-behind fade; `FPOrientationPitchScale` squash + the name-based encroach/counter pitch contract `FPPitchRoleForPart`/`FPPitchMagnitude`/`FPOrientationVerticalShift` — features + hair sink at the top view, ears + V-chin tuck up). Also carries the authored silhouette poses (`FPSchematicAuthoredPoseTable`/`FPSchematicAuthoredPoses`/`FPSchematicYawMorph`/`FPOrientationAuthoredMorph`, which `FPOrientationOutline` branches to for the four silhouette parts while every feature keeps the formula), the anchor-class contract (`FPSchematicAnchorClass`/`FPSchematicAnchorClassForPart`/`ForTag`), the per-state visibility + Z-order contract (`FPSchematicLayerVisibleInState`/`FPSchematicLayerVisibleInTag`/`FPSchematicLayerOrderInState`/`FPSchematicLayerOrderInTag`, state centers mirror `EFaceAngleState` order with default zone multipliers), the silhouette-delta crossfade/swoosh contract (`FPSilhouetteDelta`/`FPSchematicTransitionBlendRate`/`FPSchematicShouldSwoosh`), and the authored-pose validator (`FPOutlineIsValidClosedRing`/`FPSchematicValidatePoseSet`/`FPSchematicValidateAllAuthoredPoses`, 41% back-change gate). Covered by `TestSchematicParts`/`TestYawRule`/`TestSchematicCoverage`/`TestHairSystem`/`TestSchematicFilters`/`TestEdgeMapMirrors`/`TestPhase2Orientation`/`TestAuthoredOrientation`/`TestAnchorClass`/`TestPhase3Visibility`/`TestPhase4SilhouetteDelta`/`TestPhase6PoseValidation` + `TestPhaseCUpDownScrub` + the Phase 7 art-swap contract (`FPSchematicStateAtAngles` nearest-state resolution, `FPSchematicBracketStates` blend-weight bracket, `FPSchematicStatePoseOut`/`FPSchematicOutlineForState` exact per-state poses, `FPSchematicLayerArtAlpha` art-availability fade target, `FPSchematicSwapModeFor` crossfade-vs-swoosh gate, `FPOrientationOutline` SNAPS to the nearest state with no per-frame morph, walk-behind hides every non-silhouette card), PLUS the section 10 SVG-style smooth-curve paint contract (FPSchematicCurveCmd/FPSchematicArtChain/FPSchematicArtFace + FPSchematicArtFaceForRing, a pure C++17 port of smooth_art.py that lets the canvas preview render the same smooth curves and fills the Art library emits - CovEdgeA/B/WrapCov coverage for the occlusion dash, Tint/Order/Opacity per patch). |
| `generate_art.py` | Placeholder art library generator | Pure-Python — parses `FPSchematicAuthoredPoseTable()` + `DefaultPartSchematics()` from `FaceParallaxSchematic.h` and emits the 354-piece vector line-art library via the `smooth_art.py` engine (Catmull-Rom curve smoothing, per-part feature constructions: eye wedge+iris+highlight, mouth open-curve+gap, hair ribbon inner boundary+gloss, brow arch, nose triangle; fill patches for highlights/gloss per art_guide I.1/I.6/I.7). View→ring resolution mirrors `FPSchematicStatePoseOut` + the `FPSchematicPairPartner` pair swap exactly. Idempotent: `py generate_art.py` from the repo root. |
| `smooth_art.py` | Art-attractiveness engine | Catmull-Rom → Cubic Bezier spline conversion with curvature-based sharp-corner detection; per-part feature builders (`_build_eye_paths`: upper-lash wedge + disconnected lower-lash + solid iris fill + highlight fills; `_build_mouth_paths`: open upper/lower lip curves with center gap; `_build_hair_paths`: smooth outer contour + inner boundary 10-15% inset + crown gloss fill; `_build_brow_paths`: smooth arch; `_build_nose_paths`: angular triangle); `ring_to_svg_paths()` → list of path dicts; `emit_svg()` → complete SVG. Follows art_guide I.1 (monoline + fill patches), I.6 (construction geometry), I.7 (Curve Continuity, Shape Contrast). |
| `art_viewer_bridge.cpp` | Art-viewer bridge (the system's truth) | Pure C++17 (compiled with the same g++/flags as the math tests into `art_viewer_bridge.exe`) — `#include`s the canonical `FaceParallaxSchematic.h` + `FaceParallaxSvgParse.h` and prints one JSON document on stdout: the 14 state tokens/centers/walk-behind flags, the 17-part ↔ feature-token pairs, the per-(state × part) visibility + Z-order (`FPSchematicLayerVisibleInState`/`OrderInState`), and the per-(state × feature) RESOLVED cell key (`FeatureCellKey(Feature, CollapseViewStateForFeature(...), 0)`). The viewer never re-implements a rule; when the headers change the viewer recompiles this bridge and every value comes from the NEW system |
| `art_viewer.py` | Art viewer dashboard | Stdlib-only Python: compiles/runs the bridge, extracts the resolved cells verbatim from `Art/_grids/<Feature>.svg` (the same single import source the runtime bake uses), stacks them far-to-near into `Art/_views/View_<StateToken>.svg` — one composed character view per state, exactly as the editor canvas paints them — and serves a dark dashboard (3×3 canonical grid + transition strip + full library gallery) with "Regenerate all art" (runs the system's `generate_art.py`, recompiles the bridge, re-emits the views) and "Rebuild views". `py art_viewer.py` (serve, opens the browser) / `py art_viewer.py --emit-only` (headless compose) |
| `Art/` | Placeholder art library (generated) | 18 feature folders × per-state SVGs = 354 vector pieces (incl. 15 visemes + 18 blinks + the E10 empty Back3Q/Back cells shipped as `EMPTY CELL` placeholders; Bottom is deliberately excluded per art_guide Part V.4/VIII). Naming follows art_guide Part VIII: `<Part>_<State>_Y<Yaw>_P<Pitch>.svg` (state tokens Front/3Q/Profile/Back3Q/Back + mirrored `_L` variants + Top + pitch corners). `Art/_grids/<Feature>.svg` (18 files, 531 cells) hold one `<g id="<cell key>">` per cell — the single import source for the runtime bake AND the viewer; `Art/_tokens.json` is the library manifest; `Art/_views/` holds the 14 composed views. Each SVG contains smooth cubic-Bezier curves (not straight-line polygons), per-part feature constructions, and solid-fill highlight/gloss patches |
| `FaceParallaxEditorSubsystem.h/.cpp` | Editor subsystem | Registers the **Face Editor** toolbar button + **Window → Face Parallax Editor** menu entry, the `FaceParallaxOpenEditor` console command, and auto-open on editor startup (`bAutoOpenEditorOnStartup`, default ON); hosts the widget in a docked nomad tab. Asset deployment is handled entirely by `deploy.py` (repo root) |
| `FaceParallaxModule.cpp` | Module entry | `IMPLEMENT_MODULE(FDefaultModuleImpl, FaceParallax)` — required for the runtime DLL to register |
| `Tests/ParallaxMathTests.cpp` | Math tests | Standalone C++17 (no UE dep) — 2668 tests covering state machine, transforms, blink/expression/viseme, swoosh, parameters, nested art + jiggle, 3D pin projection + translation-only pin transforms (master blueprint: 2D art never rotates/scales per-frame), batch ops, zone multipliers, per-view visual hull, Phase B–H mirror suites + Phase H layout-design contract, part schematic + yaw rules + coverage/aliases + hair system + canvas filters + midpoint jiggle ramp + edge-map group mirrors, the billboard smooth-turn orientation contract (`TestPhase2Orientation`), the up/down pitch scrub contract (`TestPhaseCUpDownScrub`), the authored-pose morph suite (`TestAuthoredOrientation`), anchor-class mirrors (`TestAnchorClass`), the per-state visibility + Z-order contract (`TestPhase3Visibility`), the silhouette-delta crossfade/swoosh contract (`TestPhase4SilhouetteDelta`), the authored-pose validation with negative controls (`TestPhase6PoseValidation`), the authored feature-card matrix (`TestPhase2AuthoredFeatureMatrix` — per-zone rings from the front glyph, near/far P45 role split, profile/Top drops, ear back-fuzz, −45 view mirror), the Phase 7 art-swap contract (`TestPhase7ArtSwap` — nearest-state snap resolution, bracket weights, exact per-state poses, art-availability alpha, crossfade-vs-swoosh gate, back-half rule), the velocity-hierarchy yaw-rule feed mirror (`TestYawRule` — `FPYawRule::ComputeVelocityOffset` + `FPSchematicTagHasParallaxRate`), the Phase 8 parallax + hard-swap master-blueprint turn (`TestPhase8ParallaxSwap` — velocity table + part-to-tag aliasing, slide peaks, ramp key/next-key, rigid-translation no-morph pins, outgoing-peak/incoming-exact swap, Top/Bottom exact, BackHair promote, geometry passes), the placeholder art-library contract (`TestArtLibrary` — the 17×13 SVG encode: slot/mirror/pair-swap resolution identical to `FPSchematicStatePoseOut`, Bottom state 9 excluded per art_guide Part V.4, every visible piece equals the runtime snap outline), the section 10 SVG-style smooth-curve paint contract (TestSVGPaintSmooth - golden FRONT-ring geometry pins, chain/coverage-model invariants, decorative accents never dash, the M==1 Teeth straight-L regression, a 17-part x 13-view face sweep), the WI2–WI6 contracts (TestPhaseIISchmittStep/TestPhaseIIProximity/TestPhaseIIAnchorRead/TestPhaseIIPinLag/TestPhaseIIShapeContrast), the validation-batch pins (A.7 TestPhaseA7MaskRead, A.8 TestPhaseA8Asymmetry, A.10 TestPhaseA10FillChains, B.12 TestPhaseB12Residual), and the Req 4/5 rotation-bar pins (`TestPhaseHUIDesign` zone-bar row/above-canvas/root-removal checks + `TestPhase1ZoneScrub` camera-orbit strip-order/wrap pins via `FPZoneStripPixelForYaw`) |
| `Tests/SyntaxValidator.py` | Syntax validator | Brace/macro balance, include guards — enforces clean parsing on all source files |
| `Tests/validator_silhouette.py` | Silhouette geometry validator (E11) | Pure-Python — parses `FPSchematicAuthoredPoseTable()` and enforces the three geometry gates: hair-ribbon separation at 3Q (P45 far-side / P135 near-crown gap minimums), the ahoge cowlick at every yaw (mirror of `FPSchematicCowlickInRing` v2), and canthus preservation through the foreshortened eye cards (mirror of `FPSchematicScaleRingAboutCanthus` — chord-angle preservation to 1e-6 deg + sliver-width pin 0.025812242613 + a sensitivity negative control). `--path` arg; run by `run_tests.ps1` after the syntax validator |
| `Tests/run_tests.ps1` | Test runner | Root→SAMPLES sync + Python syntax validator + silhouette geometry validator + C++ math tests + optional UE build test |
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
Pitch > +45°   → Top
Pitch < -45°   → Bottom
                ┌─────────────────────────────────────────────┐
                │  -180   -135   -90   -45    45    90   135  │
                │ Back   BL    LP    3QL  Front 3QR  RP   BR  │
                │←── 45° zones ──────────────────────────────→│
                └─────────────────────────────────────────────┘
```

Default zone boundaries (multiplier × HalfZoneWidth = 45°) — the master
blueprint's hard swaps: each view's pose appears exactly at its key angle
(45 = 3/4, 90 = profile, 135 = back-right, 180 = full back) and the boundary
angle belongs to the NEXT view (half-open bands):

| State | Yaw Range (degrees) |
|---|---|
| Front | [-45, 45) |
| ThreeQuarterRight | [45, 90) |
| RightProfile | [90, 135) |
| BackRight | [135, 180) |
| Back | exactly ±180 |
| BackLeft | [-180, -135) |
| LeftProfile | [-135, -90) |
| ThreeQuarterLeft | [-90, -45) |
| Top | Pitch > TopViewPitchThreshold (+45) |
| Bottom | Pitch < BottomViewPitchThreshold (-45) |

**Custom zone boundaries**: Set `ZoneBoundaryMultipliers` (TArray<float>, indices 0-3 for Front, ThreeQuarter, Profile, Back) to adjust zone widths. Default `{1,2,3,4}`.

### Transitions

- **Crossfade**: When a state change is detected, `BlendAlpha` is a pure function of the rotation parameter — a **parameter-space crossfade** (art_guide III.6) spanning ±`BlendWindowWidth` (default 0.75°) around the hysteresis-adjusted trigger, so the same angular sweep fades identically at any interaction speed. `CrossfadeSpeed` is retained only for Blueprint compatibility and no longer drives the blend.
- **Continuous Blending** (optional): Disable `bUseContinuousBlending` for an instant hard swap at the state flip; when enabled (default), the incoming card fades in over the parameter-space window.
- **Hysteresis**: The state flip is a **directional Schmitt trigger** (art_guide IV.0), not a frame-count debounce — the flip commits only once the rotation parameter passes the shared swap boundary by ±1.5° in the direction of travel (forward at `Boundary + 1.5°`, reverse at `Boundary − 1.5°`), so a slow hover at a threshold never flickers and a fast drag commits at the same absolute angle. The commit key coincides exactly with the crossfade's α = 0.5 key. `HysteresisFrames` remains only as a same-frame jitter backstop (a raw flip must persist across frames); `AngleHysteresisBuffer` keeps the angle-based hold while hovering inside the buffer.

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
| `PinRotationFromYawDev(YawDev, HalfZoneWidth, MinRot, MaxRot, Sens)` | Static pin view-angle rotation. MASTER BLUEPRINT: 2D art cards never rotate per-frame, so this is **translation-only** — always returns 0 (signature preserved; unit-mirrored) |
| `PinRotationFromViewAngles(YawDev, PitchDev, HalfZoneWidth, MinRot, MaxRot, Sens)` | Phase 5 pitch-aware pin rotation. MASTER BLUEPRINT: translation-only — always returns 0 (unit-mirrored by `TestPrimaryLayerPin`) |
| `PinScaleFromView(YawDev, PitchDev, MinScale)` | Phase 5 pin view-angle scale. MASTER BLUEPRINT: translation-only — always returns 1.0 (unit-mirrored) |
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

### 3D Pins (Translation-Only, Master Blueprint)

`FFacePin3D` holds the attachment point. **Master blueprint:** real 2D art cards never rotate or scale per-frame — the turn is parallax translation + pre-created view swaps — so the pin's per-frame transform is **translation-only**: the art follows the projected 3D point, and the view-angle rotation/scale are identity (the `PinRotationFromViewAngles`/`PinScaleFromView` helpers always return 0 / 1.0). Signatures are preserved for compatibility and unit-mirrored.

| Field | Meaning |
|---|---|
| `bPinned` | Enables 3D pin projection: the element's pivot is the projected UV of `Position3D` (face-local `-1..1` per axis) at the live camera angle, so the element stays attached to its 3D point as the head turns |
| `Position3D` | Face-local pin position (X left/right, Y up/down, Z toward the nose) |
| `bEnableViewAngleRotation` | Retained for compatibility (gates the identity rotation/scale helpers — no per-frame rotation/scale is ever applied) |
| `MinRotation` / `MaxRotation` | Retained for compatibility (unused: rotation is always 0) |
| `RotationSensitivity` | Retained for compatibility (unused) |
| `MinScale` | Retained for compatibility (unused: scale is always 1.0) |

**Rotation/scale:** the pin transforms are translation-only — `PinRotationFromYawDev`/`PinRotationFromViewAngles` always return 0 and `PinScaleFromView` always returns 1.0 regardless of deviation/zone/range/sensitivity (mirrored by `TestPrimaryLayerPin`, `TestPinRotation`, and the `TestNestedEffectiveTransform` mirrors). The pin still *translates* every frame: a pinned element's effective pivot is the projected pin UV at the live camera angle (`ProjectPinToUVInternal`), so the art stays glued to its 3D point as the head turns — but it never rotates or scales with the view.

**Authoring:** `SetNestedPinFromUV` converts a click position into `Position3D` (Back-view clicks mirror the X axis so round-trips stay consistent); `ProjectPinToUVForState`/`ProjectPinToUV` project using the state's zone-center camera angle.

**Editor notes:**
- The pin section in the editor widget edits the **selected** top-level element (stepper `</>` or click a row in the Nested Elements outliner to select; the selected row is highlighted). Children are listed read-only.
- **Pin gizmo:** the preview-canvas gizmo paints the pin target at the selected pin's projected UV in the active view. **P7-C: pins are always live** — the old canvas-strip **Pin Mode** toggle is gone (no more silently switching the click model). The pin handle is draggable whenever the selected element (or the layer, when no element is selected) has a pin; a click on the handle moves it, and every other canvas click still selects a part (one-map). **Add Pin arms a one-shot placement:** after `Add Pin`, the next left-click on the canvas places the new pin at the cursor (`PlacePinAtUV` → `SetNestedPinFromUV`/`LayerPinFromUV`), then the arm clears and normal one-map selection resumes
- Controls are disabled when the active state/layer has no elements. Slider normalization guards zero/inverted ranges (`PinSliderNorm`, mirrored in tests), so `MinRotation == MaxRotation` never produces a NaN slider position.
- Design semantics: the pin contributes **position only** — the rotation/scale controls are inert (identity) by blueprint design, and a pinned element's transform is exactly the parent + relative composition plus the projected-pin offset.

---

## Zone Boundary Multipliers

The `ZoneBoundaryMultipliers` property (`TArray<float>`) on `UFaceParallaxComponent` lets you customize the width of each view zone relative to `HalfZoneWidth`:

| Index | Zone | Default Multiplier | Boundary at HW=45 |
|---|---|---|---|
| 0 | Front | 1.0 | 45° |
| 1 | ThreeQuarter | 2.0 | 90° |
| 2 | Profile | 3.0 | 135° |
| 3 | Back | 4.0 | 180° |

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
- **Context-panel workspace (W1)** — a single **MainRow = Center | ContextPanel** layout: center preview canvas (display-mode row + overlay stack) on the left, and one **621 px context panel** (`FPLayout::ContextPanelWidth`) on the right that replaces the old 5-rail switcher column + the 340 px selected-slot properties pane. Old T0–T3 tabs were re-homed into the context panel's 4 task pages + the Developer drawer
- **Task tab bar + context pages (W1, replaces Phase B rails)** — the old 5-rail switcher (`RAIL-Switcher` / `RL-*`) and its 273 px rail column are **gone**: the right side (old rail 273 + props 340 + gap 8 = **621 px**) is now ONE context panel switched by a labeled 5-button tab row (`FPLayout::CTTabRow`: CT-Tab0 "Assign" 56, CT-Tab1 "Transform & Sync" 118, CT-Tab2 "Expression/Blink/Viseme" 148, CT-Tab3 "Preview & Debug" 106, CT-DevTab "Developer" 84, + CT-Spacer, `TabBarHeight` 26, `AccentBlue` active highlight). Pages: **Assign** (Selected Layer, Layers + Pins, Import, Outline→Depth, Bulk Assign, Assign Ops), **Transform & Sync** (Transform + the ONE Copy/Sync panel), **Expression/Blink/Viseme** (Nested Art/Pins + Viseme Frames + Hull Review — the old separate **Animated Variants** and **Nested Elements & Pins** rails merged into one accordion in P6), **Preview & Debug** (Camera Follow, Camera, Blend Preview, Edge Analysis, Depth Debug), and the **Developer** drawer (closed by default: Tag Validator, Material Cross-Reference, Param Reference, Config, Param Bindings, Problems, plus the Status Detail + All Layers matrices moved off the old View & Layer rail). Param Reference + Param Bindings were re-homed from Nested & Pins into the Developer drawer (review grouping: params live with the diagnostics); the section registry (`FPLayout::PageSectionTitles`) mirrors the moves. Every page keeps its chip row + section registry; the tab bar sets `ActivePageIndex` exactly like the old icons, so chips, search jumps, and `SetActivePageIndex` all stay in sync
- **View strip with status dots (Phase A)** — per-state tabs + colored dots (green complete / amber missing albedo / orange per-view overrides) via `GetStateDotColor`; the "v" context menu per state keeps **Clear overrides (this slot)** only
- **ONE Copy/Sync panel (Phase 3)** — every sync/copy path consolidated into a single panel on the Transform & Sync page's **Sync + Align** section (the state-strip pick mode, the apply-to-views popups, the Assign page Quick Actions + Cross-View Transform sections, and Assign Ops' "Apply views" are all gone; view tabs always switch the active view). The panel has a **Transform / Textures / Both** op selector (`FPLayout::SyncOpLabel`/`SyncOpHasTransform`/`SyncOpHasTextures`), an always-visible 2×5 destination grid (`SyncViewCheckBoxes`, one per state; the active view reads "This" and is disabled), then **Apply to picked** (`SyncLayerToSelectedViews`/`SyncTexturesToSelectedViews`), **Apply to all** (`SyncLayerToAllViews` + `SyncTexturesLayerToAllViews`), **Copy from** view combo → `DuplicateState`, **Fill missing** (`FillMissingViewsFromActiveSlot`), and the **Link across views** broadcast checkbox with the drift indicator. The destination grid is a **live per-view diff preview**: `RefreshSyncDestDiff` colors each destination by what applying the current op would do for the selected layer — red = active has art the destination lacks (apply fills it), amber = transform and/or textures differ (apply overwrites), green = already matches; the mirror `FPLayout::FPSyncDestDiff` pins the classification (missing outranks differ). It replaced the old sprawl: the "v" menu picker, state-strip pick mode, Quick Actions' Apply to views.../Duplicate/Fill Missing, and Assign Ops' Slot → All
- **Per-part status chips (P2)** — the schematic glyphs and the 17-part legend chips now show per-part slot completeness in the ACTIVE view (green = full A/N/D, amber = partial, red = missing): the glyph layer paints a status dot at each part's centroid (painted after outlines so it always sits on top), and `RebuildPartsStrip` adds a matching dot + "A/N/D: ..." tooltip line to every legend chip
- **Vector-art viewer (Phase 2/3/4)** — when the preset carries the generated `Art/<Part>` SVG library (deployed via `deploy.py` → `LibraryVectorArt`), the schematic canvas paints the resolved rotation-driven **two-card crossfade** instead of the ring glyphs: `RefreshSchematic` resolves each part's cell pair through the pure `FPSvg::ResolveViewCellPair` contract (bracket pair from `FPSchematicBracketStates`, yaw-row collapse, pitch band) and paints the Prev card at (1−α) and the Cur card at α exactly like the runtime swap; cards the runtime hides (walk-behind features, the profile merge) are NOT drawn (`PartAlpha` 0), and cells without art fall back to the ring glyph. The **Preview & Debug → Camera "Vector art" checkbox** toggles the preset's `bUseVectorArtViewer` (default on; rings when off). The **"Vector albedo" checkbox** extends the same library to the 3D preview: with the preset's `bUseVectorArtAlbedo` on, each runtime quad's albedo becomes a per-tag texture composited from the authored cells through the pure `FPSvg::RasterizeAlbedoForTag` contract (slot key `ResolveVectorAlbedoKey` → per-feature cells in tag painter order → supersampled raster), baked lazily per state at 128² × SS2 and pre-warmed 4 per tick (`TickVectorBakes`); layers the runtime hides in a state (walk-behind, profile merge, Top) bake to nothing and hide exactly like the raster path, and the crossfade prev-slot uses the same bakes. The library itself is generated by `generate_art.py` (17 parts × 13 views, plus viseme/blink authored shapes and the FaceBase Profile merged contour, all gated: silhouette-read, art-escape, turn-seam margins)
- **Sync drift indicator (Phase C)** — `RefreshSyncDriftIndicator` compares the active state's canonical transform against the other 9; shows "Synced" or "Drifted: n/9"
- **Folder import wizard (Phase C, P4)** — `OpenImportFolderWizard` opens a modal: pick a folder, Scan (`FindFilesRecursive` for png/jpg/jpeg/tga), part cards parsed from `{Part}_{View}_{Map}` names via `MatchStateSuffix`/`StripChannelSuffix` (full names before short codes), per-view×channel coverage preview with green/miss dots, Apply assigns to the active layer then closes. **P4: the folder wizard is the bulk import path** — toolbar "Import Art...", the pinned quick-actions "Import Art…", the Assign page "Import Folder...", and the Problems-panel Import all route here; the whole folder row is a **drop zone** (drag image files straight in — they are parsed exactly like a Scan), and the Assign page Import section also accepts file drops into the wizard. Single-art import (Phase 2) supersedes the artless-glyph click: clicking an artless schematic glyph or legend chip now opens the native OS picker (`OpenImportArtDialog`) for that part directly, with the wizard retained as the secondary "Import Folder..." bulk path
- **Display modes (Phase D)** — `SetDisplayMode` maps Textured/Depth/Wireframe/Split to the preview toggles; the mode row sits above the canvas (P5: superseded as the primary selector by the large Preview: segmented row; see Unified inspect mode)
- **Depth Debug knobs (Phase D)** — live sliders for the visualizer's `GridResolution`, `MeshSize`, `HeightScale`, `LocalOffset.Z` plus hex Low/High color edits and a Rebuild button that re-feeds the active slot's depth texture
- **Edge overlay + histogram (Phase D)** — `BuildEdgeOverlay` downsamples the active albedo, runs a Sobel edge pass (`EdgeDensity` static), and renders green edges over the preview; a 16-bin luminance histogram (`BuildLumaHistogram` static) draws as bars with density/mean stats
- **Hull Review (Phase D)** — orbit-3D auto-rotate/speed/snap controls plus a 2×5 thumbnail grid of every state's albedo for the active layer (click to jump)
- **Viseme Frames grid (Phase E)** — `RebuildVisemeGrid` renders the active layer's viseme timeline: one row per viseme (plus named visemes) with one cell per frame, filled cells = assigned art; clicking a filled cell plays that viseme; each row shows its fill percentage and the currently playing viseme is highlighted
- **Nested-art outliner (Phase E)** — `RebuildNestedOutliner` lists the active layer's nested elements with per-view visibility checkboxes, [Pin]/[Jiggle] badges, a Del button, and indented child rows; edits write back through `SetNestedElement`; the selected row is highlighted
- **Jiggle editor controls (Phase 7)** — the Nested Art / Pins pane adds a **Jiggle** checkbox plus Stiff / Damp / Imp / Mid / End Stiff / End Damp / End Imp sliders on the selected nested element (writes via `SetNestedJiggleEnabled`/`SetNestedJiggleSettings`); the sliders enable only when a nested element is selected and jiggle is on, and disable for whole-layer pins
- **Pin manager (Phase E, P3)** — the Nested Art / Pins section has an **[Elements] / [Pins]** pane switcher (`SetNestedPaneMode`); the Pins pane (`RebuildPinManager`) is the central pin overview the review asked for: the header reads **"Pins on `<Layer>`: N"**, and an **Add Pin** button creates a new element and **arms a one-shot placement** — the next canvas click drops the pin at the cursor (`PlacePinAtUV`, undo-scoped; the arm clears after exactly one click, then normal one-map selection resumes). One row per pinned item (whole-layer pin, every pinned element, pinned children), each with a visibility toggle, jump-to-element (selects the element and flips back to its controls), and an Unpin button; a Copy row duplicates the selected pin to another element (pin data copied via `FFacePin3D`, undo-scoped). Rows are counted by `FPLayout::FPPinnedRowCount`
- **Pin controls (advanced, P3)** — element `<` `>` stepper (no more hardcoded element 0), live Pinned checkbox, then three clearly separated sub-sections: **PLACE** (Pin X/Y/Z as **0–100% of the layer's frame**, not raw −2..2 units), **PHYSICS** ("Rotate w/ view angle" checkbox + Min Rot / Max Rot / Sens sliders, plus a Min Scale slider), **MOTION** (jiggle checkbox + Stiff/Damp/Imp/Mid/End Stiff/End Damp/End Imp sliders, plus Idle Frame duration and Idle Speed sliders on nested elements); all refreshed by `RefreshPinControls()`; pins are also draggable directly on the canvas via their pin handle, and placed at the cursor via the Add Pin one-shot (see above)
- **Param bindings table (Phase E)** — `RebuildParamTable` shows every binding of the active state/layer: editable param name, target cycle button (PosX→PosY→SclX→SclY→Rot→Blend), Invert checkbox, and X remove; the Add row above appends new bindings via `SetParamBindings`
- **Problems panel (Phase F)** — `RebuildProblemsPanel` validates all 10 states × every layer for missing albedo/normal/depth and warns on blink-frame and viseme-frame count mismatches across layers; issues are deduplicated (sorted-unique), color-coded (red error / amber warning), and clicking a row jumps to that state
- **Problems panel upgrades (Phase 4)** — the panel now has a quick-actions bar (page jump chips + Import + Clear Stale), a Layout Group section that live-runs the manifest's `ValidateDesign(BuildSpec())` and lists any P1–P24 violations (green "Design contract OK" when clean), an issue search box that filters rows live with a match count in the summary, and a summary line on the Problems accordion header (`SFaceAccordion::SetSectionSummary`)
- **Show Pins (Phase 3, P5)** — the **Canvas Options** overflow menu's "Show Pins" checkbox paints every nested-element marker on the gizmo color-coded: amber = static pin, cyan = rotation pin, purple = jiggle element, red ring = plain pivot anchor (unpinned); the selected pin handle stays amber as before
- **Sync drift indicator (Phase C)** — `RefreshSyncDriftIndicator` compares the active state's canonical transform against the other 9; shows "Synced" or "Drifted: n/9" (exact-equality semantics mirrored by `FPPinDriftCount`/`TestPinDriftMirror`); wired into the Transform & Sync page's Sync + Align section (Phase 3) and refreshed from `RefreshUI()`
- **Alignment tools (Phase B + UX Phase 1)** — canvas gizmo (`SFaceLayerGizmo`); since UX Phase 1 the transform box is **fully interactive on canvas**: drag the box edges to move, drag the top handle to rotate, drag the bottom-right corner to scale (uniform), routed by `SFaceHotspotLayer` through the static pure contract `GizmoHitTest`/`GizmoApplyDrag` into the canonical/override/link path (`SetGizmoTransform`). The gizmo leaf stays paint-only; the box **interior** is a deliberate miss so the P1 part glyphs behind it remain clickable; the transform is snapshotted at mouse-down so the drag cannot drift; handle cursors (move/rotate/scale) appear on hover. Onion-skin ghost of the adjacent state with opacity slider, and the Cross-View copy-from combo + Link checkbox moved into the Sync + Align page (Phase 3; `ApplyCanonicalTransformWithLink`)
- **Thumbnail status matrix** — 10-state × N-layer grid of aspect-correct albedo thumbnails (pixel size + click-to-jump tooltip) replacing the old 22×20 ✓/× grid; the grid is **carousel-paged** (P18) inside a fixed 184 px viewport with a prev/page/next strip, so the table's last layer row (e.g. **Hair**) stays reachable above the terminal section instead of being clipped under it — the manifest mirrors it as `SD-Carousel`/`SD-CarouselNav` (`MainRowHeight` band, P17/P24)
- **Camera follows view** — view-strip clicks and matrix jumps snap orbit yaw/pitch to the state's zone center when `bCameraFollowsView` is enabled (default)
- **View override mode** — transform edits write `ViewOverrides` (per-rendered-view deltas) instead of canonical transforms when `bViewOverrideMode` is on
- **Silhouette → depth** — "Generate Depth from Outlines" extracts silhouette edges from rotation-view albedo textures, builds a visual-hull depth buffer, and bakes it into every layer's depth channel
- **Batch import by channel suffix** — imports auto-assign by filename suffix (`_N`/`_Normal`/`_normalmap` → Normal, `_D`/`_Depth`/`_height`/`_displacement` → Depth, else Albedo); every import path (wizard Scan/Apply, wizard drop zone) uses it when a layer is selected
- **Quick Actions (P21 pinned strip)** — the canonical batch actions (Auto-Fit All, Sync All→All, Clear All Overrides) live only in the pinned strip between the state strip and the tab row
- **Outline view management** — per-state checkboxes bound to the component's `OutlineViewStates` (All/None shortcuts); depth grid resolution is user-editable (8–256)
- **Outline → depth with scope + confirm** — "Generate Depth from Outlines" is arm/confirm-guarded (first click arms, second click commits; Detect Profile runs the combined flow unarmed) and offers a bake scope (Front only / 8 h-states / all 10). Each target view receives its own per-view visual-hull map computed in that view's camera frame instead of a verbatim copy of the front map, and the bake only touches the depth channel of the target states' layers
- **Toolbar search** — the filter box moved to the toolbar so it applies across the context panel's pages (search jumps across all 5 pages via `OnPageSearchCommitted`)
- **Page accessibility (Phase 4b, re-based for W1)** — the big-scroll remediation follow-up, all mirrored by `TestAccessibilityMirrors` (`FPLayout::PageSectionTitles` / `FindPageSectionByTitle` / `ConfigSummary` / `VisemeSummary` / `RailWidthAfterDrag` / `QuickActionLabels`):
  - **Progressive disclosure below section level** — the Developer drawer's 4-checkbox Config section (Blinking / Swoosh / Nested Art / Params) and the Expression page's Viseme Frames grid each collapse into a one-line summary ("K of 4 on" / "N viseme rows") behind a clickable `SFaceDisclosure` header; summaries update live from `RefreshConfigCheckboxes` / `RebuildVisemeGrid`
  - **Persistent quick-actions bar** — Import Art…, Sync All→All, Auto-Fit All, Clear All Overrides sit in a full-width pinned strip row between the state strip and the tab row — never inside a scroll viewport (P21 `PinnedActionsNeverInScroll`: the manifest's `PinnedStrip` node holds exactly these four, flagged `bPinnedAction`; the Python validator flags any canonical label built in a rail or scrolled panel). Button set mirrors `FPLayout::QuickActionLabels()`
   - **Cross-page search jump** — pressing Enter in the toolbar search runs `OnPageSearchCommitted`, finds the first section title containing the query across all 5 context pages, switches page, expands the accordion section, and scrolls it into view (`SScrollBox::ScrollDescendantIntoView`); a "no match" status message is shown otherwise
   - **Fixed context panel width** — the context panel is **fixed at `FPLayout::ContextPanelWidth` (621 px)** and is NOT manually resizable: no internal splitter anywhere (the old `SFaceRailResizer` handle between rail and canvas, the rail-width spinbox, and the rail/canvas splitter are all gone). Resizing only happens at the very outside of the widget (the window/tab edge). The width is derived from the edge-schematic section's empty space — `MainRowWidth − CenterColumnMinWidth` (1089 − 468 = 621 = the old rail 273 + props 340 + right gap 8) — the maximum that still leaves the center column its 468 px minimum (5-button display-mode row + 450 px aspect-locked canvas), so the paged carousels and edge map can never be broken by dragging (P24 NoTerminalOverlap defect class)
  - **Section-jump chips + scroll indicator** — every context page has a pinned chip row above its content box (`BuildPageSectionChips`): one chip per registered section (`RegisterPageSection` / `RegisterAccordionPageSections`), click to jump (cross-page jumps rebuild + auto-expand + scroll), the last-jumped chip highlights; the registry is the single source of truth for chips and search
- **Live numeric camera readouts** — Yaw/Pitch/Dist values shown next to the sliders and updated on refresh
- **Spatial part picking (Phase 1/2/4 → P1 one-map)** — the canvas click model was consolidated to ONE map: `SFaceSchematicLayer`'s 17 part glyphs are the only pick surface (the old hotspot-region outlines and the layer-art quad click layer are gone — no Alt/Ctrl modifier paths remain):
  - **One interaction model** — left-click a glyph (or its legend chip) resolves the part to a layer (`ResolveHotspotLayer`: the preset's persisted `HotspotLayerMap` first, then `FPLayout::FPHotspotLayerMatch` exact → plural → L/R-collapse → prefix derivation, so the default Eyes/Brows/Mouth/Hair layer set resolves EyeL→Eyes, BrowR→Brows, Mouth→Mouth out of the box), selects it, jumps to the Assign page with tweak controls open, and — when the layer has no art — opens the native OS picker (`OpenImportArtDialog`) preselected on that part; layers with art are selected for review only. Right-click (glyph or chip) opens the in-tool part→layer remap menu (persisted in the preset, "Auto (derived)" resets)
  - **Legend strip** — the chips under the canvas mirror the glyph map exactly (same 17-part set, same per-layer hue colors, dark gray = unmapped) with the visible `Part → Layer` mapping; a click on a chip does exactly what a click on its glyph does
  - **Breadcrumb + click pulse** — picking a part sets the 'Front → Eyes' breadcrumb (`SetBreadcrumb`; alias parts show `Front → Mouth (Teeth)`) beside the layer label and pulses the glyph bright for 0.5 s (`SchematicFlashPart`/`SchematicFlashTimestamp`, invalidated by `NativeTick`) — feedback at the point of action
  - **Per-view bounds from stored transforms** — `RefreshHotspotRegions` transforms each region's outline by its layer's effective transform for the active view state (`FPHotspotTransformRegion` mirrors the master material's UV chain: pivot subtract → art position → scale → pivot add → rotate), so outlines hug the art's real position/scale on Profile/Back/¾/Top/Bottom instead of a fixed front template; unmapped regions keep the template pose
  - **Cycle Preview (Phase 2)** — 8-second tour sequencing the live systems one at a time: blink 2s → expression 2s → viseme 2s → orbit sweep 2s
   - **Live Preview (Phase 4b)** — the assembled-result check: blink + Smile expression + re-triggered viseme (2.5s cadence) + continuous orbit sweep (8s period) all run TOGETHER; the two modes are mutually exclusive (starting one stops the other). Both mode machines are mirrored by `TestPreviewModesMirror` (`FPLayout::PreviewSystems` / `PreviewCyclePhaseDuration` / `LivePreviewVisemeCadence` / `LivePreviewOrbitPeriod` / `PreviewModeSystemFlags`)
- **Central-canvas redesign (schematic default view + interactivity + front/base/back yaw rules)** — the canvas shows the 17-part schematic as THE map (P1 one-map); glyphs are transformed by the layer's effective transform so the map hugs the rendered head:
  - **Part schematic source of truth** — `FaceParallaxSchematic.h` (pure C++17, synced into the editor module like the layout spec) defines 17 part glyphs (13 anatomical hotspot regions + Bangs/Hair/BackHair/Head) with `FPDepthClass` (Front/Base/Back) and the 10-layer tag table `FPTagClassForTag`; glyph fidelity was re-authored for recognizability (almond eyes, arc brows, nostrilled nose, lip-shaped mouth with cupid's bow, ear shells, cheek arcs, chin-rounded head silhouette, hair fringe + full silhouette + curtain); math mirror tests `TestSchematicParts`/`TestYawRule` assert the glyphs, classes, hit-testing, and rule formula
  - **Full part→layer coverage** — every one of the 17 parts resolves to a base-preset layer: `FPHotspotLayerMatch` derivation handles CheekL/R→Cheeks and EarL/R→Ears once those layers exist (plus EyeL→Eyes, BrowL→Brows), and `FPSchematicLayerAlias` covers the rest (Teeth→Mouth, Chin→Head, Neck→Head); `TestSchematicCoverage` pins all 17 resolutions
  - **Hair system (Phase 2)** — `FPHairLayerSet` (Bangs/Hair/BackHair) + `FPSchematicIsHairLayer` pin the hair contract: **Bangs = FRONT hair** (moves WITH yaw, amber), **Hair + BackHair = BACK hair** (move AGAINST yaw, cyan); every hair layer rides the normal per-layer pipeline (camera-sync, auto-fit, bulk-assign, nested pins, visibility, problems panel) — the set only declares which layers are hair and what class they carry (`TestHairSystem`)
  - **Per-edge occlusion — the map reads the composition (Review Req 2)** — `RefreshSchematic` keeps EVERY glyph (P1 one-map); every glyph paints per-edge in the resolved view state (`FPSchematicStateAtAngles` → `SFaceSchematicLayer::SetCurrentState` + `ComputeOccludedEdges` + `FPSchematicLayerOrderInState`): edges hidden behind a part rendered in front of them draw dashed, exposed edges draw solid. The placeholder glyphs ARE the character (they always exist and recover when replacement art is removed), so the per-edge read applies to artful AND artless parts alike — there is no "no art = dashed" state; the P2 status chips (red/amber/green at each glyph's centroid) carry slot completeness. Glyphs are transformed by the layer's effective transform in the active view (`FPHotspotTransformPoint` chain), so the schematic hugs the same positions the assigned art will paint; unmapped parts keep the template pose
  - **The canvas is fully clickable (Phase 0 / P1)** — Slate routes mouse events to exactly one topmost hit-test-visible leaf plus its ancestors, so the old overlay stack had dead clicks (the gizmo SBox swallowed everything whenever a layer was selected, and nothing below it was ever reachable). Now the gizmo is **paint-only** (`EVisibility::SelfHitTestInvisible` on the SBox and the leaf), and `SFaceHotspotLayer` — the topmost interactive widget — is THE click router with one fixed order: **pin-drag (pin mode, moved out of the gizmo) → schematic glyph (left select/import, right remap) → miss** (misses are swallowed so clicks are never dead; the named-region and layer-art-quad click steps were deleted in P1). Hover and the Hand cursor are forwarded to `SFaceSchematicLayer` lens- and filter-aware, so what you see is exactly what you can click
  - **Click-to-assign** — one core (`SelectPartOrImport`) drives both the canvas glyph and the legend chip: resolves the part to its layer (derivation → alias), selects it, sets the breadcrumb, pulses the glyph; empty layers open the native OS picker (`OpenImportArtDialog`) preselected on that part (Phase 2 direct import); layers with art are selected for review only
  - **Selection emphasis (Phase 3)** — the selected layer's glyphs render at 3px with full alpha and a soft translucent fill halo while every other glyph dims to 25% alpha; hover still highlights at 2.5px
  - **Filter row (Phase 3)** — under the canvas mode row (toggled by the Canvas Options "Filter row" checkbox): a depth radio (All/Front/Base/Back), one toggle chip per base-preset layer (10 chips colored amber/grey/cyan by depth class), the **Focus** zoom, and **Clear**. The row is the pure mirror of `FPSchematicFilterAllows` (empty layer filter = all layers; depth radio 1/2/3; AND semantics) — the math tests pin the mirror
  - **Focus toggle (Phase 3)** — `ToggleSchematicFocus` zooms the selected layer's glyphs to fit: `RefreshSchematic` builds a lens (uniform scale clamped to [1,8], 90% fit margin) from the selected layer's view-transformed glyph bounds, and the schematic paints + hit-tests through the same lens (`SetFocus`/`FocusPoint`/`InverseFocusUV`)
  - **Base preset yaw rules** — the 10-layer base preset (`deploy.py` `LAYERS`, grouped Front/Base/Back with the hair layers commented) carries a per-layer `depth_class`; `SyncLayerDefinitionsFromPreset` consults `FPSchematic::FPYawRule::DepthScaleForTag`/`InvertsParallaxForTag` (Front = scale 1.0, no invert; Base = scale 0.15; Back = scale 1.0, inverted; max offset 5.0) for newly-created layers, mirroring `ComputeOffsetForState`
  - **Smooth-turn orientation (Phase B rework) + master-blueprint turn (Phase 8: parallax + hard swap)** - the placeholder is **billboarded**: every part is a flat card that always faces the camera (the surface normal never turns edge-on / never paper-thins - real 2D art), and the turn is faked by **sliding five Z-depth planes** against each other via camera-translation parallax (FPZDepth 1 Nose/Front Bangs -> 5 Neck/Back Hair; FPYawSlidePeak/FPYawSlideAt: closest Z slides furthest, the Z-5 backdrop never slides), with a **hard swap** at each exact 45/90/135/180 yaw state center. Phase 8 replaces the phase-7 'snap to nearest pose with no motion' defect with REAL motion that never deforms art: FPOrientationOutline slides every vertex by the SAME dx/dy (FPSchematicTagParallaxRate + FPSchematicParallaxSlidePeak; velocity hierarchy +100% Nose+Bangs / +60% Eyes+Brows+Mouth+Cheeks / 0% Face Base anchor / -50% Ears / -100% BackHair) so a uniform monoline is preserved, and at the zone key (0/45/90/135/180/±90 pitch) the pose HARD-SWAPS to the next view's authored pose via FPSchematicParallaxRamp (0 at a pose key, 1 at the next) - no vertex blend/morph. Zone geometry re-baselined: FPSchematicViewZone HalfZoneWidth 45, pitch thresholds ±45, boundaries 45/90/135/180; FPSchematicStateAtAngles half-open bands (boundary belongs to the NEXT view; |yaw|>180 -> Back, 180 = Back); FPSchematicBracketStates back-wraps with B3=135, exclusive at 135, inclusive at ±180. Top/Bottom return the authored pitch pose EXACTLY (no encroach/slide). Walk-behind read keeps the AnchorCritical Ears as flat back-fuzz while features hide (FPSchematicLayerVisibleInState), and BackHair promotes to layer 1 in state 4 (FPSchematicLayerOrderInState). The Phase 2-5 morph/slide/fold/squash formulas (FPRampEval smoothstep, FPOrientationPitchScale squash, FPOrientationVerticalShift encroach/counter, authored silhouette poses FPSchematicAuthoredPoseTable/FPOrientationAuthoredMorph) are preserved for the pure pins but never run per-frame. **Geometry ratio pins (Part I)**: FPSchematicFaceGeometry measures the face (midline eyes at y≈0.44, 5-part width rule) via FPSchematicMeasureFaceGeometry + FPSchematicFaceGeometryPasses. Pinned by TestPhase8ParallaxSwap + re-pinned TestPhase2Orientation/TestAuthoredOrientation/TestPhase3Visibility/TestPhase7ArtSwap.
  - **Authored feature-card pose matrix (Phase 2, guide Parts IV/V)** — the 13 anatomical cards (eye/brow/cheek/ear pairs + nose/mouth/teeth/chin/neck) carry a full authored pose matrix in `FPSchematicAuthoredPoseTable` (`FPSchematicFeatureRingAt` → `FPSchematicBuildFeaturePoseSets` remain only as the fallback for non-table parts), every card's **P45/P90 slots hand-authored** (per-segment compression, never a uniform scale — the outer/profile-side edge compresses more than the nose-side edge, and every ring is pinned to differ from the formula output): **P45** splits the near/far role — near member `Eye_Near_3Q` (~0.84) vs far member `Eye_Far_Narrow` (~0.50), `Brow_Far_3Q` (~0.60) vs near brow (~0.80), near ear (~0.91) vs far ear (~0.76, the L-side card is the canonical far card at +yaw), near cheek (~0.82) vs far cheek (~0.70), the mouth compresses into an off-center `Mouth_3Q` (~0.80 wide + ~0.02 shift), the nose darts toward the turn side (CX ~0.53), teeth/chin follow the same dart+compress rules; **P90** is the profile sliver (eye 0.014 / brow 0.040 / cheek 0.018 / ear 0.016 wide — every card stays <0.08 wide and the near ear P90 runs taller than its front glyph; centerline cards drop into the contour as narrow placeholders), **P135** the ear back-fuzz band, **P180** the folded >10%-of-canvas back card (clears the Phase 6 back-change gate), **PTop/PBottom** the front glyph. The left-half states (5–7) resolve the PARTNER's ring mirrored (`FPSchematicPairPartner`), so the −45 view is still the exact mirror of +45 with the near card riding the left side. Visibility precedes resolution: **Top (8) drops every feature card** (Part V.2, ears/Head/Hair stay), **profile 2/6 merges Nose/Mouth/Teeth into the contour**, the far-side member folds at the profile, walk-behind 3/4/5 hides everything but the AnchorCritical ears. Pinned by `TestPhase2AuthoredFeatureMatrix` + re-pinned `TestPhase2Orientation`/`TestAuthoredOrientation`/`TestAnchorClass`/`TestPhase3Visibility`/`TestPhase7ArtSwap` (the FPSilhouetteDelta numbers follow: Front→Top = 0.4 / Front→Bottom = 0.1 / Top→Back = 0.3 / Top→Bottom = 0.5, so the crown swap swooshes).
  - **Up/down view scrub (Phase C)** — a dedicated vertical **Pitch view** strip mounted **left of the canvas** (the yaw zone bar sits above the schematic canvas) is the up/down counterpart of the yaw zone scrub: drag up lifts the head toward **Top**, drag down lowers it toward **Bottom**; the relative pixel drag is fed through the pure `FPLayout::FPZoneScrubPitchAfterDrag` contract (full strip = 180°, clamped `[-90,90]`, NO wrap) and drives `SetOrbitPitch` + `ActiveViewState` live (`DetermineStateFromAngles` parks at the Top/Bottom art states past their thresholds — both are real art states with vertical parallax). Release snaps the pitch to the canonical state center so **Top parks at +90 / Bottom at −90**. The strip spans the canvas height (450px = 2.5px/deg) beside it. `RefreshSchematic` prefers the scrub yaw/pitch while either strip is dragging (`bScrubbing = bZoneScrubbing || bPitchScrubbing`). Pinned by `TestPhaseCUpDownScrub` + the vertical-parallax pins in `TestPhase2Orientation`
  - **360 rotation bar above the schematic (art-guide Req 4)** — the yaw zone diagram (the "360 rotation slider": yaw/pitch readout + the 20px zone strip + `SZoneBoundaryOverlay` boundary drags + empty-space yaw scrub) moved out of the widget-top root row into the **center column directly above the canvas** (`BuildPanelCanvas` mounts `BuildPanelZoneDiagram` between the filter row and the canvas; manifest node `CN-ZoneDiagram`, pinned by the Req 4 row-order/above/below checks in `TestPhaseHUIDesign`). The strip **reads in camera-orbit order (Req 5): Left → 3/4L → Front → 3/4R → Right → BackR → Back → BackL**, left-to-right with the right edge wrapping back to the left edge (BkL is the last segment), so a full 360 sweep is a continuous turn with no jump. The overlay's boundary lines + yaw cursor use the pure `FPLayout::FPZoneStripPixelForYaw` contract (left edge = −135, the Left-profile zone start; `FPZoneStripRebaseDeg`), pinned by the strip-order/wrap pins in `TestPhase1ZoneScrub`; the relative-drag scrub math (`FPZoneScrubYawAfterDrag`) is order-independent and unchanged
  - **Depth overlay checkbox** — the **Canvas Options** overflow's "Depth overlay" toggle (`ToggleDepthOverlay`/`BuildDepthOverlay`) composites the selected layer's depth map over the live preview at 55% opacity
  - **Group-colored edge map (Phase I)** — the part edge map is now group-colored **by default** (the canvas's default look): every glyph paints in its `FPEdgeGroup` color — eyes green, mouth red, hair violet, everything else grey-blue — with the depth class scaling the LUMINANCE so **front reads lighter than back** (`FPEdgeLuminanceForClass`: Front 1.0 > Base 0.72 > Back 0.45). The **hair system** is its own group with three detailed levels, each a distinct luminance step of the violet family (`FPHairLevelLuminance`: Bangs 1.0 > Hair 0.72 > BackHair 0.45 — level drives the brightness, the depth class never dims hair), so the three hair layers read as separate edges, all distinct from every other group. Edge-map outlines paint at full alpha (the legacy tint stays subdued), and a **legend strip under the canvas** keys every group + hair level to its color while the map is on. The **Canvas Options** overflow's **Edge map** checkbox (`SetSchematicEdgeMap`) reverts to the legacy depth-class tint (amber/grey/cyan), and **Hair edges** (`SetEdgeMapHairEdges`) hides the hair system's detailed edge levels wholesale while every other group stays (`FPEdgeMapShows`). Pure mirrors: `FPEdgeGroupForPartName`/`FPEdgeGroupForTag`/`FPEdgeColorForPart`/`FPHairLevelLuminance`, pinned by `TestEdgeMapMirrors`
  - **Fixed canvas + post-assign flash** — the preview canvas is **fixed at the 450 px design constant** (no interior drag-resize handle: resizing is only allowed at the very outside of the widget, the window/tab edge — both the old canvas handle and the rail/canvas splitter are gone; resizing them let the canvas outgrow the `MainRowHeight` band or shrank the center column, breaking the paged carousels, P24 NoTerminalOverlap defect class); after any albedo assignment, `SetSlotTextures` arms a 1.5 s fading green pulse ring on the layer's canvas quad (`AssignFlashLayer`/`AssignFlashTimestamp`, invalidated by `NativeTick`)
  - **Phase II contract work items (WI2–WI6, art_tech_guide)** — new pure schematic contracts (Sections 13–17): **WI2** the per-sample directional Schmitt step `FPSchematicSchmittStep` (one neighbor per sample, hard commit at `FPSchematicSchmittTriggerAt` 1.5° through the canonical 12-pair boundary table `FPSchematicYawBoundaryForPair` — the component's default edge multipliers {0.5,1,1.5,3,6,8,−8,−6,−3,−1.5,−1,−0.5}×HZW 22.5, so Front|NarrowR sits at 11.25, NarrowR|3QR at 22.5, 3QR|SliverR at 33.75 — the edges are NOT the state centers; the 22.5/67.5 sub-swaps fire at their own edges) + the III.6 local-delta-reset `FPSchematicThetaFiredRebase` (offset = Peak×[sin(θ)−sin(θ_fired)] — zero exactly at the captured trigger, velocity-continuous). **WI3** camera proximity `FPProximityFactor` (clamped K/max(Z,Z_min); F = 1.0 at the reference 100, clamps 0.25/2.0, never diverges at Z→0), the proximity-scaled swap ramp `FPSchematicProximitySwapRamp` (a close-up completes the swap EARLIER over the same screen-space travel, a long shot never widens), `FPSchematicSeamMargin` (percentage × F_prox with an F-scaled floor), and `FPSchematicBakeRegionClamp` (a state-key bake region never straddles the seam; the 22.5/67.5 sub-keys collapse to pure slide — no bake). **WI4** the anchor-critical read registration `FPSchematicAnchorAngleForPart`/`FPSchematicAnchorProjectionAt` (the XIV.1 master projection: Theta = θ₀+yaw, Phi = φ₀+pitch, one radius per domain — 1R cranium for eyes/brows/ears, 1.5R jaw for nose/mouth/teeth/chin, +Y up; the 4 silhouettes ride the cranium origin, Neck hangs at −94°, Teeth sit at −52°, cheeks bulge ±30/−35) + `FPSchematicAnchorCriticalInReadBand` (every AnchorCritical part stays inside |Dx|,|Dy| ≤ 1R under ±45° pitch at every yaw — ears fold to back-fuzz past the profile). **WI5** pin lag `FPSchematicLagVelocity` (the II.3 0.20 flicker fraction, clamped) + `FPSchematicChainDecay` (0.70 per link) + the S1 ease-back `FPPinLagCurve`. **WI6** `FPShapeContrastRatio`/`FPShapeContrastPasses` (the XIII.2 ~4:1 rounded:sharp gate) + `FPSchematicShapeContrastForRing` — live canonical data reads 195 round vs 3 sharp corners (the two brow tips + the hair tip), aggregate 65:1 passes. — `TestPhaseIISchmittStep`/`TestPhaseIIProximity`/`TestPhaseIIAnchorRead`/`TestPhaseIIPinLag`/`TestPhaseIIShapeContrast`
- **Bulk Assign + Assign Ops (Phase P3, on the Assign page)** — a 10-state × 3-row bulk-assign grid, each cell colored by coverage (green = fully assigned, amber = partial, dark = empty; mirror `AssignCellState`) with a "Filled X/30" coverage label (`AssignCoverageText`); clicking a cell selects state+layer. Ops row: Clear Row (`ClearAllOverridesForSlot` across the row's states) and Apply to views... (the P2 picker). Below: Performance tier combo (Low/Medium/High → `MaxAsyncTextureCacheSize` via `PerformanceTierCacheSize`, grid 32/64/128 via `PerformanceTierGridSize`) and camera-source combo (`CameraSourceLabels`: PlayerCamera0/PlayerCamera1/SpecifiedActor/SequencerCamera/PreviewActor/Custom → `SetCameraSource`)
- **Per-axis sync (Phase P3)** — `SyncLayerAxisToAllViews` → preset `SyncCanonicalAxisToAllViews`: only the chosen canonical axis (0 PosX, 1 PosY, 2 ScaleX, 3 ScaleY, 4 Rotation) is rewritten across all 9 other views, untouched axes keep their per-view overrides (mirror `SyncAxisDelta`). The former Cross-View Transform section that hosted the Sync-axis row was removed in Phase 3 (sync/copy consolidated into the ONE Copy/Sync panel); the per-axis API remains available and tested
- **Drag-drop on every texture slot (P4)** — every visible texture slot accepts direct drops from the Content Browser or the OS: the SELECTED SLOT panel's three channel thumbs (drop targets assign to that channel), and the hull-review grid's per-state cells (drop assigns that state's albedo; clicking the cell still jumps to the state). `SFaceDropTarget` (Shared.h, one shared drop contract) routes asset drops (`FAssetDragDropOp`/`FContentBrowserDataDragDropOp`) and image-file drops (import + assign), both undo-scoped
- **Import completion (Phase P3)** — the folder wizard's Apply button appends a post-import coverage summary to the status line (`| albedo A/10, normal N/10, depth D/10` via `ImportCoverageSummary`)
- **Selection outline (Phase C)** — the selected layer's art quad is outlined in AccentBlue at 2px, **always visible** (not hover-only), and rebuilt from the active view state's stored transforms on every `RefreshUI` — so selection hugs the art in Profile/Back/¾/Top/Bottom too (the cross-view outline constraint). The quad hit-test/cycle click layer itself was removed in P1 (one map — the schematic glyph is the only pick surface); the `FPLayout::FPHitTopmostQuad`/`FPCycleQuadHit` helpers remain in the manifest library and stay covered by the math tests
- **Unified inspect mode (Phase C, P5; W7)** — the canvas's segmented row is the **single large, clearly-labeled 5-mode primary selector**: **Preview: Textured / Outline / Depth / Wireframe / Heatmap** (replaces the old Textured/Depth/Wireframe/Split display row). **W7 folded the four display-mode toggles into this row:** the old Diagnostics Config checks (Show Textures / Depth Mesh / Wireframe / Color by Depth) were **retired** — the segmented row is the **sole** display-mode control (`SetInspectMode` → `FPLayout::InspectComboForMode`), and the Developer drawer's Config section now keeps only Blinking / Swoosh / Nested Art / Params (4-row `Sec-CFG` manifest, "K of 4 on" disclosure). The row highlight re-derives from the five source flags on refresh (`FPLayout::DeriveInspectMode`; any other combo — e.g. textures+depth — shows no highlight). **P5 demotion:** onion-skin (checkbox + opacity slider), Show Pins, Depth overlay, the edge map, and the Filter row moved into a collapsed **Canvas Options** overflow menu (`SMenuAnchor`, rebuilt fresh on every open so states always reflect the model)
- **Grouped sync control + linked editing to chosen views (Phase D, re-homed in Phase 3)** — the sync row is one grouped control on the Sync + Align page: an explicit **Transform / Textures / Both** op selector (`FPLayout::SyncOpLabel`, `SyncOpHasTransform`/`SyncOpHasTextures`), **Apply to picked** (the always-visible destination grid, via `SyncLayerToSelectedViews`/`SyncTexturesToSelectedViews`) and **Apply to all** (the canonical everything-everywhere action). The Link checkbox broadcasts live edits to the *picked* views only (`FPLayout::FPLinkDestCount`/`FPLinkDestIsPicked` through `GetLinkTargetsForEditing`); with nothing picked it falls back to all 9 views — the Phase B contract
- **Thumbnail-first layer rows + completeness badge (Phase E)** — every layer-tree row now leads with an 18×14 albedo thumbnail of the layer in the active view (gray block when the view has none), followed by a green/amber/red completeness dot (Assigned = albedo+normal+depth, Partial, Missing — `FPLayout::AssignCellState`/`AssignCellLabel`, tooltip shows the exact A/N/D presence per state)
- **Undo/redo keyboard shortcuts (Phase F)** — the ring-buffer undo (`FWidgetUndoScope` → 32-entry stack, `Undo`/`Redo`/`CanUndo`/`CanRedo`) now answers **Ctrl+Z** (undo), **Ctrl+Shift+Z** and **Ctrl+Y** (redo) while the panel holds focus — the tab-activation hook hands focus to the widget (re-entrancy-guarded: `SetKeyboardFocus` inside `OnTabActivated` can re-trigger tab activation and recurse to a stack overflow, so the handler skips re-entrant calls via a shared flag), and the key table is mirrored by `FPLayout::UndoShortcutAction`; focus elsewhere leaves the editor's own global undo untouched
- **World-bound tab lifecycle** — the editor widget and its preview actor live in the current editor world; when that world is discarded (level switch, Live Coding compile), `FWorldDelegates::OnWorldCleanup` closes the tab and drops `EditorWidgetInstance` so the old world's package is GC-able (otherwise the engine fatals with "World Memory Leaks"). PIE world cleanups are ignored — the tab survives play sessions; reopening after a world change re-creates the widget against the new world
- **Undo/Redo toolbar buttons + History menu (P6)** — Undo/Redo sit in the toolbar next to Save (buttons 2/3, `TB-Undo`/`TB-Redo` in the Phase H manifest); the **History ▾** menu (`TB-History`) next to Redo opens the full undo/redo stack (click an entry to revert/re-apply to that point, 32-entry cap, `MaxUndoEntries`) plus Snapshot current state / Restore snapshot; the old single-slot "Snapshot" pair in the bottom bar was relabeled **Backup Point**/**Restore Backup** with an explanatory tooltip clarifying it is a manual safety copy, not multi-step undo. **Action-point flashes (P6)** — `SFaceFlashButton` (Shared.h) is a click-confirmation button that flashes green with a ✓ for ~0.7 s at the point of action: it backs **+ Add Layer**, **Add Pin**, the wizard's **Apply to Active Layer** (which now closes 0.6 s after the flash so the confirmation is seen), and the Param Bindings **Add** button; Clear State/Clear All require a confirming second click; "Log: ON/OFF" collapses the diagnostic log
- **Drag-and-drop texture thumbs (Phase A)** — each Albedo/Normal/Depth thumbnail is a drop target (`SFaceDropTarget`): dropping a Content Browser texture (legacy `FAssetDragDropOp` or `FContentBrowserDataDragDropOp`) assigns it directly to that channel (undo-scoped, auto-fit aware); dropping Explorer image files imports them via `ImportTexturesFromFiles` and assigns the ones whose name suffix matches that channel (`ChannelFromTextureName`), reporting "imported X, assigned Y to <channel>" in the status line
- **Preview FOV slider (Phase A)** — the Camera section now has a 10–90° FOV slider wired to `SetPreviewFOV`/`GetPreviewFOV` with a live readout
- **Duplicate nested element (Phase A)** — nested-outliner rows gain a Dup button that appends a copy of the row's element and selects it (undo-scoped); `DuplicateNestedElement` now supports append destinations (`DestIndex >= count`); `SyncTexturesLayerToAllViews` is BlueprintCallable
- **Dev tools relocated (Phase A, drawer W1)** — Tag Validator and Material Cross-Reference moved out of the bottom bar into the Developer drawer as accordion sections (`Sec-TagValidator`/`Sec-MatCrossRef` in the Phase H manifest); the bottom bar now holds only workflow actions

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
| `SetCameraFollowsView(bEnabled)` / `GetCameraFollowsView()` | Enables/disables orbit camera snapping on view switch; `bCameraFollowsView` property |
| `SnapCameraToActiveView()` | Snaps orbit yaw/pitch to the active state's POSE KEY (`GetYawKeyForState`, the 0/45/90/135/180 swap keys) + `GetZoneCenterPitch`, so the preview parks on the authored pose exactly |
| `ImportTexturesFromFiles(Files)` | Imports image files into `/Game/FaceParallax/Imported` via `FAssetToolsModule` and returns the created `UTexture2D`s |
| `AssignImageDropToSlot(State, Tag, DragEvent)` | Phase F shared drop pipeline: assigns all dropped Content-Browser/OS textures to the scoped slot by channel suffix (Normal/Depth else Albedo); returns whether any were assigned |
| `AssignImageDropToBlinkFrame(State, Tag, FrameIdx, DragEvent)` | Phase F shared drop pipeline: assigns dropped textures to one blink frame of the scoped slot by channel suffix to `SetBlinkFrameTextures` |
| `AssignTextureToSlot(Tex, State, LayerTag, Channel)` | Assigns a texture to Albedo/Normal/Depth of a slot, auto-fits if enabled, refreshes UI |
| `OpenImportArtDialog()` | Native single-art OS file picker (Phase 2 primary import path): filters to droppable image types, imports via `ImportTexturesFromFiles` and assigns to the scoped slot by channel suffix; opened by clicking an artless schematic glyph/legend chip and by the Assign page "Import Art..." button |
| `GenerateDepthFromOutlines(GridSize)` | Arm/confirm-guarded silhouette → depth bake: builds the visual-hull buffer and writes the depth channel of the scoped view states (per-view reprojection) |
| `SetOutlineDepthScope(Scope)` / `GetOutlineDepthScope()` | Bake scope: 0 = front only, 1 = 8 horizontal states, 2 = all 10 states |
| `GetOutlineDepthArmed()` | True between the arm click and the confirming click |
| `SetOutlineOverlayVisible(bVisible)` / `GetOutlineOverlayVisible()` | Toggles the depth-buffer overlay on the preview image |
| `SetOutlineViewEnabled(State, bEnabled)` | Adds or removes a state from `OutlineViewStates` (per-checkbox management; `SetOutlineViewState` only adds) |
| `SetActivePageIndex(Index)` | Switches the context panel page (0 Assign, 1 Transform & Sync, 2 Expression/Blink/Viseme, 3 Preview & Debug, 4 Developer drawer) |
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
| `RegisterPageSection(PageIdx, Title, Target, Accordion?, AccordionIdx?)` | Registers a page section for chips + search jump; accordion sections pass the accordion + index so jumps auto-expand |
| `RegisterAccordionPageSections(PageIdx, Accordion)` | Bulk-registers every accordion section in visual order (Assign 2, Expression 3, Developer 8) |
| `JumpToPageSection(PageIdx, SectionIdx)` | Switches page if needed (pending jump consumed at rebuild end), expands the section, scrolls its header into view, highlights its chip |
| `OnPageSearchCommitted(Query)` | Enter-in-search handler: `FPLayout::FindPageSectionByTitle` across all pages → jump or status message |
| `UpdateDisclosureSummaries()` | Refreshes the Config disclosure "K of 4 on" summary from component toggle state (W7: the four display-mode toggles moved to the canvas mode row, leaving only the four system Config checks) |
| `GetRailWidthPx()` / `SetRailWidthLive(W)` / `ApplyRailWidthDelta(Delta)` | (Kept, legacy) rail width accessor + clamped setter/commit — the context panel is now FIXED at `FPLayout::ContextPanelWidth` and no UI calls these (SetRailWidthLive writes the context-panel width); they remain for the manifest library's `ClampRailWidth`/`RailWidthAfterDrag` mirrors |
| `HandleHotspotClick(RegionName)` | Legend-chip pick (P1 one-map): resolves the region to a layer, selects it, opens the Assign page, sets the breadcrumb, pulses the glyph, and opens the native OS picker preselected on that part (Phase 2) |
| `HandleSchematicPartClick(PartName)` | Canvas glyph pick (P1 router): identical to `HandleHotspotClick` — both delegate to `SelectPartOrImport` (one map, one interaction model) |
| `SelectPartOrImport(PartName)` | P1 one-map core: resolve part → layer (derivation → alias), select, Assign page, `Front → Eyes` breadcrumb (`SetBreadcrumb`), 0.5 s glyph click pulse, and native OS picker (`OpenImportArtDialog`) when the layer has no art |
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
1. **SAMPLES sync** — copies the 23 root source files into `SAMPLES/MyProject/Plugins/FaceParallax/Source/` (runtime Public/Private, editor Public/Private)
2. **Python syntax validator** — braces/macros/includes on all `.h`/`.cpp` files
3. **C++ math tests** — 2668 standalone tests (g++ from msys64 ucrt64), including silhouette-edge distance, visual-hull depth (front view + per-view yaw/pitch variant), camera-snap zone-center mapping, import channel-suffix detection, Phase B–G mirror suites (gizmo mapping both directions + round trip, link broadcast, suffix parser, sync drift, luminance histogram, Sobel edge density, fill ratio, grid columns, sorted-unique dedupe, frame-count mismatch, outline-depth bake quantization, depth-scope targeting), Phase 1 interactive-gizmo mirrors (`GizmoHitTest`/`GizmoApplyDrag`: handle resolution — rotate/scale corners beat the move edge ring, box interior is a deliberate miss so part clicks stay live, rotated-box handle tracking; drag math — move = pixel delta / canvas, rotate = center-angle delta normalized to ±180 and clamped to ±360, uniform scale by center-distance ratio clamped to [0.02, 50] with the per-axis [0.01, 100] clamps applied last; guards for degenerate boxes/anchors, zero/negative canvases, unknown modes), Phase 2 direct-import mirrors (`CanvasDropTargetLayer` part-hit-wins / selected-layer fallback / no-target; `ChannelFromName` suffixless→Albedo default), Phase C canvas selection + inspect mode (quad hit-test topmost/cycling, `DeriveInspectMode`/`InspectModeLabel`), Phase D sync integration (op labels/channels, link-target count and membership with the no-picks fallback), Phase E layer badges + pin manager + undo shortcuts (`AssignCellLabel` 2/1/0 mapping, `FPPinnedRowCount` layer/element/child row totals, `UndoShortcutAction` Ctrl+Z/Ctrl+Shift+Z/Ctrl+Y key table), translation-only pin transforms (master blueprint: 2D art never rotates/scales per-frame — rotation mirrors always return 0, scale mirrors always return 1.0; back-view authoring round-trip, slider normalization, 8-state projection sweep, effective-transform translation-only accumulation, cross-view sync pin preservation), the velocity-hierarchy yaw-rule feed (`FPYawRule::ComputeVelocityOffset` + `FPSchematicTagHasParallaxRate`), Phase H UI design contract (P1–P24 over the layout manifest: zero violations, the exact 5-page context-panel viewport set (pages 621×800 clipped bNoVScroll stacks — the context panel is FIXED at `ContextPanelWidth` = `MainRowWidth − CenterColumnMinWidth` = 1089 − 468 = 621 = the old rail 273 + props 340 + gap 8, with NO internal splitter: resizing only happens at the very outside of the widget), mirrored design constants, anchor-node presence (`CP-Switcher`/`CP-P0-Assign`…`CP-DevDrawer`/`CT-TabRow`), negative controls proving every principle fires, P14 context-panel right-edge gap, P15 no-scroll pages, P24 no-terminal-overlap push-away (every center-column row fits the `MainRowHeight` band), the status-matrix overlap guard (the real layer × state grid is unbounded, so the mirror pages it as `SD-Carousel` and asserts the unpaged table fires P17 while the paged one fits — the hidden-last-row "Hair" defect class), plus section slots auto-stacked so sections never paint over each other), Phase I UI-testing procedures (fit-first P17 / carousel P18 / reserve P19 over the real pages), and Phase 4b accessibility mirrors (page section registry + cross-page search jump), and W1 retired-machinery mirrors (no `PR-Carousel`/`PR-CarouselNav`/`PR-Scroll`, no `RL-*` rail nodes, no `RAIL-Switcher`)
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

Alternatively, use the **Import Art...** button to batch-import texture files from disk (opens the Import Folder Wizard — the bulk import path). Single-art import uses the native OS picker opened from an artless part click.

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
- **Continuous Blending**: Enable `bUseContinuousBlending` for the parameter-space crossfade (the fade spans ±`BlendWindowWidth`° around the hysteresis-adjusted trigger, same angular sweep at any speed); disable for instant hard swaps
- **Jiggle Physics**: Add nested art elements with jiggle for dynamic secondary motion
- **Occlusion Pins**: Use 3D pin projection on nested elements for perspective-aware positioning; with `bEnableViewAngleRotation` the element also rotates around the pin as the camera turns (Min/Max rotation sweep + sensitivity, see "3D Pins & View-Angle Rotation")

---

## Art Viewer (art_viewer.py)

A stdlib-only local dashboard for reviewing the placeholder art library and the composed character views — **it never re-implements a single runtime rule**. Every rule value comes from the compiled system:

- **Bridge** (`art_viewer_bridge.cpp` → `art_viewer_bridge.exe`) — a pure C++17 program built with the same compiler/flags as the math tests that `#include`s the canonical `FaceParallaxSchematic.h`/`FaceParallaxSvgParse.h` and prints the per-state truth as JSON: the 14 state tokens + centers + walk-behind flags, the 17 part↔feature pairs, per-(state × part) visibility + Z-order (`FPSchematicLayerVisibleInState`/`OrderInState`), and per-(state × feature) resolved cell keys (`FeatureCellKey(Feature, CollapseViewStateForFeature(F,S), 0)`).
- **Composition** — the viewer extracts the resolved cells verbatim from `Art/_grids/<Feature>.svg` (the same single import source the runtime albedo bake uses) and stacks them far-to-near into `Art/_views/View_<StateToken>.svg`, one composed character view per state — exactly as the editor canvas paints them. Missing cells and E10 empty cells raise hard errors.
- **Dashboard** — a single true 3×3 grid container (Front center, Top above, Back bottom-center, left half on the left) plus a 5-strip of transition states, and a gallery of EVERY authored SVG from `Art/_tokens.json`. The character-views section is **exactly one full window height** (the grid stretches between the top bar and the transition strip, so the art always fits the window height; the floors — section 860 px, grid 620 px, rows `minmax(200px, 1fr)` — keep every character at least 200 px tall on short windows, where the page scrolls instead of collapsing, and guarantee the grid can never overflow onto the strip) and the **library section below is reached by normal page scrolling**; every piece renders on a **light canvas so the dark line art reads at full contrast**, and **clicking any art piece opens it in a large lightbox sized to the browser height** (300 px minimum, up to 92 vw × 88 vh, so it can never overflow the window — Esc / backdrop / × closes). **Regenerate all art** runs the system's `generate_art.py`, recompiles the bridge, re-emits the 14 views; **Rebuild views** just recompiles + re-emits. Path traversal is rejected (regex-whitelisted names).

```powershell
py art_viewer.py                 # serve the dashboard (opens the browser, port 8765)
py art_viewer.py --port 9000 --no-open
py art_viewer.py --emit-only     # compose the 14 views headless
```

When any schematic header changes, the viewer's staleness check rebuilds the bridge automatically, so the dashboard always shows the current system's truth.

---

## Default Property Values

| Property | Default | Category |
|---|---|---|
| `HeadBoneName` | "head" | Skeletal Mesh |
| `bAutoSpawnLayerQuads` | true | Layer Quads |
| `LayerMaterialPathRoot` | "/Game/FaceParallax/Materials/Instances/MI_FaceParallax_" | Layer Quads |
| `LayerQuadWorldWidth` | 100.0 | Layer Quads |
| `LayerQuadLocalOffset` | (10,0,0) | Layer Quads |
| `TopViewPitchThreshold` | 45.0 | View Angles |
| `BottomViewPitchThreshold` | -45.0 | View Angles |
| `HalfZoneWidth` | 45.0 | View Angles |
| `ZoneBoundaryMultipliers` | {1,2,3,4} | View Angles |
| `CrossfadeSpeed` | 15.0 (no-op) | Transitions |
| `HysteresisFrames` | 3 (jitter backstop) | Transitions |
| `AngleHysteresisBuffer` | 1.5 (°) | Transitions |
| `MaxParallaxOffset` | 5.0 | Parallax |
| `MaxVerticalParallaxOffset` | 3.0 | Parallax |
| `DepthMapIntensity` | 1.0 | Depth Maps |
| `bUseMaterialDrivenDepth` | true | Depth Maps |
| `bUseContinuousBlending` | true | Transitions |
| `BlendWindowWidth` | 0.75 (half-width °) | Transitions |
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
FaceParallaxLayoutSpec.h                 — Phase H layout manifest + P1..P24 design validator
FaceParallaxEditorSubsystem.h/.cpp       — Editor subsystem (toolbar, tab; deployment is deploy.py)
deploy.py                                — THE deployment script: creates every binary asset in-editor
generate_art.py                          — Placeholder art library generator (parses FaceParallaxSchematic.h)
smooth_art.py                            — Art-attractiveness engine (spline smoothing + feature construction)
art_viewer_bridge.cpp                    — Art-viewer bridge (pure C++17, prints the canonical per-state truth)
art_viewer.py                            — Art viewer dashboard (stdlib-only; compose + serve)
FaceParallaxModule.cpp                   — Runtime module entry (IMPLEMENT_MODULE)
AGENTS.md                                — Agent guide with rules and test info

Tests/
  ParallaxMathTests.cpp                  — 2668 standalone C++ tests
  SyntaxValidator.py                     — Python syntax validation
  validator_silhouette.py                — Silhouette geometry validator (E11 gates)
  run_tests.ps1                          — Test runner

SAMPLES/MyProject/                       — Standalone UE5 project (plugin copy) for CI builds
```
