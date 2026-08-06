# Anime Head Line Art & 2D Pin-Rig Animation Manual

A programmatic, coding-centric construction and rigging reference for building a monoline anime character head as a fully rotatable, parallax-driven 2D pin rig with deterministic math rules for auto 360-degree rotation and hard asset swaps.

### Revision Notes

This manual is the technical specification for the software implementation of a monoline anime character head rig — a fully rotatable, parallax-driven 2D pin rig with hard asset swaps and no deformation of the source art. It is the programmatic counterpart to `art_guide.md` (the art-principle reference). It is organized as follows:

- **Parts 0–IX** — the core contract: scope and axes (0), parametric facial construction (I), layer architecture and pin hierarchy (II), the parallax displacement math (III), the yaw state machine (IV), pitch rotation (V), the bilinear blend space and residual correction (VI), masking and expression states (VII), asset naming (VIII), and the template/target parity system (IX).
- **Part X — Cutout Rigging Principles (Technical Specification).** The auditable rule set the no-deformation contract depends on, as a spec checklist: the multiplane parallax principle, the art-swap/angular-discretization principle, the swap-set and crossfade principles, the articulated-cutout principle, the atmospheric-veil principle, the hand-moved jitter principle, the **18-commandment audit checklist**, and the programmatic audit pseudocode.
- **Part XI — Enumerated View Matrix.** The programmatic enumeration of all 24 primary cells (8 yaw × 3 pitch) + 2 sub-threshold rows + the mirror shortcut and its three exception classes. Includes the per-cell contract (cohort, visibility, depth, authority) as a data structure.
- **Part XII — Cross-View Consistency Validation.** The algorithmic checks an automated validator runs against every authored cell: Reference Cross continuity, the five anchor registrations, foreshortening math, anchor-critical vs. bridge-safe visibility contract, mirror-vs-reauthor classification.
- **Part XIII — Attractiveness Metrics.** Programmable appeal checks: the cardioidal-strain ratio table (realistic vs. anime feature placement), the ~4:1 round-to-sharp shape-contrast counter, the baby-schema feature-set membership test, the eye-highlight placement validator, and the uncanny-valley no-deformation enforcement (a programmatic assertion that no source-art vertex is moved at runtime).
- **Part XIV — Canonical Math Reference.** The code-friendly forms of every formula (spherical projection, smoothstep S₁/S₂/Sₙ, directional Schmitt trigger, parameter-space crossfade α, local-delta-reset T(θ), cosine foreshortening with fold-clamp, clamped inverse-proximity F_prox) with explicit context selectors.
- **Part XV — Atmospheric Perspective & Depth Haze.** The per-layer translucent haze (opacity `1 − e^(−k·Z)` scaled by a `mist` parameter), the per-pixel compositing operation, the seam-margin interaction, the melt-away crossfade, and the optional hand-moved jitter.
- **Part XVI — Anime Girl Proportions & Personality (Owner's Preference).** The programmatic proportions reference: front-view measurements, eye-shape variants, per-view deltas, hair design, personality archetypes, and the Owner's Preference slider config.
- A **Glossary** of the rig's domain terms.

**Prior edition (verified-formula pass).** Verified every formula against the geometry it's supposed to satisfy, and fixed what didn't check out. Grouped by severity:

**Breaking / domain errors**
- `phi0 = arcsin(y/R)` is undefined for `|y| > R`. The chin, nose, and mouth all sit below `y = -R` (they live on the separately-constructed jaw Bezier, not the cranium sphere), so their spherical authoring angles were mathematically undefined. Introduced a second authoring radius, `R_jaw`, so the lower face has a valid domain — see I.6.
- The eye baseline (`I.4`) contradicts its own stated derivation: "midpoint between cranium top and chin bottom" computes to `-0.25R`, not the `-0.75R` given. Nose baseline, mouth baseline, and `W_face` all inherited the error; all three are recomputed below.

**State-machine / timing consistency**
- `II.2`'s depth-reorder pseudocode re-tested the raw yaw/pitch angle instead of reading the Schmitt trigger's state flag, so the depth stack and the asset swap it must accompany could fire on different frames. Rewired to share state.
- The Local Delta Reset (`III.6`, `IV`) rebased against the *nominal* threshold value (e.g. `sin(45.1°)`) instead of the angle the Schmitt trigger actually fired at (`46.6°` rising / `43.6°` falling, given the 1.5° hysteresis band). This leaves a small residual offset — not zero — at every swap after the first. Fixed by capturing `theta_fired` at the trigger event.
- `I.5`'s per-view shortcuts substituted the raw yaw/pitch value directly (`sin(45°)`, `cos(45°)`) instead of `Theta = theta0 + theta` from `III.4` — which implicitly assumes every feature sits at `theta0 = 0`. The far eye's actual `theta0 ≈ -23°`, so its true azimuth at 45° yaw is ≈22°, not 45° — the shortcut overcompresses it substantially.

**Geometric inconsistencies**
- The Hairline Arc (`I.2`) was specified as a fixed-radius circle (`0.9R`) but is required to widen out to meet the jaw origin (`R, 0`) at the equator — a circle can't do both. Replaced with a radius function of elevation.
- The Chin Bezier's second control point forces a fully horizontal tangent at the apex, which draws a rounded cup, not the "slightly blunted V" the construction goal calls for. Adjusted.
- Profile's jaw-origin anchor, `(0,0)`, is the cranium's *center* — not a point on any surface, and if derived by rotating the front-view jaw hinge to 90° yaw, it lands on a point with `Z_sort < 0` (occluded). Replaced with a properly derived surface point.
- The Bottom View's neck insertion point, `(0, -R)`, sits *above* the chin (`-1.5R`), which can't be right for a point the neck attaches below. Corrected.
- `V.2` (Top View, post-swap, 0% feature visibility) contained a leftover eye-deformation line that belongs to `V.1`'s pre-swap parallax zone. Relocated and recomputed.

**Completeness (facial feature placement, all views)**
- `I.6` had a coordinate formula only for eyes. Added brows, nose, mouth, and hair-ribbon roots, each placed via the corrected domain model, plus a consolidated per-feature/per-view coordinate table.
- `I.3` gave only the centerline/browline *intersection point*, not the drawable curves. Added the full parametric definitions, piecewise across the cranium/jaw domain boundary.
- `VI` described bilinear residual correction only in prose. Added the explicit formula and flagged the pitch 45°→90° wedge, where no yaw-corner grid exists to interpolate against.
- Confirmed the Residual Correction sign convention (`E = P_art - P_math`) is correct as written here — and flags that the previous (prose) edition had it backwards.

**Hardening**
- `III.6`'s rounding rule now accounts for device pixel ratio, not just `Math.round()`.
- `IX.2`'s validation sweep extended to negative yaw (exercises the mirror path) and explicit boundary-value tests at every hysteresis-adjusted threshold, not just a uniform step sweep.
- Restored the `Y22`/`Y67` sub-threshold naming tokens to `VIII` — `IV` already uses these zones, but the naming section didn't have tokens for them.

---

## PART 0 — Scope, Axes & Conventions

Coverage: single character head/neck unit, monoline line art, non-destructive 2D pin rig (deformer + hard-swap hybrid cutout rig).

Rotation Parameters:

- Yaw (X-axis turn): -180 deg to +180 deg, where 0 deg = full front, +180 deg = full back. Positive values turn the character toward camera-right.

- Pitch (Y-axis tilt): 0 deg = eye-level neutral. Range extends to true zenith at +90 deg and true nadir at -90 deg. +45.1 deg is the hard-swap threshold into the single Top View asset; -45.1 deg is the hard-swap threshold into the Under-Plane asset.

- Yaw Sub-Thresholds: 22.5 deg (`Eye_Far_Narrow`) and 67.5 deg (`Eye_Far_Sliver`, `Eye_Near_3Q`). Programmatically treated as internal state triggers, identical in mechanism to the primary thresholds — see `IV.0`.

Projection Model & Camera Independence: every rotating anchor (eye, brow, nose dash, projection root, hair root) is authored with an initial spherical position `(theta_0, phi_0)`. The Master Rotation Parameter `(theta, phi)` computes a normalized vector-space offset. **Two authoring radii exist, not one** — `R_cranium` for anchors that sit on the upper-head circle (eyes, brows, ears, upper projections), and `R_jaw` for anchors that sit on the separately-constructed jaw curve (chin, nose, mouth). See `I.6` for why, and the domain rule that decides which an anchor uses. Camera proximity (`III.5`) is a separate, camera-level parameter that scales the *magnitude* of the resulting offset — it never changes which hard-swap threshold fires, since thresholds are defined purely in rotation-parameter degrees, never in screen distance.

---

## PART I — Facial Construction Geometry & Parametric Generation

To generate line art deterministically without deformation, the structural anchors must map to precise mathematical coordinates. The origin `(0, 0)` is defined as the center of the Cranium Anchor.

### I.1 The Monoline Constraint & Vector Pathing

Every line must be defined as a flat SVG/Vector path (e.g., cubic Bezier) with a strictly locked stroke width `W`. Scaling functions like matrix `[Sx, 0; 0, Sy]` are strictly forbidden on the rendering layer to prevent stroke warping. All visual compression is handled via pre-drawn swapped assets and translation vectors `[Vx, Vy]`.

### I.2 Parametric Cranium & Jaw Foundation

- Cranium Anchor: A perfect circle with radius `R` (= `R_cranium`). Origin `C = (0, 0)`.

- Equator & Jaw Origin: Horizontal line `y = 0`. Left/right intersections at `x = -R` and `x = R`.

- Chin (V-Apex): The lowest point sits exactly at `y = -1.5R`, `x = 0`.

- Jaw Curve Construction (Bezier): Drawn via cubic Bezier from `(R, 0)` to `(0, -1.5R)`. `CP1 = (R, -0.75R)` (shares the start point's x, so the initial tangent runs straight down — this is what pushes the curve outside the cranium's own circular drop, since the true circle would already be curving inward by this point). `CP2 = (0.5R, -1.5R)`.

  **Fix — chin tangent:** `CP2` sharing the endpoint's y-coordinate forces a fully horizontal tangent at the chin, which draws a rounded/flat cup, not a "slightly blunted V." Move `CP2` to `(0.4R, -1.42R)` — close to the endpoint but not flush with it, so the curve approaches the apex at roughly 25-30° off vertical rather than 0°. This keeps a legible V-angle while still rounding off what would otherwise be a sharp point. Mirror for the left-side curve.

- Hairline Arc: **Fix — not a fixed-radius circle.** A circle of radius `0.9R` centered at the origin intersects the equator at `x = ±0.9R`, but the arc is required to widen out to meet the jaw origin `(±R, 0)` exactly at the equator — those two requirements can't both hold for one constant radius. Define the radius as a function of elevation `psi` (measured up from the equator, `psi = 0` at equator, `psi = 90°` at crown):

  ```
  R_hairline(psi) = R * (0.9 + 0.1 * cos(psi))
  ```

  Check: `psi = 0 → R_hairline = R` (meets the jaw origin exactly). `psi = 90° → R_hairline = 0.9R` (10% inset at the crown, as specified). The derivative at `psi = 0` is `-0.1R * sin(psi) = 0`, so the arc meets the jaw origin with a flat tangent — no seam kink, consistent with `I.7`'s Curve Continuity standard.

### I.3 The Rotational Reference Cross

The centerline and browline are deterministic traces of a 3D sphere projected onto a 2D plane — but the centerline crosses *both* domains (cranium above the equator, jaw below it), so its formula is piecewise. The single-point formula given previously (`x = R cos(phi) sin(theta), y = R sin(phi)`) is the special case of this at `psi = 0`; the full drawable curves are below.

**Centerline — upper segment** (crown to equator, parametrized by `psi ∈ [0°, 90°]`, `psi = 90°` at crown):

```
x(psi) = R_cranium * cos(psi + phi) * sin(theta)
y(psi) = R_cranium * sin(psi + phi)
```

**Centerline — lower segment** (equator to chin, parametrized by `psi_jaw ∈ [0°, 90°]`, `psi_jaw = 90°` at chin):

```
x(psi_jaw) = R_jaw * cos(psi_jaw + phi) * sin(theta)
y(psi_jaw) = -R_jaw * sin(psi_jaw + phi)
```

Check continuity at the equator: both segments give `(0, 0)` at their `psi = 0` end when `theta = phi = 0`, and at `psi_jaw = 90°, theta = phi = 0`, the lower segment gives `(0, -R_jaw) = (0, -1.5R)` — the chin, as required.

**Browline** (fixed elevation at the eye baseline, swept across azimuth `beta` — the visible half-width of the face):

```
x(beta) = R_cranium * cos(psi_brow + phi) * sin(beta + theta)
y(beta) = R_cranium * sin(psi_brow + phi)     [constant across beta]
```

where `psi_brow = phi0_eye ≈ -14.5°` (the eye baseline's own elevation — see `I.4`/`I.6`). Sample `beta` at regular intervals (15° steps is a reasonable default) and connect with the Curve Continuity standard (`I.7`) rather than a stitched polyline.

Redraw or reference this cross at every hard-swap threshold, including the two sub-thresholds. Pitch bows both curves the same way, orthogonally — already folded into the `+ phi` term above. Rotation order is fixed: yaw is applied before pitch everywhere in this manual (`III.4`).

### I.4 Programmatic Feature Placement (The 5-Part Grid — Front View 0 deg)

For the 0° (front) view, the absolute bounds are `X` between `-R` and `R`.

- **Eye baseline — corrected:** stated derivation is "the exact midpoint between the top of the cranium circle and the bottom of the chin." Top of cranium = `+R`; bottom of chin = `-1.5R`. Midpoint = `(R + (-1.5R)) / 2 = -0.25R`.

  ```
  y_eye_baseline = -0.25R      [was: -0.75R]
  ```

- **`W_face` — derive, don't hardcode:** rather than an independent approximate constant, solve the Jaw Curve Bezier (`I.2`) for the `x` where `y = -0.25R`. Setting up `y(t) = -R*(2.25t - 0.75t^3)` from the Bezier's own control points and solving `2.25t - 0.75t^3 = 0.25` numerically gives `t ≈ 0.112`, and evaluating `x(t)` at that `t` gives `x ≈ 0.982R`. So:

  ```
  W_face ≈ 1.964R      [was: an independent constant, ≈1.8R]
  ```

  Deriving it this way means `W_face` can never silently drift out of sync if `R` or the Bezier control points are retuned later — implement as a cached Newton's-method solve against the live Bezier definition, not a hand-computed literal.

- **5-Part Math — flag the ratio, don't hardcode it:** `W_eye = W_face / EYE_RATIO_DIVISOR`. Setting `EYE_RATIO_DIVISOR = 5` (the literal "5 equal segments" reading) gives `W_eye ≈ 0.393R` — but 1/5 is XVI.1's **realistic** column, not its **anime-default** column (1/4). Every downstream anchor formula that consumes `W_eye` (`I.6`, `III.4`, `XII.2`'s `gap0 * cos(theta)`) was being driven by the realistic ratio while XVI.1 tells the artist to build the anime-default proportions — the two would silently disagree for any character actually built to XVI's numbers. Fix: read `EYE_RATIO_DIVISOR` from XVI.1's `eye_width_to_face_width` table (default 4, i.e. `W_eye ≈ W_face / 4`) instead of hardcoding 5; treat 5 as the "realistic" preset alongside XVI.1's other presets (moe: 3.5, mature: 6), not as the default.

- **Nose Baseline — recomputed.** Distance from eye baseline to chin: `-0.25R - (-1.5R) = 1.25R`. At 60% of that distance down from the eye baseline:

  ```
  y_nose = -0.25R - 0.60 * 1.25R = -1.00R      [was: -1.2R]
  ```

- **Mouth Baseline — recomputed.** Stated range is 80-85% down. For a single deterministic value, use the midpoint (82.5%):

  ```
  y_mouth = -0.25R - 0.825 * 1.25R ≈ -1.28R      [was: -1.35R]
  ```

  Range endpoints, if a tunable spread is preferred: 80% → `-1.25R`; 85% → `-1.3125R`.

- Compressed Grid Math (Far Side): at yaw `theta`, the visible width of a far-side segment centered at azimuth `alpha` compresses proportionally to `cos(alpha + theta)`.

  **Fix — clamp required.** Once `alpha + theta` exceeds 90°, `cos()` goes negative — that's not a "negative width," it's the segment having rotated past the visible limb into full occlusion. Clamp before applying:

  ```
  compression_factor = max(0, cos(alpha + theta))
  ```

### I.5 Parametric Shifts for Discrete Views (3/4, Profile, Top, Bottom)

To maintain absolute consistency, hand-authored swap assets must perfectly align with these programmatic boundary shifts. **General fix applying to this whole section:** every shortcut below that plugged the raw view angle (`45°`, `90°`) directly into `sin()`/`cos()` was implicitly assuming `theta0 = 0` for the feature in question. That's true for the Face Base silhouette's own defining points (they sit near the pole), but false for any offset feature. Use `Theta = theta0 + theta` (`III.4`) generally; the corrected values below show why it matters.

- **The 3/4 View (Yaw 45°):** the far eye's own `theta0 ≈ -23.1°` (derived in `I.6`). Its true total azimuth at 45° yaw is `Theta = -23.1° + 45° = 21.9°`, not `45°`. Compression factor `cos(21.9°) ≈ 0.928`, not `cos(45°) ≈ 0.707` — the flat shortcut overcompresses the far eye by a wide margin. Recompute per-feature using each feature's own `theta0`; do not reuse the view's yaw value as a stand-in for every anchor's `Theta`.

- **The Profile View (Yaw 90°):** entire far hemisphere mathematically triggers `Z_sort < 0`, setting visibility to `0.0` — this rule is correct and general; it's the same `Z_sort = R cos(Phi) cos(Theta)` test from `III.4`, and should be used as the rig-wide occlusion test everywhere, not restated as a one-off for profile.

  **Fix — nose tip.** Previously stated `Y = -1.2R`, inherited from the uncorrected Nose Baseline. Corrected: `Y = y_nose = -1.00R`. For the X-extension, using the corrected two-radius model: `phi0_nose ≈ -41.8°` (see `I.6`), so at profile (`Theta = 90°, phi = 0`):

  ```
  x_nose_tip = R_jaw * cos(phi0_nose) * sin(90°) = 1.5R * cos(-41.8°) ≈ 1.12R
  ```

  This closely matches (and now formally derives, rather than just asserts) the previously-stated `1.15R` — treat `1.15R` as an acceptable small artistic exaggeration past the geometric baseline if desired, not as an independent magic number.

  **Fix — jaw/ear connection point.** Previously stated as starting from `(0, 0)`. That coordinate is the cranium's *center*, not a surface point — a curve can't originate there. If it was derived by rotating the front-view jaw-origin point (`theta0 = 90°`, the equator's lateral edge) forward to 90° yaw, it lands at `Theta = 180°`, which does project to `(0, 0)` numerically — but at `Theta = 180°`, `Z_sort = R cos(0) cos(180°) = -R`, meaning that specific point is fully occluded (behind the head) at profile. It's the wrong point to build a *visible* silhouette from. Author an explicit hinge elevation instead — e.g. `phi_hinge ≈ -12°` (ear/jaw hinge sits slightly below eye-level) — and derive the visible surface point properly:

  ```
  x_hinge = R_cranium * cos(phi_hinge) * sin(90°) ≈ 0.978R
  y_hinge = R_cranium * sin(phi_hinge) ≈ -0.208R
  ```

  Use `(0.978R, -0.208R)` as the profile jaw's start point, not `(0, 0)`.

- **The Top View (+90° Pitch):** frontal feature `Y`-centers translate to `R * sin(90°) = R` — correct as a limiting formula, though moot in practice since Primary Features are already at 0% visibility by this pitch (`II.2`); relevant only as the continuous limit approaching the swap, not as a "current state" at 90° itself.

  **Fix — hand-wavy forehead line.** "Forehead geometry expands to occupy 80% of the upper cranium radius" doesn't match the rigor of the rest of this section. Replace with an explicit radius: the Top asset's visible forehead/crown boundary is a circle of radius `0.8 * R_cranium` concentric with the cranium origin, drawn as a distinct silhouette ring inside the outer Hairline Arc ring (`I.2`).

  **Fix — misplaced eye-deformation line.** "Eyes shift to `Y = -0.1R` and lose 70% of vertical bounding box height" describes pre-swap deformation and does not belong in this (post-swap, 0%-visibility) section — see `V.1`, Zone P2, where it's relocated and recomputed correctly.

- **The Bottom View (-90° Pitch):** jawline baseline translates upward in screen space, occluding the face base; chin overlaps the nose bounding box.

  **Fix — neck insertion point.** Previously stated as `(0, -R)`. `y = -R` sits *above* the chin (`-1.5R`) — that can't be where the neck attaches, since the neck has to insert at or below the jaw's lowest point. Corrected:

  ```
  neck_insertion_point ≈ (0, -1.6R)
  ```

  slightly past the chin apex, consistent with the Neck Patch's proximity-scaled seam margin (`II.4`) needing headroom to stretch toward `-90°` on this same asset (`V.4`).

### I.6 Volumetric Anchor Coordinates & Feature Algorithms — Two-Radius Domain Model

`I.2` already defines the head as two separate constructed pieces — a perfect circle (cranium) and a Bezier (jaw) — not one continuous sphere. The rotation math should mirror that split rather than forcing every anchor onto one shared sphere with radius `R`:

```
R_cranium = R                     — eyes, brows, ears, upper projections (|y| <= R)
R_jaw     = 1.5R                  — chin, nose, mouth (structurally on the jaw curve, not the cranium circle)
```

`R_jaw = 1.5R` is calibrated so the chin — the jaw's own farthest point, at `y = -1.5R` — sits exactly at that sphere's pole (`arcsin(-1.5R / 1.5R) = -90°`), the same way the cranium's own farthest point (the crown) sits at `R_cranium`'s pole.

Record every coordinate as a rigid angular offset vector `vector A = [theta_0, phi_0]`, using whichever `R` its domain calls for consistently across both the `x` and `y` terms of the projection formula (`III.4`) — never mix `R_cranium` in one axis and `R_jaw` in the other for the same anchor.

**Eyes** (`R_cranium` domain):

- Global anchor: center at `(x_eye, y_eye)`. Horizontal offset from centerline: eye center sits at `1.0 * W_eye` from centerline (midpoint between the inner canthus at `0.5 W_eye` and the outer corner at `1.5 W_eye`, per the 5-Part Grid). At the anime-default divisor of 4 (I.4), `W_eye ≈ W_face/4`, giving `x_eye ≈ 0.491R` — not the `0.393R` a literal 1:5 reading would produce; recompute `theta_0_eye` below from whichever divisor the character actually uses. Vertical: `y_eye_baseline = -0.25R`.

  ```
  theta_0_eye = arcsin(x_eye / R_cranium)   ≈ 29.4° at the anime-default divisor  [was: fixed at 23.1°, silently assumed the realistic 1:5 divisor]
  phi_0_eye   = arcsin(-0.25)  ≈ -14.5°
  ```

- Local shape (eye-anchor-relative coordinates, origin at the eye's own center): upper lash Bezier from `(-0.5 * W_eye, 0)` to `(0.5 * W_eye, 0)`, control point pushed up by `+0.7 * W_eye`.
- Iris: concentric fill, radius `≈ 0.45 * W_eye`. Pupil: concentric fill, radius `≈ 0.20 * W_eye`. Both flat solid fills, no gradient. Highlights: solid light-fill shapes layered on top, not lines.

**Brows** (`R_cranium` domain, one full eye-height above the eye's upper lash):

```
H_eye = 0.75 * W_eye                                  [midpoint of the 70-80% range]
y_upper_lash = y_eye_baseline + H_eye
y_brow = y_upper_lash + H_eye = y_eye_baseline + 2*H_eye
```

This was left as an uncleaned scratch derivation with a leftover "wait" and a sign error, and it hardcoded the old fixed `W_eye ≈ 0.393R` value that I.4's fix above replaced with a parameter — recompute both lines from whichever `W_eye` the character actually uses:

```
theta_0_brow ≈ theta_0_eye  (same horizontal center; brow spans a slightly wider range)
phi_0_brow = arcsin(y_brow / R_cranium)
```

Construct as a single Bezier from `(x_eye_inner, y_brow)` to `(x_eye_outer, y_brow)`, control point offset upward by `≈ 0.15 * W_eye` for arch (0 offset for a straight brow variant). For the "few degrees of tilt/arch asymmetry between the two brows" rule (`I.6`, original), vary the control-point offset by a small fixed delta between the two sides — e.g., left brow at `0.15 * W_eye`, right brow at `0.12 * W_eye` — rather than mirroring identically.

**Nose** (`R_jaw` domain — this is the anchor that broke `arcsin` before the domain fix):

```
theta_0_nose = 0
phi_0_nose = arcsin(y_nose / R_jaw) = arcsin(-1.00 / 1.5) ≈ -41.8°
```

Indicator: small triangle or dash centered at `(0, y_nose) = (0, -1.00R)`, illustrative default size `width ≈ 0.08 * W_eye`, `height ≈ 0.12 * W_eye`.

**Mouth** (`R_jaw` domain):

```
theta_0_mouth = 0
phi_0_mouth = arcsin(y_mouth / R_jaw) = arcsin(-1.28 / 1.5) ≈ -58.6°
```

Width `= W_eye`, centered at `(0, y_mouth)`. Construct as two short Bezier segments (not one continuous curve) with a small fixed gap between them at `x = 0` — e.g. `gap_width ≈ 0.1 * W_eye` — this is what produces the "dead-center gap, no corner dots" read rather than a single notched curve.

**Chin** (`R_jaw` domain, pole point): `theta_0 = 0`, `phi_0 = -90°` exactly.

**Hair ribbon roots** (`R_cranium` domain for the *root attachment*, per the Hairline Arc; the ribbon *shape* itself extends outward past the cranium silhouette): roots attach along `R_hairline(psi)` (`I.2`'s corrected radius function), parametrized by azimuth. Each ribbon's own inner boundary (where the drawn shape begins, distinct from the root's attachment point) sits `10-15%` *outside* the cranium circle — `R_ribbon_inner ≈ 1.125R` (midpoint default) — before sweeping outward in the S-curve rhythm (`I.6`, original) to its tip.

**Ears** (`R_cranium` domain — never previously given coordinates despite being anchor-critical in `XII.4` and carrying a displacement peak in `III.1`):

```
theta_0_ear ≈ theta_0_brow + 0.15R  (tucked just outside the brow's horizontal extent)
phi_0_ear_top    = phi_0_brow            (top edge level with the browline)
phi_0_ear_bottom = phi_0_nose            (bottom edge level with the nose baseline)
```

One closed monoline shape per ear, single interior fold-line, no separate lobe unless the design calls for it.

**Neck** (not a `theta_0/phi_0` anchor — constructed relative to the jaw curve's endpoints, not the sphere): two curves dropping from the jaw origin points (`I.2`), width `≈ 0.4 * W_face` at rest (`XVI.7` anime-default), widening slightly toward the shoulders. Modeled as its own closed shape (Neck Outline) independent of the Jaw Curve, so `II.2` can reorder it independently.

**Teeth** (not a standalone anchor — a conditional sub-shape of the Mouth asset): drawn only when `viseme in {A, I}` (`VII.2`); a two-line upper/lower ridge inside the mouth shape at the locked stroke width `W`. No coordinate of its own — it inherits the Mouth anchor above.

**Per-Feature / Per-View Coordinate Summary** (neutral pitch, front-authored values; apply `Theta = theta0 + theta`, `Phi = phi0 + phi` per `III.4` for any other view; eye/brow `theta_0` and `x` below are shown at the anime-default divisor of 4 per `I.4`'s fix — recompute if a character uses a different divisor):

| Feature | Domain | theta_0 | phi_0 | Front (x, y) |
|---|---|---|---|---|
| Near/Far Eye (center) | R_cranium | ±29.4° | -14.5° | (±0.491R, -0.25R) |
| Near/Far Brow (center) | R_cranium | ±29.4° | recompute per fix above | (±0.491R, recompute) |
| Nose | R_jaw | 0° | -41.8° | (0, -1.00R) |
| Mouth | R_jaw | 0° | -58.6° | (0, -1.28R) |
| Chin apex | R_jaw | 0° | -90° | (0, -1.50R) |
| Ears | R_cranium | ≈ Brow theta_0 + offset | Brow phi_0 to Nose phi_0 | — (spans two anchors, no single point) |

### I.7 Appeal & Silhouette Principles — Checkable Form

Translated from qualitative rules into checks a build script can actually run:

- Silhouette Read Test: rasterize the flattened silhouette to a single-color mask; run connected-component analysis. A passing asset produces exactly one component with no internal holes larger than a small noise-floor area — a hair/jaw overlap that splits the mask into two components, or leaves an unintended gap, fails automatically. Run this per hard-swap and sub-threshold asset independently, not only at front view.
- Shape Contrast: tag each path segment as round (arc/Bezier with low curvature variance) or sharp (near-straight segments meeting at an angle below some threshold, e.g. 150°). Target ratio: round-to-sharp `>= 4:1`. Flag any asset that falls below it.
- Curve Continuity: reject any silhouette edge built from more than one path object where a single continuous Bezier chain could do the job — check for `moveTo` discontinuities mid-silhouette.
- Gap Rhythm Consistency: collect the eye gap, brow-to-eye gap, and nose-to-mouth gap as measured values; compute their variance against a common unit `U` (their mean). Flag if any individual gap deviates from `U` by more than `~15%`.
- Bounding-box overlap check (kept from this edition's original `I.7`): if bounding box `B_hair` and `B_jaw` overlap without a defined Z-gap (`II.4`), log an aesthetic failure.

---

## PART II — Layer Architecture & Pin Hierarchy

### II.1 Baseline Z-Depth Stack (0 deg default)

Array index controls render order (0 = Top, 11 = Bottom).

1. Extended Projections
2. Front Bangs
3. Hair Shadows
4. Primary Features
5. Side Hair (near)
6. Face Base
7. Base-Anchored Projections
8. Side Hair (far)
9. Neck Patch
10. Neck Outline
11. Back Hair

### II.2 Programmatic Depth Reordering — Fixed for State Consistency

**Fix.** The original pseudocode re-tested the raw angle independently of the Schmitt trigger's hysteresis state:

```text
// BEFORE — can desync from the asset swap
if (yaw >= 90.1) { swapIndex(SideHairNear, SideHairFar); }
```

This fires at the bare threshold, while the corresponding asset swap (governed by `IV.0`'s Schmitt trigger) actually fires at `90.1 + 1.5 = 91.6°` rising or `88.6°` falling — a window where the depth order and the art can disagree, which is precisely the failure `II.2`'s own rule prohibits ("never on an adjacent frame"). Fix: read the same state flags the Schmitt trigger sets, rather than re-deriving them:

```text
// AFTER — single source of truth
if (state.yaw_zone_90 == true)  { swapIndex(SideHairNear, SideHairFar); }
if (state.yaw_zone_180 == true) { moveToFront(BackHair); moveToBack(FaceBase); }
if (state.pitch_zone_45 == true){ setVisibility(PrimaryFeatures, 0.0); }
```

where `state.yaw_zone_90` etc. are the exact booleans flipped inside `IV.0`'s trigger branches, evaluated once per cycle and shared by both systems.

Yaw stack logic must explicitly resolve before Pitch stack logic per cycle (unchanged — this ordering rule was already correct and is what makes a diagonal crossing deterministic).

**Missing case: the effects tier.** The emotion effects tier (XVII.6) is defined as sitting above Primary Features by name, not by tracked index — but `moveToFront(BackHair)` above changes what "above Primary Features" actually means in the stack at `yaw_zone_180`. Add an explicit rule:

```text
if (state.yaw_zone_180 == true) {
    moveToFront(BackHair);
    moveToBack(FaceBase);
    pinAbove(EffectsTier, topOfStack());   // re-anchor relative, not absolute
}
```

Without this, a temple vein or sweat drop authored against the front-view stack order silently renders underneath `BackHair` the instant this reorder fires.

### II.3 Kinematic Pin Taxonomy

1. Positional Pins: Translation vector `T = [X, Y]`.
2. Root/Tip/Lag Pins: FK hierarchy. Tip calculates offset via delayed velocity vector `V_lag = (vector P_current - vector P_prev) * 0.20`. Clamped by magnitude `sqrt(X^2 + Y^2) <= MaxLag`.
3. Chain Pins: Iterative dampening loop. `vector V_pin(i) = vector V_pin(i-1) * DecayRatio` (`DecayRatio ≈ 0.70`).

Both defaults (`0.20` lag fraction, `0.70` decay) sit within the 15-25% / 65-75% ranges that keep a whip-turn from overshooting past the seam margin (`II.4`) — no change needed here.

### II.4 Programmatic Seam Prevention

To automate the 8-12% extension margin, calculate the maximum possible translation derivative `D_max` for the layer, scale by the `F_prox` factor (`III.5`), and expand the SVG fill geometry outward by `D_max` pixels at compile time.

**Addition — floor value.** Percentage-based scaling breaks down at extreme close-up the same way stroke-width scaling does (`III.5`): below a small margin, use `max(percentage_margin, floor_margin)`, where `floor_margin` is a fixed absolute value sized to the layer's largest `C_peak` at `F_max`. This guarantees close-up-heavy shots on high-`C_peak` layers (projections, `C = 1.50`) don't silently under-cover a seam just because the percentage math happened to round small.

---

## PART III — Parallax Displacement & Spatial Mathematics

### III.1 Displacement Hierarchy Coefficients

Define a scalar constant `C_peak` for each layer type: High-Proj (`C = 1.50`), Nose (`C = 1.00`), Primary (`C = 0.60`), Face (`C = 0.00`), Base-Anchored Projections (`C = -0.50`), Back Hair (`C = -1.00`).

### III.2 Easing Logic & Derivative Math

The translation offset function is the exact integral of spherical surface velocity. For a zone spanning `[theta_a, theta_b]`:

```
T(theta) = C_peak * [sin(theta) - sin(theta_a)]
```

Because `T'(theta) = C_peak * cos(theta)`, velocity continuity at the boundary is mathematically guaranteed *provided* `theta_a` is the angle the zone actually started at — see `III.6`'s fix below, since with hysteresis in play that's not always the nominal threshold constant.

### III.3 Mirroring Transformation Matrix

For negative yaw (`theta < 0°`), apply reflection matrix `M_mirror` to the output vector: `M_mirror = [-1, 0; 0, 1]`. Applies identically regardless of whether the anchor is in the `R_cranium` or `R_jaw` domain — mirroring is a yaw-axis (`theta0 -> -theta0`) operation, orthogonal to which radius an anchor's elevation uses.

### III.4 3D-to-2D Spherical Projection Formula & Matrix Multiplication

For a given feature with local origin `(theta_0, phi_0)` and rig state `(theta, phi)`, using whichever `R` the feature's domain requires (`I.6`):

```
Theta = theta_0 + theta
Phi   = phi_0 + phi

delta_x = R * cos(Phi) * sin(Theta)
delta_y = R * sin(Phi)
Z_sort  = R * cos(Phi) * cos(Theta)
```

**The Pole Limit:** as pitch approaches ±90°, `cos(Phi)` approaches 0, collapsing all horizontal yaw parallax to exactly 0 for `R_cranium`-domain anchors. The identical collapse applies to `R_jaw`-domain anchors as pitch drives their own `Phi` toward ±90° — relevant near the Bottom View, where the chin (`phi_0 = -90°` already) is at that pole regardless of live pitch, and the nose/mouth approach it. This is the formal justification for discrete, hand-authored Top and Bottom presets: 2D translation vectors physically cannot simulate rotation at either sphere's pole, cranium or jaw.

### III.5 Camera Perspective Frustum & Proximity Factor (Z_cam)

```
F_prox = max(F_min, min(F_max, K / max(Z_cam, Z_min)))
S = F_prox * [delta_x, delta_y]
```

Crucial Constraint: the matrix scaling operator on the graphic object must remain `[1.0, 1.0]`. `F_prox` also scales the seam-extension floor margin (`II.4`) — the same proximity math that widens the parallax swing has to widen the margin covering it, or a close shot on a high-`C_peak` layer opens a gap the wide-shot margin never had to cover.

### III.6 Parallax-to-Swap Registration (Zero-Jump Logic)

- Pivot Anchor Uniformity: baseline origin vector `P_base` shared identically in both Asset A's and Asset B's data structures.

- **Local Delta Reset — fixed for hysteresis.** At the nominal threshold `theta_t` (e.g. `45.1°`), the naive formula rebases as `T_incoming(theta) = C_peak * [sin(theta) - sin(theta_t)]`. But the Schmitt trigger (`IV.0`) doesn't actually fire at `theta_t` — it fires at `theta_t + H` rising or `theta_t - H` falling. Rebasing against the nominal constant leaves a small nonzero residual exactly at the real firing moment: `T_incoming(theta_fired) = C_peak * [sin(theta_t + H) - sin(theta_t)] ≠ 0`. Fix: capture the actual firing angle as a runtime variable inside the trigger event, and rebase against *that*:

  ```
  T_incoming(theta) = C_peak * [sin(theta) - sin(theta_fired)]
  ```

  where `theta_fired` is set the instant the Schmitt trigger flips (see `IV.0`'s corrected pseudocode). This guarantees `T_incoming(theta_fired) = 0` exactly, regardless of direction.

- **Floating-Point Preservation — hardened.** Compute coordinates in `float64`. Rounding must account for device pixel ratio, not just call `Math.round()` on the logical coordinate — on a HiDPI/retina surface, naive rounding snaps to the wrong grid and reintroduces the sub-pixel shiver this rule exists to prevent:

  ```
  pixelSnap(x) = Math.round(x * devicePixelRatio) / devicePixelRatio
  ```

  Apply only at the final render rasterization step, identically for the outgoing and incoming asset during a crossfade.

---

## PART IV — Yaw Rotation State Machine

### IV.0 Directional Schmitt Trigger (Hysteresis Math) — Fixed to Capture Firing Angle

```text
// state: CurrentAsset, theta_fired (shared with III.6's rebase formula)
if (CurrentAsset == A && theta > (V + H)) {
  CurrentAsset = B;
  theta_fired = theta;                 // <-- capture, not the nominal V
  StartCrossfade(A, B, duration_in_degrees);
} else if (CurrentAsset == B && theta < (V - H)) {
  CurrentAsset = A;
  theta_fired = theta;
  StartCrossfade(B, A, duration_in_degrees);
}
```

`theta_fired` is read directly by `III.6`'s `T_incoming` formula for whichever zone just became active. `duration_in_degrees` (not frame count) keeps the crossfade width consistent regardless of interaction speed.

### Zone 1: Front to 3/4 Transition (0° to 45°)

- Zone 1a (0° to 22.5°): `T(theta) = C * [sin(theta) - sin(0°)]`.
- Zone 1b sub-threshold (22.5°): triggers `Eye_Far_Narrow` swap via Schmitt logic; sets `theta_fired`.
- Zone 1b (22.5° to 45°): `T(theta) = C * [sin(theta) - sin(theta_fired)]` — using the captured firing angle (`≈24.0°` rising, given `H = 1.5°`), not the nominal `22.5°`.

### Zone 2: The 3/4 Hard Swap (45.1°)

Fires main asset cohort replacement. Resets base anchors to the new 45°-hand-authored geometry; `theta_fired ≈ 46.6°` rising.

### Zone 3: 3/4 to Profile Transition (45.1° to 90°)

- Zone 3a: `T(theta) = C * [sin(theta) - sin(theta_fired)]`, using Zone 2's captured firing angle.
- Zone 3b sub-threshold (67.5°): triggers `Eye_Far_Sliver`; sets its own `theta_fired`.

### Zone 4: The Profile Swap (90.1°)

Silhouette collapses to the 1D limit. Far-side `Z_sort` components evaluate negative, triggering visibility `0.0` (the same general test from `III.4`, not a special case).

### Zone 5: The Back Turns (90.1° to 180°)

135° triggers back-fuzz planes. 180° triggers the featureless cranium sphere.

---

## PART V — Pitch (Vertical) Rotation (0° to +/- 90°)

### V.1 Looking Down — Parallax Phase (0° to 45°)

Reuses `C_peak` array. Applies to `delta_y = R * sin(Phi)`. No state swap in Zone P1 (0° to 20°).

**Relocated and recomputed — Zone P2 (20.1° to 45°):** eye vertical deformation belongs here, not in the post-swap Top View section. Using the eye's own `phi0_eye ≈ -14.5°` (`I.6`), at `phi = 45°` (just before the hard swap): `Phi = -14.5° + 45° = 30.5°`, so:

```
y_eye(phi=45°) = R_cranium * sin(30.5°) ≈ 0.51R
```

— a real computed value, replacing the previously unsourced `Y = -0.1R`. Eye vertical bounding-box compression toward the 70% figure is a deformation over this same span, ending at the hard swap.

### V.2 Top View — Single Hard Swap (45.1° to 90°)

Discrete swap to `FaceBase_Top`. Face feature cohort visibility drops to `0.0` in the same keyframe as the swap (shares `state.pitch_zone_45`, per `II.2`'s fix). Because horizontal parallax `delta_x` geometrically collapses under the `cos(90°) = 0` multiplier (`III.4`'s Pole Limit), the rig switches from spherical parallax to purely planar 2D rotation for any residual crown motion (e.g. a hair whorl) — no eye/nose/mouth content applies here at all; see `V.1` for where that content actually lives.

### V.3 Looking Up — Parallax Phase (0° to -45°)

Zone P1' (0° to -20°): parallax up, using each anchor's own `phi0` per the corrected domain model (`I.6`) — note the nose and mouth are now in `R_jaw` territory here, so their motion uses `R_jaw`, not `R_cranium`.

Zone P2' (-20.1° to -45°): algorithmic preview cross-fade of alpha channels against the Under-Plane asset.

### V.4 Bottom View — Hard Swap & Parallax Range (-45.1° to -90°)

Discrete swap to `FaceBase_Bottom`. Jaw geometry mathematically occludes the front base. Vector displacement extrapolation continues via `sin(Phi)` (using `R_jaw` for the chin/nose/mouth complex, `R_cranium` for anything above the equator still in frame), moving the chin beyond the nose anchor limit. Neck insertion point corrected to `(0, -1.6R)` — see `I.5`.

---

## PART VI — Bilinear Blend Space & Residual Correction

For unauthored diagonal poses (e.g., 30° yaw, 25° pitch): do not generate new SVGs. Compute via Spherical Projection (`III.4`), using each anchor's own domain (`R_cranium` or `R_jaw`).

Residual Correction: hand-authored corner anchor vector `P_art` can deviate from the pure mathematical vector `P_math`. Apply `E = P_art - P_math`. **Confirmed correct as stated** — verify: at the corner itself, the corrected live position must equal `P_art` exactly, i.e. `P_math_corner + E = P_art`, which only holds if `E = P_art - P_math_corner`. (Flag: the previous prose edition of this manual stated the subtraction the other way around, which does not satisfy this identity — this edition's convention is the one to keep.)

**Addition — explicit bilinear formula.** For a live pose `(theta, phi)` inside a grid cell bounded by yaw corners `[theta_lo, theta_hi]` and pitch corners `[phi_lo, phi_hi]`, with residual vectors `E_00, E_10, E_01, E_11` at the four surrounding corners:

```
u = (theta - theta_lo) / (theta_hi - theta_lo)
v = (phi - phi_lo) / (phi_hi - phi_lo)

E(theta, phi) = (1-u)(1-v)*E_00 + u(1-v)*E_10 + (1-u)v*E_01 + uv*E_11
```

Add `E(theta, phi)` to the live formula-computed position.

**Edge case:** this requires four real corner assets per cell. The pitch band from `45°` to `90°` (Top View) has no yaw-corner grid at all — Top is a single asset regardless of yaw (`V.2`) — so bilinear interpolation does not apply inside that wedge; residual correction there is a single fixed offset at the `+90°` asset itself, not an interpolated one.

---

## PART VII — Non-Destructive Masking & Expression States

### VII.1 Dynamic Alpha Mattes

Eyes use `clipPath` definitions referencing the white sclera vector shape to truncate overlapping hair paths mathematically, avoiding destructive bit-erasure.

### VII.2 Programmatic Lip Sync & Blink Logic

Viseme array `[Closed, A, I, U, Neutral]`. Each shape needs its own asset at every yaw hard-swap zone (front, 3/4, profile) — mouth shape is a function of both phoneme and head angle. Controlled by a distinct, decoupled state variable `PhonemeTimer`. Crossfade is evaluated via temporal framerate (`delta t`), explicitly ignoring the rotational hysteresis logic — visemes have no equivalent of "hovering near a threshold," since they're triggered by phoneme changes, not the rotation parameter. Teeth are drawn only for the `A` and `I` visemes (a simple two-line upper/lower ridge at the locked stroke width `W`); `Closed`, `U`, and `Neutral` show none — this is the only teeth-construction rule in either document; the high-intensity "shark teeth" manpu (XVII.2) is a separate, stylized asset that doesn't share this geometry.

### VII.3 Blink State Set

```text
BLINK_STATES = [OPEN, HALF, CLOSED]   // pre-draw at minimum, at the same yaw zones as VII.2
```

All three states share the primary eye asset's construction (`I.6`) — only the lid position changes. Like visemes, blinks cross-fade on their own short fixed timer, not the rotation-driven Schmitt/crossfade contract — a blink is timed by performance, not by where the head happens to be pointed.

### VII.4 Eyebrow Expression Set (Optional Extension)

```text
BROW_STATES = [NEUTRAL, RAISED, FURROWED]   // at minimum: front, 3/4, profile zones
```

Independent of the emotion-state machine (Part XVII) — a lightweight brow-only modifier layer for reactions that don't warrant a full emotion swap.

---

## PART VIII — Asset Naming & Production Pipeline

Tokenize structurally: `$Feature_$State_Y$YawZone_P$PitchZone.ext`

**Addition — missing sub-threshold tokens.** `IV` defines Zone 1b and Zone 3b as real swap events (`Eye_Far_Narrow`, `Eye_Far_Sliver`), but this section had no `YawZone` tokens for them:

```
YawZone tokens: Y00, Y22 (sub-threshold), Y45, Y67 (sub-threshold), Y90, Y135, Y180
```

Example: `Eye_Far_Narrow_Y22_P00.svg`.

---

## PART IX — Template & Target Art Parity System

### IX.1 The Geometry Contract

Target art vector arrays must share the identical bounding boxes, `R_cranium`, `R_jaw`, and Bezier anchor mappings as the template code — both radii, now that the domain split exists, not just one.

### IX.2 Algorithmic Validation Pass — Extended Coverage

```
Write a test script that sweeps theta and phi and asserts clean boolean
validation (bounding-box intersection, Z-depth order) at every step.
```

**Fix — insufficient coverage as originally scoped:**

- The sweep only covered `theta: 0 -> 180`. Extend to `theta: -180 -> 180` — the mirror transform (`III.3`) is a distinct code path and needs its own coverage, including verifying that assets flagged as asymmetric (`Part 0`) correctly *skip* the mirror rather than silently reusing it.
- A uniform-step sweep can step over the exact instant a threshold fires, especially with hysteresis in play. Add explicit boundary-value test points at each threshold's `V - H`, `V`, and `V + H` (all six primary thresholds, plus the two sub-thresholds) — this is what actually exercises the Schmitt trigger's dead zone and the `theta_fired` capture (`III.6`, `IV.0`), which a coarse uniform sweep would likely miss entirely.

### IX.3 Production Scale

Neither this document nor `art_guide.md` totals the actual asset count the pre-build implies. A floor estimate:

```
rotation_cells   = 16 hand-authored primary + 8 mirrored, ~8 feature layers/cell  ≈ 150-200 assets
emotion_assets   ≈ 7 emotions × feature variants × view cells touched (XVII.4)
viseme_assets    = 5 shapes × yaw zones (VII.2)
composed_assets  = emotion × viseme intersection (XIV.9) — not covered by either count above
```

`rotation_cells` alone is already several hundred assets before `emotion_assets` and `viseme_assets` are added, and `composed_assets` (a talking, emoting face at once) isn't accounted for by simply summing the other three — it's a new, separate requirement `XIV.9` introduces. Decide up front which emotion×view and emotion×viseme cells are in scope for a given character tier, and rely explicitly on the stated fallback behavior (template art for unauthored rotation cells, `IX.2`; neutral for unauthored emotion cells, `XVII`) for everything else, rather than treating this as a fixed, small checklist.

---

## PART X — Cutout Rigging Principles (Technical Specification)

This rig belongs to the cutout-animation family: flat articulated art pieces, jointed at pins, moved by transforms on the pins rather than by mesh deformation. The principles below are the load-bearing rules the no-deformation contract depends on, stated as an auditable technical specification. Every rule maps onto a section of Parts I–IX.

### X.1 The Multiplane Parallax Principle

Depth comes from **multiple independently-translatable layers separated by an optical gap**. Two rules govern the parallax that gap produces:

1. **The further from the camera, the slower the slide.** The entire justification for the signed `C_peak` table in III.1 — Nose/Bangs slide fastest (+1.00), Face Base anchors (0.00), Back Hair slides opposite (−1.00). A depth-ordered monotonic slide-rate table *is* the multiplane rule.
2. **Foreground and background sliding in opposite directions produces the read of rotation.** Near-side features slide one way; far-side pair members and Back Hair slide the other during a yaw turn — opposing slides are the rotational cue.

The gap between layers is what gives each layer its own focus falloff and makes the depth read volumetrically even at rest. This is the principle behind the seam margin (II.4) and, when the atmospheric veil of Part XV is enabled, behind the visible depth haze between planes.

### X.2 The Art-Swap Principle (Angular Discretization)

A flat 2D image cannot be rotated in 3D without foreshortening incorrectly, so the turn is faked by **discrete per-view art swaps**: the same character is represented by different hand-authored art depending on its rotation relative to the viewer, with an **angular crossfade at cell boundaries** to prevent popping. That crossfade is the Parameter-Space Crossfade of III.6/XIV.4.

The view sphere is partitioned into a discrete set of angular cells (Part XI); each cell owns one authored asset, selected by nearest angular cell. The art is *authored once per angle*, never deformed per-frame. This is the entire justification for the Full-Matrix Pre-Build of Part 0 — the rig is a pre-built asset selector, not a generative one.

### X.3 The Swap-Set & Crossfade Principles

```text
// Three rules govern how discrete art swaps are softened into seamless transitions.

FADE_THEN_HIDE:
    // A layer leaving the cohort fades opacity to 0 BEFORE the boundary,
    // never holds at alpha 0 (wastes fill-rate, risks double-render).
    // A layer with no incoming art hides at transition start.          [IV.0, V]

SWAP_DIVORCED_FROM_ALPHA:
    // The visible part is computed from the discrete state ALONE;
    // opacity is a separate continuous channel.
    // If both drive rendering, two poses render through the boundary
    // and produce a double-image.                                     [IV.0]

CROSSFADE_IN_PARAM_SPACE:
    // The blend window is a function of the rotation parameter (±0.75°),
    // NOT frame count. A frame-count window is silently speed-dependent:
    // same N frames for fast and slow => fast snaps, slow lingers.     [III.6, XIV.4]
```

### X.4 The Articulated-Cutout Principle

Every body part is a **fixed outline shape**; only its position, rotation, scale, opacity, and Z-order change between frames. There is **no in-plane squash, stretch, or vertex deformation** — the medium of cutout animation does not allow it without re-cutting the piece. This is the physical enforcement of the Zero-Morphing Guarantee (III.4). The silhouette line is the source of truth; the fills are secondary. The source art is authored once, against fixed construction coordinates, and the rig never redraws it.

### X.5 The Atmospheric-Veil Principle

A separately-animated translucent overlay, whose opacity ramps with depth, carries the atmospheric read that parallax alone cannot. The haze is a **translucent plane whose distance from the artwork controls the diffusion falloff** — a receding feature dissolves through the veil rather than cutting out. This principle is formalized in Part XV: it generalizes the fade-then-hide rule (X.3) from swap boundaries to *any* receding depth, and makes the "air between planes" read volumetrically rather than as a flat collage.

### X.6 The Hand-Moved Jitter Principle

Perfectly deterministic motion can read as mechanical; a tolerated sub-pixel position noise reads as "breathing" and is, in some aesthetics, essential to the medium. The principle is offered as an **optional** overlay: a controllable noise term on the per-frame parallax offset, off by default, available as an aesthetic choice. It applies to the pin's translation only, never to the source-art vertices. See XV.6.

### X.7 The 18-Commandment Audit Checklist

Every commandment is enforced somewhere in Parts I–IX; this is the consolidated checklist an automated validator runs against the build.

```text
 1. ART_IS_IMMUTABLE         — no source-art vertex moves at runtime.
                                Legal per-frame ops: translate, rotate,
                                scale, opacity, Z-order of WHOLE pieces.  [III.4]
 2. ROTATION_IS_NODE         — rigid turns use a rotation transform, never
                                a mesh warp.                              [III.4]
 3. TURN_IS_SWAP             — between pose keys, swap the visible
                                attachment; never vertex-morph A → B.    [IV.0]
 4. DEPTH_IS_PARALLAX_SLIDE  — Z diffs = different translate magnitudes
                                per layer. Closest slides most; backdrop
                                never slides.                            [III.1, X.1]
 5. PEAKS_ARE_SIGNED         — fg/bg slide in OPPOSITE directions to
                                fake rotation (multiplane rule).         [III.1, X.1]
 6. VELOCITY_MONOTONIC_IN_Z  — per-tag rate stays strictly ordered by
                                depth; never invert two layers' rates.   [III.1]
 7. POSE_TO_POSE             — author extremes at canonical angles; the
                                in-between is parameter-space blend, not
                                vertex interpolation.                    [IV, VI]
 8. CROSSFADE_IN_PARAM_SPACE — blend window = function of rotation param
                                (±0.75°), NOT frame count. Speed-
                                independent.                             [III.6, XIV.4]
 9. FADE_THEN_HIDE           — layer leaving art fades BEFORE boundary;
                                never alpha-zero-hold.                   [IV.0]
10. SWAP_DIVORCED_FROM_ALPHA — discrete state picks visible part;
                                opacity is separate channel. Two parts
                                never render through boundary.           [IV.0]
11. SCHMITT_HYSTERESIS       — every boundary has dual thresholds (±1.5°)
                                with a dead zone; no chatter.            [IV.0, XIV.3]
12. EASE_WITH_SMOOTHSTEP     — linear interp produces "shrunken shape".
                                Use S₁ = 3t²−2t³ min; S₂ for C².         [III.2, XIV.2]
13. ANIMATE_Z_ORDER          — a layer going to back must demote in sort;
                                don't rely on occlusion alone.           [II.2]
14. BREAK_MIRROR_PER_POSE    — 3/4 card ≠ mirror of front. Insert one
                                controlled asymmetry (ahoge, iris arc,
                                off-center mouth).                       [I.6, I.7, XIII.4]
15. MINIMAL_VERTEX_COUNT     — densify only where deformation happens;
                                here it doesn't, so stay sparse.         [III.4]
16. RESPECT_SOLID_DRAWING    — pose selection preserves volume: profile
                                stays as wide-at-eye-line as front.      [XIII.3]
17. ONE_FOCAL_POINT          — during swoosh, damp secondary layers so
                                the swap reads; re-enable after.
                                                                         [staging principle]
18. ARCS_FROM_COMPOSITING    — pure yaw slide is intentionally straight
                                ("mechanical"); arc = composite yaw+pitch,
                                never arc one axis.                      [arcs principle]
```

**Swoosh vs. ordinary crossfade — the missing test.** Commandment 17 assumes it's already decided that a given threshold uses Swoosh; nothing in either document says how that decision gets made, beyond the one worked example (180° front↔back). A plain crossfade only reads cleanly when the outgoing and incoming silhouettes are close enough that their outlines nearly coincide during the blend — Pivot Anchor Uniformity guarantees the anchor point matches, not that the outlines do, and on monoline art (no soft edges to hide a seam) two non-coincident line paths fading through each other read as a double-image, not a morph. Measure the non-overlapping outline area between the two assets within one seam-extension margin (II.4); above a set tolerance, use Swoosh instead of the standard crossfade. Don't assume 45.1° (front→3/4) or 90.1° (3/4→profile) pass this test by default — both introduce new silhouette geometry the outgoing asset didn't have (an eye-socket contour, merged nose/mouth/projection edges) and either can fail it the same way 180° does.

### X.8 Programmatic Audit Pseudocode

```text
function AuditRig(rig):
    for asset in rig.assets:
        assert asset.stroke_width_uniform == true                      // 1
        assert asset.has_mesh_warp == false                            // 2
        assert asset.vertex_count <= MAX_VERTICES                      // 15
    for boundary in rig.thresholds:                                     // rotation-driven boundaries only —
                                                                          // viseme/blink swaps (VII.2/VII.3) are
                                                                          // exempt by design and must not be in
                                                                          // this set, or this loop contradicts VII.2
        assert boundary.has_schmitt == true                            // 11
        assert boundary.crossfade.is_parameter_space == true           // 8
        assert boundary.fade_then_hide == true                         // 9
        assert boundary.swap_disjoint_from_alpha == true               // 10
    for layer in rig.layers:
        assert layer.depth_order_is_animated == true                   // 13
        assert layer.peak_signed_per_depth_order(layer.peak) == true   // 5, 6
        assert layer.ease_uses_smoothstep(layer.ease_curve) == true    // 12
    for cell in rig.view_matrix:                                       // Part XI
        assert cell.cohort_resolved_discretely == true                 // 3, 7
        assert cell.solid_drawing_volume_preserved == true             // 16
        assert cell.one_focal_point_during_swoosh == true              // 17
        assert cell.arcs_composite_axes == true                        // 18
        assert cell.has_deliberate_asymmetry == true                   // 14
```

---

## PART XI — Enumerated View Matrix

The literal production matrix Parts IV–VI reference piecemeal. A rig with any unauthored cell has a hole in its 360° coverage.

### XI.1 Data Structure

```text
struct ViewCell {
    yaw_zone:    YawZoneId;       // Z0..Z7 (camera-orbit order, see XI.2)
    pitch_band:  PitchBandId;     // P_MINUS | P0 | P_PLUS
    is_mirror:   bool;            // true for Z1, Z3, Z5 (mirrored from partner)
    cohort:      AssetCohort;     // the active feature set (Part VIII naming)
    visibility:  map<FeatureId, float>;   // per-feature 0..1 (XII.4 contract)
    depth_order: array<LayerId>;          // Z-stack permutation for this cell (II.2)
    authority:   Authority;       // PARALLAX | SWAP_AT_BOUNDARY
    sub_threshold_assets: array<FeatureId>;  // features that swap INSIDE this cell
}
```

### XI.2 Yaw Zone Enumeration (camera-orbit order)

```text
enum YawZoneId {
    Z0_FRONT    = 0,  // yaw center   0°, range [-22.5, +22.5]
    Z1_3Q_LEFT  = 1,  // yaw center -45°, range [-67.5, -22.5]   (MIRROR of Z2)
    Z2_3Q_RIGHT = 2,  // yaw center +45°, range [+22.5, +67.5]
    Z3_PROF_LEFT= 3,  // yaw center -90°, range [-112.5, -67.5]  (MIRROR of Z4)
    Z4_PROF_RIGHT=4,  // yaw center +90°, range [+67.5, +112.5]
    Z5_B3Q_LEFT = 5,  // yaw center-135°, range [-157.5,-112.5]  (MIRROR of Z6)
    Z6_B3Q_RIGHT= 6,  // yaw center+135°, range [+112.5,+157.5]
    Z7_BACK     = 7,  // yaw center±180°, range [+157.5, -157.5] (WRAPS)
}

camera_orbit_order = [Z1, Z3_LEFT_IMPLIED, Z0, Z2, Z4, Z6, Z7, Z5_WRAP]
// The widget reads the strip LEFT → 3/4L → FRONT → 3/4R → RIGHT → BACK_R → BACK → BACK_L
// with the right edge wrapping back to the left. (BkL is the LAST segment.)
```

### XI.3 Boundary Set (Schmitt ±1.5° each)

```text
THRESHOLDS_YAW = [
    { value: ±22.5,  type: SUB,        cohort_swap: [Eye_Far -> Eye_Far_Narrow] },
    { value: ±45.1,  type: PRIMARY,    cohort_swap: FULL_3Q_COHORT },
    { value: ±67.5,  type: SUB,        cohort_swap: [Eye_Far -> Eye_Far_Sliver,
                                                     Eye_Near -> Eye_Near_3Q] },
    { value: ±90.1,  type: PRIMARY,    cohort_swap: PROFILE_COHORT },
    { value: ±135,   type: PRIMARY,    cohort_swap: BACK_3Q_COHORT (features -> 0%) },
    { value: ±180,   type: PRIMARY,    cohort_swap: BACK_COHORT (FaceBase -> featureless) },
]
THRESHOLDS_PITCH = [
    { value: +45.1,  type: PRIMARY,    cohort_swap: TOP, features_visibility: 0.0 },
    { value: -45.1,  type: PRIMARY,    cohort_swap: UNDER_PLANE },
]
HYSTERESIS_HALF_DEG = 1.5
```

### XI.4 Pitch Band Enumeration

```text
enum PitchBandId {
    P_MINUS = -1,  // pitch center -90°, range [-90, -45.1]; UnderPlane asset;
                   //   PARALLAX carries the rest to -90° (NO second asset at -90°)
    P0      =  0,  // pitch center   0°, range [-45, +45]; parallax only
    P_PLUS  = +1,  // pitch center +90°, range [+45.1, +90]; FaceBase_Top asset;
                   //   features at 0%; no further tiers to +90°
}

// Asymmetry: Top = swap-and-stop; Bottom = swap-and-continue-parallaxing.
// There is intentionally NO Pn90 token (Part VIII) — the under-plane asset
// stretches to nadir, it does not get replaced.
```

### XI.5 Complete Corner Grid (the production checklist)

```text
// 8 yaw × 3 pitch = 24 primary cells; Z1/Z3/Z5 are mirrors ⇒ 16 authored + 8 mirrored.
// 2 sub-thresholds (Y22, Y67) add rows for the features they touch.

primary_grid[Z][P]:
            P_MINUS         P0              P_PLUS
Z0_FRONT   [parallax]      [FRONT]         [parallax]
Z1_3Q_L    mirror          mirror          mirror          // of Z2
Z2_3Q_R    [3Q @ P-]       [3Q]            [3Q @ P+]       + subs Y22, Y67
Z3_PROF_L  mirror          mirror          mirror          // of Z4
Z4_PROF_R  [PROFILE @ P-]  [PROFILE]       [PROFILE @ P+]
Z5_B3Q_L   mirror          mirror          mirror          // of Z6
Z6_B3Q_R   [BACK3Q @ P-]   [BACK3Q]        [BACK3Q @ P+]
Z7_BACK    [BACK @ P-]     [BACK]          [BACK @ P+]

sub_threshold_rows:
    Y22 (in Z0/Z1 boundary, ±22.5°):  Eye_Far, Eye_Near, Projections  @ each P band
    Y67 (in Z2/Z3 boundary, ±67.5°):  Eye_Far, Eye_Near                @ each P band

// Count: 24 primary cells - 8 mirrored = 16 authored.
// Sub-thresholds add up to 6 sub-cells per affected feature (2 yaw × 3 pitch).
// Total authored assets per feature ≈ 16 primary + up to 6 sub-threshold = ~22,
// before expression variants (visemes, blinks, brows — Part VII).
```

### XI.6 Per-Cell Contract (four simultaneous contracts)

Every authored `ViewCell` carries:

```text
1. ASSET COHORT        — set of feature files active in the cell (Part VIII naming).
2. VISIBILITY          — map<FeatureId, 0..1>. Anchor-critical always 1.0 (XII.4);
                         bridge-safe 0.0 in walk-behind (Z5/Z6/Z7) or P_PLUS Top.
3. DEPTH ORDER         — Z-stack permutation. Most-notably:
                           BackHair -> position 1 at Z7;
                           FaceBase -> back at Z7;
                           PrimaryFeatures -> dropped at P_PLUS.
4. AUTHORITY           — PARALLAX (continuous slide within cell) OR
                         SWAP_AT_BOUNDARY (discrete cohort replacement at edge).
                         Boundary itself always uses Parameter-Space Crossfade (III.6, XIV.4).
```

### XI.7 Mirror Shortcut & Three Exception Classes

```text
MIRROR_TRANSFORM(asset, theta_0):
    // negate authored azimuth, flip X-axis signs (III.3)
    mirrored.theta_0 = -asset.theta_0
    mirrored.delta_x = -asset.delta_x
    mirrored.delta_y = asset.delta_y            // pitch terms untouched
    mirrored.phi_0   = asset.phi_0
    return mirrored

// Three classes CANNOT ride the mirror — must be re-authored separately:
EXCEPTIONS = [
    ASYMMETRIC_DESIGN_FLAG,   // Part 0: ahoge, single earring, off-center part.
                              //   Their theta_0 was never symmetric.
    HAND_DRAWN_BACK_FUZZ,     // Silhouettes whose back-fuzz art has a deliberate
                              //   non-geometric curl the mirror would overwrite.
    RESIDUAL_CORRECTION,      // Bridge-safe features with E = P_art - P_math (VI).
                              //   Mirror the geometry, RECOMPUTE the correction
                              //   against the mirrored anchor — don't copy.
]
```

---

## PART XII — Cross-View Consistency Validation

The algorithmic checks an automated validator runs against every authored cell. A cell that fails any check is not sign-off-able, even if its art looks correct in a static pose.

### XII.1 The Invariant: Reference Cross Continuity

```text
function ValidateReferenceCross(cell, neighboring_cells):
    // The centerline + browline of THIS cell, bowed per III.4, must be the
    // continuous extension of its neighbors' crosses. Drift breaks the read
    // at the boundary no matter how clean the cohort swap is.
    for neighbor in neighboring_cells:
        cross_at_boundary_self    = SampleCross(cell,     cell.boundary_with(neighbor))
        cross_at_boundary_neighbor= SampleCross(neighbor, neighbor.boundary_with(cell))
        assert distance(cross_at_boundary_self,
                        cross_at_boundary_neighbor) < EPSILON_CROSS   // typically 0.5 px
```

### XII.2 The Five Anchor Registrations

```text
ANCHOR_REGISTRATIONS = [
    { name: "pupil_centers",  domain: R_cranium, zones_visible: [Z0,Z1,Z2,Z3,Z4],
      rule: "x-positions trace the browline arc; inter-ocular gap = gap_0 * cos(Theta)" },
    { name: "nose_tip",       domain: R_jaw,     zones_visible: [Z0,Z1,Z2,Z3,Z4],
      rule: "stays on the bowed centerline; never drifts off-center" },
    { name: "mouth_center",   domain: R_jaw,     zones_visible: [Z0,Z1,Z2,Z3,Z4],
      rule: "same centerline rule; dead-center gap stays dead-center" },
    { name: "chin_apex",      domain: R_jaw,     zones_visible: [Z0,Z1,Z2,Z3,Z4,Z6],
      rule: "lowest V point; never off-centerline" },
    { name: "ear_tops",       domain: R_cranium, zones_visible: [Z0,Z1,Z2,Z3,Z4,Z5,Z6],
      rule: "span eye-top to nose-bottom; rotate to back-fuzz past profile" },
]

function ValidateAnchors(cell):
    for anchor in ANCHOR_REGISTRATIONS:
        if cell.yaw_zone in anchor.zones_visible:
            predicted = SphericalProject(anchor.theta_0, anchor.phi_0, cell.yaw, cell.pitch)
            actual    = cell.authored_anchor(anchor.name)
            residual  = actual - predicted
            assert magnitude(residual) < cell.residual_correction_tolerance(anchor.name)
```

### XII.3 Foreshortening Math (the math of the turn)

```text
// When the head rotates yaw theta, every front-facing feature's projected
// width scales as cos(Theta) where Theta = theta_0 + theta (the feature's
// OWN total azimuth, III.4 — NOT the raw yaw value).

function ProjectedFeatureWidth(feature, theta):
    Theta = feature.theta_0 + theta
    return feature.w_0 * cos(Theta)       // for cos(Theta) >= 0
                                          // for cos(Theta) < 0: feature FOLDS (hide)

// VALIDATION that authored widths match geometric prediction:
function ValidateForeshortening(cell, partner_cell_front):
    for paired_feature in [Eye_Far, Eye_Near, Brow_Far, Brow_Near, Ear_L, Ear_R]:
        w_math = ProjectedFeatureWidth(paired_feature, cell.yaw_center)
        w_art  = cell.authored_width(paired_feature)
        assert abs(w_math - w_art) < EPSILON_WIDTH   // typically 5% of w_0

// KEY: the foreshortening is BAKED INTO the asset (the Eye_Far_Narrow,
// Eye_Far_Sliver, Eye_Profile cards of Part IV are pre-foreshortened),
// never computed per-frame on a single sprite. The formula above is a
// VALIDATION, not a runtime deformation.
```

### XII.4 Anchor-Critical vs. Bridge-Safe (the read contract)

```text
ANCHOR_CRITICAL = { Head, Bangs, Hair_FrontMass, BackHair, Ears }
BRIDGE_SAFE     = { Eye_L, Eye_R, Brow_L, Brow_R, Mouth, Nose, Teeth, Cheek_L, Cheek_R }

WALK_BEHIND_ZONES = { Z5_B3Q_LEFT, Z6_B3Q_RIGHT, Z7_BACK }  // |yaw| >= 135°
P_PLUS_TOP        = { pitch_band == P_PLUS }

function ValidateReadContract(cell):
    // Anchor-critical parts NEVER fully hide — swap that hides one is a DEFECT.
    for part in ANCHOR_CRITICAL:
        assert cell.visibility[part] > 0.0
        // (Ears may fold to back-fuzz past profile, but visibility > 0)

    // Bridge-safe parts MAY hide in walk-behind or P_PLUS Top — correct & expected.
    for part in BRIDGE_SAFE:
        if cell.yaw_zone in WALK_BEHIND_ZONES  or  cell in P_PLUS_TOP:
            assert cell.visibility[part] == 0.0     // MUST hide, not just "may"
        else:
            assert cell.visibility[part] > 0.0
```

### XII.5 Mirror-vs-Reauthor Decision Tree

```text
function ClassifyElementForMirror(element, mirrored_cell):
    if element.geometrically_symmetric_about_centerline:           // cranium, jaw, neck
        return MIRROR                                              // no action
    if element.is_paired_feature and element.has_partner:          // Eye_L <-> Eye_R
        return RESOLVE_TO_PARTNER_MIRRORED
        // (the P45 role split is the ONLY slot that's role-split;
        //  every other slot is slot-for-slot L == mirror(R).)
    if element in ASYMMETRIC_DESIGN_FLAGS:                         // ahoge, single earring
        return REAUTHOR_SEPARATELY
    if element.has_hand_drawn_back_fuzz_curl:                      // deliberate non-geo detail
        return REAUTHOR_SEPARATELY
    if element.is_bridge_safe and element.has_residual_correction: // VI
        return MIRROR_GEOMETRY_RECOMPUTE_CORRECTION
```

### XII.6 Per-Cell Sign-Off Audit (six-point check)

```text
function ValidateCellSignOff(cell):
    // 1. Reference Cross rebuilt for that cell's threshold (I.3) — drawn, not implied.
    assert cell.has_drawn_reference_cross == true

    // 2. Five anchor registrations (XII.2) within residual tolerance.
    ValidateAnchors(cell)

    // 3. Silhouette Read Test (I.7): one connected component, no ambiguous blob.
    mask = rasterize_flat_black(cell)
    components = connected_components(mask)
    assert count(components) == 1
    assert count(holes_larger_than(mask, NOISE_FLOOR)) == 0

    // 4. Shape-contrast ratio (XIII.3): ~4 rounded : 1 sharp.
    round_count, sharp_count = classify_path_segments(cell)
    assert round_count >= 4 * sharp_count

    // 5. Mirror-vs-reauthor (XII.5): every asymmetric element correctly categorized.
    for element in cell.asymmetric_elements:
        classification = ClassifyElementForMirror(element, cell.partner)
        assert cell.mirror_handling(element) == classification

    // 6. Depth stack (II.2): Z-order permutation matches the state flags
    //    the Schmitt trigger will set when the cell goes live.
    expected_order = compute_expected_depth_order(cell.yaw_zone, cell.pitch_band)
    assert cell.depth_order == expected_order
```

---

## PART XIII — Attractiveness Metrics

Programmable appeal checks, grounded in the art principles of `art_guide.md` Part XIII (neoteny/baby-schema, cardioidal strain, shape contrast, uncanny valley).

### XIII.1 Cardioidal Strain Ratio (the math of cute)

```text
// Negative cardioidal strain = top features expand outward+up,
// bottom features contract inward+up. This is the literal operation that
// turns a realistic head into an anime head.

CARDIOIDAL_TARGETS = {
    // (feature, classical_value, anime_value)  — values are y of face height,
    //   0.0 = top of head, 1.0 = chin (so smaller y = higher on face).
    "eye_baseline":   { classical: 0.50, anime: [0.40, 0.46] },
    "nose_baseline":  { classical: 0.70, anime: [0.62, 0.68] },
    "mouth_baseline": { classical: 0.85, anime: [0.74, 0.82] },
    "eye_width_to_face_width":  { classical: 1/5, anime: [1/4, 1/3.5] },
    "cranium_to_chin_vertical": { classical: 1.4, anime: [1.6, 2.0] },
}

function ValidateCardioidalStrain(asset):
    // The asset's measured proportions should be in the anime band,
    // NOT drifting back toward the classical canon (which loses the cute read).
    for feature, bands in CARDIOIDAL_TARGETS.items():
        measured = asset.measure(feature)
        assert bands.anime[0] <= measured <= bands.anime[1] \
            or abs(measured - bands.anime[1]) < STRAIN_TOLERANCE

// The I.4 placement values (y_eye_baseline = -0.25R, y_nose = -1.00R,
// y_mouth = -1.28R) are the cardioidal-strained targets, NOT the classical ones.
```

### XIII.2 Shape Contrast Counter (the ~4:1 rule)

```text
// Appeal principle: "variety of shape".
// Empirically: ~4 rounded forms : 1 sharp form. Too many sharp = menacing;
// too many round = blob.

function ClassifyPathSegment(segment):
    curvature_variance = compute_curvature_variance(segment)
    if curvature_variance < LOW_CURVATURE_THRESHOLD:
        join_angle = angle_between(segment.prev_tangent, segment.tangent)
        if join_angle < 150:    // sharp corner
            return SHARP
    return ROUND

function ValidateShapeContrast(asset):
    round_count = 0
    sharp_count = 0
    for segment in asset.all_path_segments:
        if ClassifyPathSegment(segment) == ROUND:
            round_count += 1
        else:
            sharp_count += 1
    assert round_count >= 4 * sharp_count       // the ~4:1 rule
    // Applied to the head: rounded = cranium, cheeks, iris, ear curves,
    //   hair-mass outer boundary, jaw curve. Sharp = chin V, nose tip,
    //   hair-tip V-terminations, brow point, ear tip.
```

### XIII.3 Baby-Schema Membership Test

```text
// Baby schema: large eyes, large cranium, small nose, small mouth,
// chubby cheeks, rounded body. fMRI (Glocker 2009) confirms baby-schema faces
// activate the nucleus accumbens.

KINDCHENSCHEMA_FEATURES = [
    { trait: "eye_area",     rule: ">= 2.5 * realistic_eye_area" },
    { trait: "cranium_size", rule: ">= 1.4 * jaw_size" },
    { trait: "nose_size",    rule: "<= 0.20 * realistic_nose_size" },
    { trait: "mouth_size",   rule: "<= 0.50 * realistic_mouth_size" },
    { trait: "cheek_chub",   rule: "cheek_gap >= nose_mouth_cluster_width" },
]

function ValidateBabySchema(asset):
    passed = 0
    for feature in KINDCHENSCHEMA_FEATURES:
        if asset.satisfies(feature.rule):
            passed += 1
    assert passed >= 4   // at least 4 of 5 — the schema is a cluster, not a single trait
```

### XIII.4 Eye Highlight Placement Validator

```text
// Under the monoline constraint (I.1), the eye highlight is the strongest
// appeal lever because it depends entirely on shape+placement — not on line
// weight or gradient. Universal anime/manga eye carries 1-3 solid white
// highlights on the iris.

function ValidateEyeHighlights(eye_asset):
    highlights = eye_asset.solid_fill_patches_of_tint(TINT_HIGHLIGHT_WHITE)
    assert 1 <= count(highlights) <= 3

    // Key light: largest, upper-outer quadrant of iris.
    key_light = largest(highlights)
    assert key_light.center_in_quadrant == UPPER_OUTER

    // Rim/bounce: smaller, lower-inner quadrant (optional but canonical).
    if count(highlights) >= 2:
        rim_light = second_largest(highlights)
        assert rim_light.center_in_quadrant == LOWER_INNER
        assert rim_light.area <= 0.5 * key_light.area

    // Eyelash hierarchy: shape and coverage, not weight — every stroke in this
    // rig is locked to the single width W (I.1); a differential stroke_weight
    // assertion here would itself be a monoline violation, not a check for one.
    // Upper lash is a closed wedge (dominant shape/fill area); lower lash is a
    // short open segment; iris outline is a thin closed loop with no wedge fill.
    assert eye_asset.stroke_width(UPPER_LASH) == W
    assert eye_asset.stroke_width(LOWER_LASH) == W
    assert eye_asset.stroke_width(IRIS_OUTLINE) == W
    assert eye_asset.fill_area(UPPER_LASH) > eye_asset.fill_area(LOWER_LASH)
    assert eye_asset.is_wedge_shape(UPPER_LASH) == True
    assert eye_asset.is_wedge_shape(IRIS_OUTLINE) == False
```

### XIII.5 Uncanny-Valley No-Deformation Enforcement

```text
// Uncanny valley: appearance-vs-motion mismatch is a primary uncanny trigger.
// Saygin fMRI: "if an animated character looks more human than its movement,
// this gives a negative impression." A 2D anime head MUST move like a 2D card
// (parallax + hard swap), not like a 3D deforming mesh.
//
// This is the OUTSIDE-AUTHORITY justification for Zero-Morphing (III.4):
// it is the only motion model that keeps a stylized character OUT of the valley.

function EnforceNoDeformation(rig):
    // 1. No source-art vertex is moved at runtime.
    for asset in rig.assets:
        for frame in rig.playback_frames:
            assert asset.vertex_positions(frame) == asset.vertex_positions(0)

    // 2. Legal per-frame transforms: translate, rotate, scale, opacity,
    //    Z-order of WHOLE pieces only.
    for transform in rig.transforms:
        assert transform.type in { TRANSLATE, ROTATE, SCALE, OPACITY, Z_ORDER }
        assert transform.applies_to_whole_piece == true

    // 3. The rotation multiplier applies ONLY to the (x,y) translation
    //    coordinates of the pin (III.4), never to vertex positions.
    for pin in rig.pins:
        for frame in rig.playback_frames:
            assert pin.asset.vertex_positions(frame) == pin.asset.vertex_positions(0)
            // pin.world_position(frame) MAY change — that's the parallax slide.
            // pin.asset.vertex_positions MUST NOT — that would be a morph.

    // 4. Stroke width stays fixed in screen-space pixels (III.5).
    for asset in rig.assets:
        assert asset.stroke_width_screen_space == CONSTANT
        // F_prox scales [delta_x, delta_y], NEVER the stroke.
```

### XIII.6 Deliberate Asymmetry Counter (anti-"twins")

```text
// The "twins" anti-pattern: mirrored sides read lifeless.
// Anime injects CONTROLLED asymmetry — typically 1-2 cues per face.

ASYMMETRY_CUES = [
    "ahoge_cowlick",          // single hair spike breaking centerline
    "hair_parted_to_one_side",
    "one_eyelid_heavier",
    "off_center_mouth",       // Mouth_3Q compressed-off-center shift is the in-zone version
    "brow_tilt_difference",   // a few degrees between L and R (I.6)
]

function ValidateDeliberateAsymmetry(asset):
    cue_count = count(cue for cue in ASYMMETRY_CUES if asset.has(cue))
    assert 1 <= cue_count <= 2
    // Rule: EXACTLY one or two asymmetry cues per face. Enough to read as
    // alive; not enough to read as deformed.

// Cross-zone consistency (I.7): every cell that touches an asymmetric
// element MUST preserve its asymmetry. Re-symmetrizing the ahoge on the
// 3/4 card while it stays asymmetric on the front is the classic pop defect.
function ValidateAsymmetryContinuity(rig):
    for cue in ASYMMETRY_CUES:
        for cell in rig.view_matrix:
            if any(cell.assets, has_cue(cue)):
                // the cue must be present and identically classified in every
                // adjacent cell that touches the same element.
                for neighbor in cell.neighbors:
                    if any(neighbor.assets, has_cue(cue)):
                        assert cell.asymmetry_classification(cue) == \
                               neighbor.asymmetry_classification(cue)
```

### XIII.7 Applicability to Emotion Assets

```text
// None of XIII.1-XIII.6 reference emotion — undefined whether they apply
// to Part XVII's emotion swap assets or only the Part XVI neutral build.
// They don't apply uniformly:
//
//   NON-COMEDIC emotions (SADNESS, ANGER, PRIDE, SERIOUS, and JOY/
//   RELAXATION/DEFEAT at MILD-HIGH) are still meant to read as the same
//   appealing character -> should pass XIII.2 (shape contrast), XIII.3
//   (baby-schema), XIII.4/5 (eye highlight) same as neutral.
//
//   COMEDIC BREAKS at the EXTREME intensity tier (XVII.5) — SD_MODE,
//   SPIRAL_EYES, void-white rage eyes, FLAT_EYES — deliberately violate
//   the model for graphic effect. Exempt by design, not oversight.
//
// Use the appeal_checked tag introduced in XVII.5 to record which bucket
// each authored emotion asset falls into, so this checklist isn't silently
// skipped for assets that were supposed to pass it.

function ValidateEmotionAppeal(emotion, asset):
    if asset.appeal_checked:
        ValidateShapeContrast(asset)          // XIII.2
        ValidateBabySchema(asset)             // XIII.3
        ValidateEyeHighlight(asset)           // XIII.4/5
    // else: comedic break, explicitly exempted — no assertion, not a gap.
```

---

## PART XIV — Canonical Math Reference

Code-friendly forms of every formula, with explicit **context selectors** (which zone/state/distance triggers which formula). This is the single citable reference.

### XIV.1 Spherical→Screen Projection (the master formula)

```text
// Inputs: anchor's authored angular pos (theta_0, phi_0), live (theta, phi).
// Output: 2D screen offset (delta_x, delta_y) + depth sort key (Z_sort).

Theta = theta_0 + theta                              // total azimuth
Phi   = phi_0   + phi                                // total elevation

delta_x = R * cos(Phi) * sin(Theta)
delta_y = R * sin(Phi)
Z_sort  = R * cos(Phi) * cos(Theta)

// R is the AUTHORING RADIUS:
//   R_cranium for eyes, brows, ears, upper-projections  (|y| <= R)
//   R_jaw = 1.5*R for chin, nose, mouth                  (|y| > R, tech guide I.6)
//   NEVER mix R_cranium in one axis and R_jaw in the other for the same anchor.

// ROTATION ORDER (fixed, non-commutative):
//   yaw (theta) applied first, around vertical axis;
//   pitch (phi) applied second, around the resulting horizontal axis.
//   This is the intrinsic Tait-Bryan R = R_y(yaw) * R_x(pitch).
//   Swapping the order moves a diagonal pose to a DIFFERENT sphere point.

// CONTEXT SELECTOR:
//   This is the ONLY positional-displacement authority in the rig.
//   Runs in every cell of Part XI; only (theta_0, phi_0) set and live (theta, phi) change.
//   NEVER replaced by "rotation_matrix * vertex_list" — that deforms the art (X.6 cmd 1).

// POLE SINGULARITY (gimbal lock):
//   At Phi = +/-90 deg, cos(Phi) -> 0, all horizontal yaw parallax collapses to 0.
//   This is the formal justification for the Top/Bottom hard swaps (V.2, V.4).
```

### XIV.2 Smoothstep Family

```text
// Canonical forms (Wikipedia, derived from endpoint + derivative constraints):

S1(t) = 3*t^2 - 2*t^3                                // cubic,   C1 continuous
S2(t) = 6*t^5 - 15*t^4 + 10*t^3                      // quintic, C2 continuous (Perlin)
S3(t) = -20*t^7 + 70*t^6 - 84*t^5 + 35*t^4           // septic,  C3 continuous

// General:
//   S_N(t) = t^(N+1) * sum_{k=0..N} C(N+k,k)*C(2N+1,N-k)*(-t)^k
//   d/dt S_N(t) = (2N+1) * C(2N,N) * (t - t^2)^N

// HLSL/GLSL clamping form:
function smoothstep(edge0, edge1, x):
    t = clamp((x - edge0) / (edge1 - edge0), 0, 1)
    return S1(t)

// Inverse (cubic only):
//   InvS1(x) = 0.5 - sin(asin(1 - 2*x) / 3)

// FREQUENCY DOMAIN: S1 Laplace rolls off at 60 dB/decade vs 20 for Heaviside,
// 40 for linear ramp. Higher continuity => more band-limited => fewer visible
// "pops" during the swap window (Nyquist-Shannon).

// CONTEXT SELECTORS:
//   - Crossfade alpha across a swap boundary (IV.0, III.6): use S1 minimum,
//     S2 if fast drags show harmonic shimmer.
//   - Per-zone parallax easing (III.2): NOT smoothstep — use the sine itself (XIV.5).
//     Smoothstep is for the BLEND BETWEEN states, not the SLIDE WITHIN one.
//   - Pin lag/chain decay (II.3): S1 on the lag offset's return-to-zero.
```

### XIV.3 Directional Schmitt Trigger

```text
// For a state boundary at value V with half-hysteresis H:
//
//   Switch A -> B  only when input crosses V + Sign*H  (forward direction)
//   Switch B -> A  only when input crosses V - Sign*H  (reverse direction)
//
//   where Sign = +1 for increasing parameter, -1 for decreasing.
//
// The dead zone [V-H, V+H] (width 2H) is the SINGLE MOST IMPORTANT defense
// against state chatter. Inside it, output RETAINS PREVIOUS VALUE
// (memory / bistable property — Schmitt trigger is a 1-bit quantizer with state).

// This rig's parameters:
THRESHOLDS_YAW   = [22.5, 45.1, 67.5, 90.1, 135, 180]   // and negatives
THRESHOLDS_PITCH = [45.1, -45.1]
H = 1.5  // degrees

// So the 45.1 deg yaw boundary commits at 46.6 deg rising, de-commits at 43.6 deg falling.
// 3-degree-wide band where the rig holds its current state regardless of small jitter.

// Pseudocode (captures theta_fired for XIV.5):
function SchmittUpdate(state, theta, V, H):
    if state.CurrentAsset == A and theta > (V + H):
        state.CurrentAsset = B
        state.theta_fired  = theta               // capture, NOT the nominal V
        StartCrossfade(A, B, duration_in_degrees)
    else if state.CurrentAsset == B and theta < (V - H):
        state.CurrentAsset = A
        state.theta_fired  = theta
        StartCrossfade(B, A, duration_in_degrees)
    // else: in dead zone, retain state.

// CONTEXT SELECTOR:
//   Every hard-swap boundary in Part XI uses this trigger. It sets TWO things:
//     (a) state.CurrentAsset  — read by the cohort swap (IV.0)
//     (b) state.theta_fired   — read by the Local Delta Reset (XIV.5)
//   Both MUST read from the same trigger event — never re-derive from raw angle
//   (tech guide II.2 fix).
```

### XIV.4 Parameter-Space Crossfade (speed-independent)

```text
// alpha is a pure function of the rotation parameter — no dt term.

function CrossfadeAlpha(theta, B, W):
    // B = Schmitt-adjusted boundary angle (XIV.3)
    // W = half-window width (this rig: 0.75 deg)
    t = clamp((theta - (B - W)) / (2 * W), 0, 1)
    return S1(t)                  // or linear: return t

// At theta = B, alpha = 0.5 (the equal-mix instant).

// WHY SPEED-INDEPENDENT:
//   alpha has NO dt term. The fade completes when the ANGLE has swept 2W degrees,
//   regardless of how fast the user swept it. A flick and a slow drag both fade
//   over the SAME angular sweep; retracing the path replays the exact same alpha
//   curve (no lingering residue).
//
//   Compare frame-count fade FInterpTo(alpha, 1, dt, speed): same N frames for
//   fast and slow => fast drag snaps, slow drag lingers => visibly inconsistent.

// WRAP HANDLING: across the +/-180 deg Back pair (Z6 <-> Z7), measure the signed
// sweep the SHORT WAY AROUND so alpha stays monotonic.

// CONTEXT SELECTOR:
//   Every swap boundary in Part XI, for every layer in the cohort.
//   Feeds opacity: outgoing = 1 - alpha, incoming = alpha.
//   The SWAP-SET RESOLUTION (which asset is "incoming") is computed discretely
//   by the Schmitt trigger (XIV.3) and DIVORCED from alpha — otherwise two poses
//   render simultaneously through the boundary (the "double-image" defect).
```

### XIV.5 Local Delta Reset (per-zone rebased sine)

```text
// theta_a is the ZONE-ANCHOR KEY the Schmitt trigger ACTUALLY fired at
// (state.theta_fired from XIV.3 — NOT the nominal threshold constant).

function RampOffset(theta, theta_fired, Peak):
    return Peak * (sin(theta) - sin(theta_fired))

// VELOCITY-CONTINUITY PROOF:
//   d/dtheta RampOffset = Peak * cos(theta)
//
//   At boundary theta = theta_fired:
//     outgoing velocity = Peak_out * cos(theta_fired)
//     incoming velocity = Peak_in  * cos(theta_fired)
//   They share the SAME cos(theta_fired) factor; only Peak differs. Therefore:
//     - Peak_out == Peak_in (same depth class): EXACT velocity continuity, free.
//     - Peak steps: velocity step proportional to Peak ratio; the Parameter-Space
//       Crossfade (XIV.4) ramps the ASSET across the same boundary, so Peak change
//       is interpolated — boundary velocity inherited smoothly.

// CONTEXT SELECTOR:
//   Every parallax layer, every cell of Part XI.
//   With theta = key + fraction * HalfZoneWidth (fraction -1..+1 from authored pose key),
//   this is the RampOffset(SignFrac, DepthFactor, MaxOffset, SignedKeyDeg, HalfZoneWidthDeg)
//   contract: same global sine, shifted per zone, velocity-matched at every boundary.

// 5-arg form used at ComputeOffsetForState / UpdateMaterialParameters call sites:
function ComputeOffsetForState(state, theta_signed, depth_factor, max_offset, key_deg, hzw_deg):
    fraction = SignedFraction(theta_signed, key_deg, hzw_deg)        // -1..+1
    return RampOffset(theta_signed, key_deg, depth_factor * max_offset)
    // evaluated with YawSign * GetYawKeyForState(state) signed anchor
```

### XIV.6 Cosine Foreshortening + Fold-Don't-Squash

```text
function ProjectedWidth(w_0, theta_0, theta):
    Theta = theta_0 + theta
    cos_val = cos(Theta)
    if cos_val >= 0:
        return w_0 * cos_val
    else:
        return 0   // FOLDED past the profile limb — feature HIDDEN, not squashed

// At Theta = 90 deg: feature edge-on (zero projected width).
// Past 90 deg: cos goes negative — geometrically a FOLD onto the back hemisphere.
// Real 2D art CANNOT invert through zero, so the rig HIDES the far-side member
// (visibility 0%) rather than letting it squash through.
// This is the "fold, don't squash" rule and the reason Z4/Z5/Z6 hide far-side
// pair members (XII.4).

// CONTEXT SELECTOR:
//   Foreshortening is BAKED INTO the authored asset (Eye_Far_Narrow,
//   Eye_Far_Sliver, Eye_Profile are pre-foreshortened art), NEVER computed
//   per-frame on a single sprite. The formula above is a VALIDATION (XII.3),
//   not a runtime deformation.
```

### XIV.7 Clamped Inverse Proximity

```text
function ProximityFactor(Z_cam, K, Z_min, F_min, F_max):
    return clamp(K / max(Z_cam, Z_min), F_min, F_max)

// K  = calibration constant, tuned so F_prox = 1.0 at rig's reference mid-shot.
// Z_cam = current camera distance.
// Z_min = small positive floor (NOT ZERO) — near-clip analogue.
// F_max = largest multiplier the seam margins (II.4) can cover.
// F_min = calibration floor (long-shot flattening).

// WHY CLAMPS ARE MANDATORY:
//   1/Z diverges as Z -> 0+. Unclamped, sends every displacement toward infinite
//   slide and tears every seam at once on a lens-touching close-up. Z_min floor
//   + F_max ceiling prevent that.
//   At the far end, 1/Z -> 0, distant layers don't slide — the mathematical
//   reason BackHair/skybox stays stationary ("skybox appears infinitely distant").

function ApplyProximity(delta_x, delta_y, Z_cam):
    F_prox = ProximityFactor(Z_cam, K, Z_min, F_min, F_max)
    return F_prox * [delta_x, delta_y]              // scales PARALLAX OUTPUT
                                                     // NEVER scales source art

// THRESHOLD INVARIANCE:
//   F_prox scales the MAGNITUDE of parallax output only. It NEVER changes which
//   Schmitt boundary fires (XIV.3) — thresholds are defined in rotation-parameter
//   degrees, never screen distance. A close-up does NOT "trigger an early swap"
//   no matter how maxed-out the parallax looks.

// MONOLINE PROTECTION:
//   F_prox scales [delta_x, delta_y] — the pin translation — AFTER the formula.
//   It MUST NEVER scale the source art's stroke width; stroke stays fixed in
//   screen-space pixels (III.5). Camera dolly (feeds F_prox) and canvas zoom
//   (display-level scale on final composite) are TWO DIFFERENT operations feeding
//   TWO DIFFERENT systems.

// SEAM MARGIN SCALING:
//   The 8-12% extension margin (II.4) MUST be scaled by F_prox too — the same
//   proximity math that widens the parallax swing has to widen the margin
//   covering it, or a close shot on a high-C_peak layer opens a gap.
//   floor_margin = max(percentage_margin, fixed_absolute_margin)
//   where fixed_absolute_margin is sized to the layer's largest C_peak at F_max.
```

### XIV.8 Context-Aware Composition (the full per-frame pipeline)

```text
// For any live (theta, phi) and camera Z_cam, evaluate IN THIS FIXED ORDER:
//
// NOTE: this resolves ROTATION ONLY. It does not fold in the emotion
// state machine (Part XVII) or the viseme/blink state (VII.2/VII.3) — those
// are described elsewhere as using "the same contract" but nothing here
// actually composes all three in a single frame the way a real performance
// (talking, turning, and emoting at once) requires. See XIV.9.

function EvaluateRig(state, theta, phi, Z_cam):

    // 1. Schmitt triggers (XIV.3) resolve -> current cell of Part XI matrix
    state.CurrentCell = ResolveCell(state, theta, phi)
    //   -> which asset cohort active, which features visible, depth order

    per_layer_output = []
    for layer L in state.CurrentCell.visible_layers:

        // 2a. Look up L's (theta_0, phi_0) and R from cohort data (I.5, I.6)
        anchor = L.anchor
        R = (anchor.domain == R_CRANIUM) ? R_cranium : R_jaw

        // 2b. Spherical projection (XIV.1) -> (x_L, y_L, z_L)
        delta_x, delta_y, Z_sort = SphericalProject(anchor, theta, phi, R)

        // 2c. Subtract L's front-view baseline
        delta_x -= L.baseline_delta_x
        delta_y -= L.baseline_delta_y

        // 2d. Local Delta Reset (XIV.5)
        T = RampOffset(theta, state.theta_fired_for(L), L.C_peak)

        // 2e. Camera Proximity (XIV.7)
        F_prox = ProximityFactor(Z_cam, K, Z_min, F_min, F_max)
        delta_x *= F_prox
        delta_y *= F_prox

        // 2f. Round to device pixels ONCE (Sub-Pixel Continuity, III.6)
        delta_x = pixelSnap(delta_x, devicePixelRatio)
        delta_y = pixelSnap(delta_y, devicePixelRatio)

        per_layer_output.append((L, delta_x, delta_y, Z_sort))

    // 3. For each layer crossing a boundary this frame:
    for L in state.layers_crossing_boundary:
        // 3a. Parameter-Space Crossfade alpha (XIV.4)
        alpha = CrossfadeAlpha(theta, L.boundary, L.window_half_width)
        // 3b. Smoothstep (XIV.2) is already inside CrossfadeAlpha
        L.opacity_outgoing = 1 - alpha
        L.opacity_incoming = alpha

    // 4. Composite in depth order (II.1/II.2) with opacity
    return composite(per_layer_output, state.CurrentCell.depth_order)

// The "context-aware" part: step 1's resolution changes which (theta_0, phi_0)
// set feeds step 2; step 2e's F_prox changes with camera; step 3 only fires
// near boundaries. The MATH is fixed; the CONTEXT selects the inputs.
```

### XIV.9 Cross-System Composition (Rotation × Emotion × Viseme)

```text
// EvaluateRig (XIV.8) only resolves the rotation cohort. A real performance
// needs emotion and viseme resolved against it in the same frame. Fixed
// precedence order, mirroring II.2's yaw-before-pitch rule:

function EvaluateFrame(state, theta, phi, Z_cam, emotion, emotion_intensity, viseme):

    // 0. HEAD-ORIENTATION OWNERSHIP: five of seven emotions define a head
    //    tilt (XVII.1's head_tilt field) as a live pitch value, not a baked
    //    asset tilt — a second value, after mouth, that Rotation and Emotion
    //    both claim. Resolve BEFORE the rotation cohort, since it changes
    //    theta/phi themselves:
    if emotion.head_tilt is not NONE:
        combined_phi = phi + emotion.head_tilt_pitch_delta(emotion_intensity)
        phi = clamp(combined_phi, -PITCH_HARD_SWAP_THRESHOLD + EPSILON,
                                    PITCH_HARD_SWAP_THRESHOLD - EPSILON)
        // Clamped so an emotion's tilt stacked with a live look-down/up input
        // never crosses Part V's 45.1 deg pitch hard-swap threshold as a side
        // effect of an emotion change. A HIGH-intensity DEFEAT (head_tilt up
        // to 30 deg forward) stacked with a legitimate look-down input is
        // exactly the case this guards against. If the combined value would
        // exceed the clamp, the emotion's own asset-level exaggeration should
        // carry the remaining intensity instead — not a rotation-boundary cross.

    rotation_output = EvaluateRig(state, theta, phi, Z_cam)      // 1. rotation cohort

    emotion_output = ApplyEmotionCohort(rotation_output, emotion, emotion_intensity)
    //   2. swaps feature assets per the Emotion x View Matrix (XVII.4),
    //      inside the view-zone rotation already resolved.
    //
    //      PRECONDITION (Emotion Pivot Anchor Uniformity — previously
    //      unstated anywhere): every emotion variant of a feature MUST
    //      share the exact same anchor coordinate as that feature's
    //      neutral asset (assert asset.anchor == NEUTRAL_ASSETS[feature].anchor,
    //      XVII.6). A rotation swap only ever fires at a declared threshold,
    //      the one place Local Delta Reset (XIV.5) runs — an emotion swap
    //      can fire at ANY live (theta, phi), including mid-parallax-slide
    //      with a nonzero offset already applied. If the anchor is
    //      guaranteed identical, step 1's already-computed (delta_x, delta_y)
    //      applies unchanged to the incoming emotion asset with no separate
    //      reset needed. Without that guarantee, a hand-drawn emotion asset
    //      whose visual center doesn't sit on the neutral asset's Reference
    //      Cross point pops on swap — and SNAP transitions (crossfade: NONE,
    //      XVII.3) have no fade to mask it, unlike every rotation boundary.

    final_output = ApplyViseme(emotion_output, viseme, emotion, emotion_intensity)
    //   3. mouth-shape ownership: viseme and emotion both target the mouth.
    //      if emotion_intensity >= MODERATE:
    //          mouth = emotion.mouth_asset       // emotion's shape wins
    //          mouth.aperture = modulate(mouth.aperture, viseme.aperture_delta)
    //          // secondary open/close modulation on top of the emotion shape,
    //          // NOT a swap to the plain neutral viseme asset
    //      else:
    //          mouth = viseme.mouth_asset         // ordinary viseme governs
    //          // emotion carried by eyes/brows only at this intensity

    return final_output

// This still requires a dedicated per-emotion mouth-open/mouth-closed asset
// pair at minimum (NOT the full 5-shape viseme set crossed with all 7
// emotions) — budget for it explicitly (IX.4) rather than discovering the
// gap in production.
```

---

## PART XV — Atmospheric Perspective & Depth Haze

Parts II–III give the rig **parallax depth** (layers translate at different rates) but no **atmospheric depth** — the rig's Z-layers separate by motion and Z-order alone, never by the aerial-perspective cue (distant things lighter, cooler, lower-contrast). This part is the programmatic form of the atmospheric-veil principle (X.5): a translucent overlay whose opacity is a function of its distance from the artwork, animated per frame. It is **additive** to the parallax system (composites after displacement, never replaces it) and **optional per shot** (the monoline cel look of Part I deliberately has no atmospheric perspective).

### XV.1 The Aerial-Perspective Law

```text
// Canonical model (landscape painting + observational physics):
// exponential decay of contrast with distance (Beer-Lambert light scattering).

function Haze(Z, k):
    // Z = layer's Z-depth (same axis as the Z_sort from XIV.1)
    // k = scattering coefficient (inverse depth). Small = clear air; large = fog.
    return 1.0 - exp(-k * Z)
    // Z = 0      -> haze = 0   (layer at camera, no veiling)
    // Z -> inf   -> haze -> 1  (fully the haze color, no contrast survives)

// WHY EXPONENTIAL (not linear/smoothstep):
//   Light scattering through a uniform medium IS exponential (Beer-Lambert).
//   Any linear or smoothstep approximation produces a haze that LOOKS synthetic.
//   The exponential is the only mathematically defensible form.

// k CALIBRATION:
//   Tune against the seam-margin depth (II.4) so the haze becomes visible
//   only at depths where parallax alone starts to read as flat.
//   Typical range for a head rig: k ~= 0.2 .. 1.0 (inverse R units).
```

### XV.2 The Veil as a Per-Layer Compositing Operation

```text
function ApplyVeil(layer, haze_color, mist_intensity, k):
    Z_L = layer.logical_Z_depth                // NOT the stack index (XV.4)
    h = Haze(Z_L, k) * mist_intensity          // in [0, 1]
    layer.displayed_color = lerp(layer.source_color, haze_color, h)
    return layer

// RULES OF THE COMPOSITE:

// 1. THE VEIL NEVER MOVES A VERTEX.
//    Like parallax offset (III.4) and F_prox (III.5), the haze is a PER-PIXEL
//    color operation on already-rasterized art. It composites AFTER the line
//    is rendered in screen-space pixels (III.5). It cannot thicken the stroke;
//    it can only tint visible pixels toward the haze color.

// 2. THE VEIL IS DEPTH-ORDERED, NOT Z-STACK-REORDERED.
//    Composite runs back-to-front over the existing Z-stack (II.1). A layer
//    that has promoted to stack position 1 (BackHair at Z7, II.2) still renders
//    front-most but still receives its own Haze(Z_L) based on its LOGICAL depth,
//    not its stack index. This is what lets a back-promoted layer read as
//    "in front but still far" rather than snapping to full clarity.

// 3. THE HAZE COLOR IS SHOT-LEVEL, NOT LAYER-LEVEL.
//    One haze_color per shot, shared by every layer; only Haze(Z_L) varies.
//    Per-layer haze colors break the aerial-perspective illusion (the eye
//    reads the gradient as unrelated tints, not one medium).
//    haze_color examples:
//      cool light grey  (#c8ccd2) for air
//      white            (#ffffff) for dense fog
//      warm amber       (#d9b382) for golden-hour
```

### XV.3 The `mist` Parameter & the Melt-Away Crossfade

```text
// The veil's effective opacity is animated per frame by a `mist` parameter.
// The digital analog
// is a `mist` parameter in [0, 1] scaling mist_intensity. It may be:

// (a) STATIC shot-wide constant:
mist = shot.mist_constant            // e.g. 0.6 foggy, 0.0 default cel

// (b) BOUND TO VIEW STATE (the melt-away crossfade):
//     In walk-behind states (Z5/Z6/Z7, XII.4), bridge-safe features would
//     normally cut to visibility 0.0. With the veil, the hide becomes a
//     haze-mediated dissolve: the feature stays at full source opacity but
//     is fully veiled, so it "melts away" (the atmospheric-veil principle).

function MeltAwayVisibility(layer, state, theta, B, W):
    if state.yaw_zone not in WALK_BEHIND_ZONES:
        return layer.source_opacity                    // normal
    // else: drive the haze ramp through the Parameter-Space Crossfade (XIV.4)
    alpha = CrossfadeAlpha(theta, B, W)                // speed-independent
    return lerp(layer.source_opacity, haze_color, alpha)  // dissolves, no cut

// (c) BOUND TO CAMERA PROXIMITY:
//     mist ramps up with F_prox (III.5) so a close-up in fog reads denser
//     than a wide shot in the same fog. Physically correct: closer camera
//     = more fog volume in the cone = more scattering.
mist = base_mist * F_prox
```

### XV.4 Seam-Margin Interaction

```text
// The optical gap between depth planes (X.1) is the
// physical "air" the rig's seam margin (II.4) leaves between Z-layers.
// With the veil enabled, that gap reads visibly: a layer sliding behind
// another during a turn trails a faint haze edge (the back layer's
// Haze(Z_L) is higher than the front layer's), which makes the depth read
// volumetrically rather than as a flat collage.

// SEAM-MARGIN RULE WITH THE VEIL:
//   The 8-12% extension margin (II.4) is sized to cover PARALLAX DISPLACEMENT,
//   NOT the haze gradient. A veiled edge can be slightly SMALLER than an
//   unveiled edge (the haze softens the seam) but never zero; the stroke
//   still needs its solid-fill backing (I.1) so the haze composites against
//   a clean edge, not a half-transparent line.

function SeamMarginWithVeil(layer, base_margin_8_to_12_pct, veil_enabled):
    if not veil_enabled:
        return base_margin_8_to_12_pct * F_prox        // standard rule (II.4)
    // veil softens the seam; allow a small reduction but keep a floor
    veiled_reduction = 0.85                            // 15% smaller acceptable
    return max(base_margin_8_to_12_pct * F_prox * veiled_reduction,
               layer.stroke_width * 2.0)               // absolute floor
```

### XV.5 When to Use the Veil (Decision Gate)

```text
function ShouldUseVeil(shot):
    // The veil is a SHOT-LEVEL aesthetic choice, not a rig default.
    // The default monoline cel look (Part I) has NO atmospheric perspective.

    if shot.style == DEFAULT_ANIME_CEL:
        return false     // flat high-contrast is the aesthetic contract
                         // (XIII.6 uncanny-valley rule 1: match realism levels)

    if shot.distance == TELEPHOTO:
        return false     // parallax already flattened (III.5); veil adds mud

    if shot.framing == EXTREME_CLOSEUP_SINGLE_FEATURE:
        return false     // no depth to haze

    // Use the veil when:
    if shot.style in {WATERCOLOR_STORYBOOK, FOG_CUTSCENE, MEMORY_FLASHBACK}:
        return true
    if shot.needs_melt_away_walk_behind:                // XV.3
        return true
    if shot.framing == WIDE_ESTABLISHING and shot.distance != TELEPHOTO:
        return true      // parallax alone reads flat at distance
    return false
```

### XV.6 Sub-Pixel Jitter (the "Hand-Moved" Option)

```text
// The hand-moved jitter principle (X.6): perfectly deterministic motion can
// read as mechanical; a tolerated sub-pixel position noise reads as
// "breathing" and is, in some aesthetics, essential to the medium.
// Hand-re-pinning leaves residual sub-pixel jitter (~0.1-0.3
// px at production resolution). The rig's math is
// exact by design (Part XIV is deterministic), so jitter is an OPTIONAL
// aesthetic overlay, NOT a default.

function ApplyJitter(layer, t):
    // J_amplitude = 0  by DEFAULT (the rig is exact; jitter is a creative choice)
    if layer.J_amplitude == 0:
        return layer.parallax_offset

    // Per-layer Perlin/value noise seeded per layer (so two layers don't move
    // in lockstep — that reads as a bug, not breathing).
    n = noise2D(layer.seed, t * layer.J_frequency)   // in [-1, +1]^2
    jitter = layer.J_amplitude * n

    // Jitter scales MONOTONE with layer depth: closer layers jitter slightly
    // more (their parallax offset is bigger, so absolute jitter reads at the
    // same RELATIVE scale). Tie J_amplitude to |C_peak| (III.1).
    jitter *= abs(layer.C_peak) / MAX_C_PEAK         // normalize to depth class

    return layer.parallax_offset + jitter

// DEFAULTS:
//   J_amplitude  = 0      (OFF)
//   J_frequency  = 0.5 Hz
// AESTHETIC RANGE WHEN ON:
//   J_amplitude  in [0.1, 0.3] px
//   J_frequency  in [0.3, 1.0] Hz

// CRITICAL: the jitter applies to the PARALLAX OFFSET, AFTER all Part XIV math.
// It NEVER applies to source-art vertices (X.3 cmd 1 still holds — the art is
// immutable; only the pin's translation jitters).
```

---

## PART XVI — Anime Girl Proportions & Personality (Owner's Preference)

The practical art reference for an attractive anime girl character (shōjo/moe/bishōjo). Deliberately simple: line-art construction only, no meshing/blending, no new math. Numbers are the consensus anime construction canon, grounded in the neoteny theory of Part XIII.

All vertical fractions are of **total head height** (0.0 = skull top, 1.0 = chin bottom) unless noted.

### XVI.1 Front-View Canonical Proportions (data table)

```text
// The defining anime departure: eyes at vertical CENTER (y ~= 0.50),
// not the realistic upper-third (y ~= 0.43). Everything cascades from that.

PROPORTIONS_FRONT = {
    // measurement : { realistic, ANIME_DEFAULT, moe_extreme, mature_extreme }
    "head_width_to_height":      { real: 2/3,   default: 3/4,     moe: 4/5,    mature: 2/3    },
    "cranium_to_lower_face":     { real: 1/1.6, default: 1/1,     moe: 1/0.8,  mature: 1/1.4  },
    "eye_baseline_y":            { real: 0.43,  default: 0.50,    moe: 0.55,   mature: 0.45   },
    "eye_width_to_face_width":   { real: 1/5,   default: 1/4,     moe: 1/3.5,  mature: 1/6    },
    "eye_width_to_height":       { real: 3/1,   default: 2/1,     moe: 1.4/1,  mature: 3/1    },
    "interocular_gap_to_eye":    { real: 1.0,   default: 0.85,    moe: 0.7,    mature: 1.0    },
    "nose_baseline_y":           { real: 0.55,  default: 0.68,    moe: 0.68,   mature: 0.60   },
    "mouth_baseline_y":          { real: 0.80,  default: 0.82,    moe: 0.85,   mature: 0.78   },
    "mouth_width_to_eye":        { real: 0.7,   default: 0.4,     moe: 0.3,    mature: 0.6    },
    "brow_to_eye_gap":           { real: 0.06,  default: 0.03,    moe: 0.02,   mature: 0.05   },  // of head height
    "neck_width_to_head":        { real: 0.6,   default: 0.4,     moe: 0.3,    mature: 0.5    },
}

// CONSTRUCTION GRID (front view — I.4's 5-Part Grid at anime-default proportions,
// NOT a separate 4-part grid; equal-segment division of these same 5 parts
// gives the realistic column above, not this one):
//   | 0.5 margin | EYE | 0.8 gap | EYE | 0.5 margin |
//   eye centers at y = 0.50
//   nose dot at y ~= 0.68 (size 0.05-0.10 of eye width, often omitted)
//   mouth line at y ~= 0.82 (width 0.4 eye widths)
```

### XVI.2 Eye Construction & Personality Signals

```text
// The OUTER CANTHUS angle is the personality switch.

enum EyeShape {
    TSURIME,   // outer corner slants UP   10-20 deg above inner. tsundere/confident/fierce
    TAREME,    // outer corner slants DOWN 10-15 deg below inner. gentle/shy/deredere/moe
    JITOME,    // half-lidded, flat top lid, narrow vertical.    bored/kuudere/aloof
    ROUND,     // innocent, young
}

EYE_ANATOMY = {
    "iris_to_eye_opening":    { real: 0.55, default: 0.78, moe: 0.95 },   // colored disc fills eye
    "pupil_to_iris":          0.33 .. 0.50,                              // diameter ratio
    "upper_lash_to_lower_coverage": 2.5,                                 // fill-area ratio, NOT stroke weight — both strokes are width W (I.1)
    "highlight_count":        { default: 2, sparkle_max: 3 },
    "highlight_placement":    [UPPER_LEFT_large, LOWER_RIGHT_small, /*optional*/ tiny_secondary_near_first],
}

// CANONICAL HIGHLIGHT RULE (the dekame / star-highlight shōjo convention):
//   1-2 standard, 3 for max sparkle.
//   One large highlight UPPER-LEFT of pupil + one small LOWER-RIGHT
//   (opposite corners = spherical "wet" read).
```

### XVI.3 Per-View Proportion Deltas (the turn)

```text
// These are ART-DIRECTED deltas (how each hand-authored card is drawn),
// NOT the cosine-foreshortening math of XII.3 (that governs the slide).

PER_VIEW_PROPORTIONS = {
    FRONT: {
        yaw: 0, symmetric: true,
        grid: "5-part at full width (I.4, anime-default proportions)",
        eyes_at: 0.50,
    },
    THREE_QUARTER: {
        yaw: 45,
        far_eye_width_factor: 0.65,        // far eye compresses to ~0.6-0.7 of near
        interocular_gap: "narrows on far side",
        nose: "short wedge with visible bridge, shifts toward near cheek",
        far_jaw: "contour curve visible",
        near_ear: "appears at center",
        far_brow: "shortens to match compressed browline",
    },
    PROFILE: {
        yaw: 90,
        visible_eyes: 1,
        eye_width_factor: 0.5,              // slit, ~0.5 of front-view width
        nose: "small triangle forehead->lip, protrudes ~0.05-0.08 head-width",
        contour: "forehead->nose->lip->chin one continuous curve",
        ear: "vertical center",
        neck_meets_skull: "behind the ear",
        mouth_width_factor: 0.3,
    },
    BACK_THREE_QUARTER: {
        yaw: 135,
        visible_features: "mostly hair silhouette; sliver of far cheek + far ear tip",
        jaw: "hint only",
        eyes_nose_mouth: HIDDEN,            // bridge-safe, XII.4
        read_carrier: "hairstyle silhouette + back-fuzz planes",
    },
    BACK: {
        yaw: 180,
        visible_features: NONE,
        visible: "pure hair + neck; hairline at nape; neck narrows into skull",
    },
    TOP: {                                  // looking down
        pitch: +90,
        crown_dominates: 0.65,              // ~60-70% of visible head
        face: "foreshortens to downward wedge; parting/crown + tops of bangs visible",
        eyes_nose_mouth_visibility: 0.0,    // V.2
    },
    BOTTOM: {                               // looking up
        pitch: -90,
        dominant: "jaw, chin, nostrils",
        nose_underplane: "reads strongly",
        eyes: "recede up and away",
        neck: "disappears behind chin",
        // carried by parallax on the Under-Plane asset (V.4); no dedicated -90 asset
    },
}
```

### XVI.4 Hair Design (the silhouette carrier)

```text
// Hair is the IDENTITY layer — on back/back-3/4 it carries the whole read.
// Drawn as TWO layers: outer mass (silhouette) + face cutout (hole).

HAIR_RULES = {
    "annulus": true,                        // outer mass + face-shaped cutout
    "volume_head_multiplier": {
        real: 1.05, default: 1.15, moe: 1.25, mature: 1.05
    },                                      // head+hair is 1.10-1.25x taller than bare cranium
}

enum BangType {
    STRAIGHT_ACROSS,    // neat, classic, young
    CENTER_PARTED,      // mature, balanced
    SIDE_SWEPT,         // stylish, modern, slightly mysterious
    HIME_CUT,           // straight blunt bangs + straight sidelocks + long back.
                        //   elegant/ojou/refined/traditional
    BABY_BANGS,         // short, above brow. bold/quirky/retro
}

SIDE_LOCKS = {
    "length": "~1 face-length (temple to chin/shoulder)",
    "width": "0.25-0.35 of face width each",
}

AHOGE = {
    "count": "1 (or 2-3) sprout(s) from the crown",
    "height": "0.15-0.25 of head height",
    "breaks_centerline": true,              // the canonical controlled asymmetry (XIII.4)
    "reads_as": "airheaded/clumsy/energetic/moe",
}

HAIR_LENGTH = {
    SHORT:      "< 1.0 head-heights (above chin)",
    BOB:        "1.0-1.5 (chin to shoulder)",
    MEDIUM:     "1.5-2.5 (shoulder to mid-back)",
    LONG:       "2.5-4.0+ (waist and beyond)",
    TWIN_TAIL:  "styled; tail mass ~= 1-2 head-heights",
    PONYTAIL:   "styled; tail mass ~= 1-2 head-heights",
}
```

### XVI.5 Mouth, Nose, Brow Micro-Features

```text
MICRO_FEATURES = {
    NOSE: {
        front_size: "0.05-0.10 of one eye-width (dot/triangle/shadow)",
        often_omitted: true,
        profile_protrusion: "0.05-0.08 of head-width (small triangular wedge)",
    },
    MOUTH: {
        default_width: "0.3-0.5 of one eye width",
        construction: "short dead-center line/gap + tiny lower-lip tick",
        smile_corner_limit: "must NOT exceed inner edge of eyes",
        open_shape: "small rounded (cat-mouth omega or 3)",
    },
    BROW: {
        thickness: "single tapered stroke, 1-2 px (moe = hair-thin)",
        position: "just above upper lash, gap 0.02-0.04 head-height",
        arch: "gentle peak over outer third of eye",
        tilt_asymmetry: {
            raise_inner: "sadness/worry",
            raise_outer: "confidence/anger",
            lower_inner: "anger",
        },
    },
}
```

### XVI.6 Personality Expression (dere archetypes & color theory)

```text
// Visual shorthand: each proportion choice signals a trope the audience reads instantly.

PERSONALITY_TRAITS = {
    EYES:     { TSURIME: "tsundere/confident/fierce",
                TAREME:  "gentle/shy/deredere/moe",
                JITOME:  "bored/kuudere/aloof" },
    CHIN:     { SHARP_V: "mature/elegant/older",
                ROUND:   "young/cute/childlike" },
    BROWS:    { THICK:   "energetic/boyish/tomboy",
                THIN:    "delicate/refined/feminine" },
    MOUTH:    { SMALL:   "demure/shy",
                WIDER:   "expressive/energetic" },
    NEOTENY:  { BIG_EYES_BIG_CRANIUM: "child/moe",
                SMALLER_EYES_LONGER_FACE: "mature/adult" },
}

DERE_ARCHETYPES = {
    TSUNDERE: { eyes: TSURIME, chin: SHARPISH, hair: "sometimes twin-tails", default_mouth: "slight scowl" },
    YANDERE:  { eyes: "tareme or flat", hair: "long dark", mouth: "unnaturally calm/flat" },
    KUUDERE:  { eyes: JITOME, hair: "blue/silver", expression: "minimal" },
    DANDERE:  { eyes: TAREME, hair: "soft bangs covering eyes", mouth: "small" },
    DEREDERE: { eyes: "round tareme", mouth: "open bright smile", colors: "warm" },
    HIMEDERE: { hair: HIME_CUT, chin: SHARP_V, eyes: TSURIME, manner: "refined" },
}

ANIME_COLOR_THEORY = {
    RED_ORANGE:    "fiery/hot-tempered/passionate (tsundere)",
    BLUE:          "calm/cool/intelligent/aloof (kuudere). Pale-skin + blue-hair + quiet = template",
    PINK:          "cute/innocent/sweet",
    SILVER_WHITE:  "mysterious/otherworldly/powerful",
    GREEN:         "natural/serene/healing",
    BLONDE:        "foreign/gyaru/regal/noble",
    BLACK:         "ordinary protagonist/traditional/mysterious",
}
```

### XVI.7 Owner's Preference Slider Config

```text
// The construction as 7 dials the owner sets. Geometry Contract (IX.2) holds
// at every combination — these are PROPORTION inputs, not rig-level changes.

struct OwnerPreferenceConfig {
    // slider : { realistic, ANIME_DEFAULT, moe_extreme, mature_extreme }
    eye_size:           SliderBand;        // eye / face width:    1/5, 1/4, 1/3.5, 1/6
    chin_sharpness:     SliderBand;        // rounded adult, soft V, round, pointed V
    hair_volume:        SliderBand;        // 1.05, 1.15, 1.25, 1.05(flat)
    brow_thickness:     SliderBand;        // medium, thin, hair-thin, medium-thick
    mouth_size:         SliderBand;        // 0.7, 0.4, 0.3, 0.6   (of eye width)
    neoteny_level:      SliderBand;        // 1:1.6, 1:1, 1:0.8, 1:1.4  (cranium:lower-face)
    neck_width:         SliderBand;        // 0.6, 0.4, 0.3, 0.5   (of head width)
}

// USAGE:
//   1. Pick a target personality from XVI.6.
//   2. Push each slider toward the column that personality favors.
//   TSUNDERE -> tsurime + smaller eyes + sharp chin + thicker brows + medium mouth
//   MOE MASCOT -> tareme + huge eyes + round chin + hair-thin brows + tiny mouth
//                  + max neoteny + thin neck
//   The "owner's preference" is literally which column each slider sits in.
```

### XVI.8 Construction Algorithm (simple line-art, no meshing)

```text
function ConstructAnimeGirlHead(prefs: OwnerPreferenceConfig, view: ViewId):
    R = CRANIUM_RADIUS
    // 1. Cranium circle + axes
    cranium = Circle(center=(0,0), radius=R)
    equator = Line(y=0); centerline = Line(x=0)

    // 2. Chin at y = -1.5R, sharpness per slider
    chin_y = -1.5 * R
    chin = ChinPoint(y=chin_y, sharpness=prefs.chin_sharpness)

    // 3. Jaw curves (Bezier, I.2)
    jaw_L = Bezier(from=equator_left, to=chin, cp1=(R, -0.75R), cp2=(0.4R, -1.42R))
    jaw_R = mirror(jaw_L)

    // 4. Eye baseline at vertical center
    eye_y = prefs.neoteny_level.eye_baseline_y   // ~= 0.50 of head height

    // 5. 5-part grid at anime-default proportions (I.4) — same grid as the
    //    realistic column, non-equal segments, not a different "4-part" grid
    eye_w = prefs.eye_size.width_for_face_width(FACE_WIDTH)
    grid = FivePartGrid(margin=0.5*eye_w, gap=0.8*eye_w, eye=eye_w)

    // 6. Eyes per personality
    eye_shape = prefs.personality.eye_shape       // TSURIME / TAREME / JITOME
    eye_L = ConstructEye(shape=eye_shape, width=eye_w, aspect=prefs.eye_size.aspect,
                         highlights=2, iris_ratio=0.78)
    eye_R = mirror(eye_L)

    // 7. Nose + mouth + brows
    nose = NoseDot(y=0.68*head_h, size=0.08*eye_w)
    mouth = MouthLine(y=0.82*head_h, width=prefs.mouth_size*eye_w, gap_at_center=true)
    brow_L = BrowStroke(just_above=eye_L.upper_lash, thickness=prefs.brow_thickness,
                        tilt=prefs.personality.brow_tilt)
    brow_R = mirror_or_asymmetric(brow_L)

    // 8. Hair annulus
    hair = HairAnnulus(volume=prefs.hair_volume, bang_type=prefs.personality.bang_type,
                       cutout=face_shape)

    // 9. Ahoge if personality calls for it
    if prefs.personality.has_ahoge:
        hair.add_ahoge(height=0.20*head_h, off_center=true)

    // 10. Neck
    neck = Neck(width=prefs.neck_width * head_width, from=chin_down)

    // 11. Per-view deltas (XVI.3) — re-author against the Reference Cross (I.3, XII.1)
    return ApplyViewDeltas(assets, view, PER_VIEW_PROPORTIONS[view])

// ENTIRE CONSTRUCTION: circles + Beziers + dots + lines at fixed proportions.
// NO meshing. NO blending. NO deformation.
```

---

## PART XVII — Expression State Machine & Emotion Specs

The technical specification for the discrete-emotion layer — joy, anger, pride, sadness, defeat, relaxation, serious — and the effect-symbol (manpu) overlays. This is the programmatic counterpart to `art_guide.md` Part XVII. The contract mirrors rotation (Part IV): each emotion is a discrete feature-state the rig swaps to; transitions are parameter-driven with the same Schmitt/hysteresis + parameter-space crossfade; effect symbols are separate overlay layers with their own fade-in timing.

Feature deltas are relative to the **neutral front-view construction** of Part XVI, expressed via the standardized **Action Unit (AU)** shorthand (an anatomically-based facial-muscle movement). Angles are degrees-from-neutral. EW = eye-width (the canonical unit for effect placement).

### XVII.1 Emotion Definitions (Data Table)

```text
// Each emotion = a feature-state. Delta values are degrees-from-neutral
// unless noted. AU = Action Unit (Facial Action Coding System muscle group).
//
// IMPORTANT: every degree value below is AUTHOR-TIME drawing guidance for
// hand-building each emotion's own swap asset (Eye_Joy, Eye_Anger, ... —
// the naming scheme of XVII.6/Part VIII). It is NOT a runtime transform
// applied to the neutral asset at render time. X.4 does permit uniform
// rotation as a legal per-frame transform, which makes this ambiguous on
// a casual read — but Emotion Pivot Anchor Uniformity (III.6/XIV.9) only
// holds if every emotion variant is authored independently to already
// share the neutral asset's anchor point, not derived from it by transform
// at runtime. If a future revision wants to spend transform budget instead
// of asset budget for a specific low-drama feature (brow tilt is the most
// plausible candidate), say so explicitly per-feature — don't infer it
// from these numbers.

EMOTIONS = {
    JOY: {
        eye_shape:      "open at low; lower lid raises +U at mid (lower lash up 15-25 deg);
                         closed into upward bow arc at high (iris hidden);
                         fox-eye > < variant = two thick half-circles",
        brow:           "slightly raised or neutral; outer ends bow down to follow eye arc;
                         inner brow neutral",
        eyelid:         "half-lid droops over top 20-30% iris at rest; lower lid covers 25-40% on Duchenne squeeze",
        mouth:          "corners up+out 15-30 deg; opens to upward-curve horseshoe at high;
                         cat-mouth omega at closed cute variant",
        nose:           NO_CHANGE,
        cheeks:         "raised (+AU6); may blush at high intensity (joy+embarrassment blend)",
        head_tilt:      "slight 5-10 deg tilt to one side (warm variant)",
        au_signature:   "AU6 + AU7 + AU12 (Duchenne = genuine); social smile = AU12 alone",
    },
    ANGER: {
        eye_shape:      "open wide (sclera above iris) AND narrowed (hard stare squint);
                         iris smaller; sclera may shadow for vengeful read",
        brow:           "inner corners DOWN and together 10-20 deg (the anger crease between brows);
                         outer ends angle sharply inward  > <  brow",
        eyelid:         "upper lid high; lower lid tight and raised (the squint)",
        mouth:          "closed thin press OR open shout (wide downward-open, shark teeth at high);
                         corners slightly down in seething",
        nose:           "nostrils flare at high intensity",
        cheeks:         "tense, sometimes hollowed",
        head_tilt:      "chin DOWN 5 deg (brow-stare) OR chin up (dominant/defiant = anger->pride blend)",
        effects:        [ANGER_VEIN at moderate+],
        au_signature:   "AU4 + AU5 + AU7 + AU23/24; high: +AU17",
    },
    PRIDE: {
        eye_shape:      "open, narrow, or slightly lidded; gaze UP and OUT (looking down on viewer);
                         half-closed smug lid covers top 20% iris",
        brow:           "OUTER ends raised 10 deg, inner neutral (knowing arch);
                         often ONE brow raised (sardonic single-brow, AU2 unilateral)",
        eyelid:         "half-lid droop (smugness), covers top 25%",
        mouth:          "asymmetric smirk (AU12 stronger one side) OR closed confident line, corners slightly up",
        nose:           NO_CHANGE,
        cheeks:         "slight raise, no blush",
        head_tilt:      "10-20 deg BACKWARD pitch (defining feature) + slight yaw (looks over one shoulder)",
        au_signature:   "AU12 + head tilt back (no single FACS code)",
    },
    SADNESS: {
        eye_shape:      "open, slightly droopy; lower lid may sag; iris large/watery at high;
                         tear accumulation at inner canthus",
        brow:           "INNER corner raised 10-15 deg AND pulled together/down  (THE sadness signature;
                         entire inner brow high while outer end slopes down)",
        eyelid:         "upper lid droops over top 30-40% iris (tired lid)",
        mouth:          "corners DOWN 15-25 deg; slight lip stretch -> small inverted-U;
                         quivering open oval at high (wobbly mouth)",
        nose:           NO_CHANGE,
        cheeks:         "drawn up slightly; no blush",
        head_tilt:      "forward/down 10-20 deg pitch (drooping head)",
        effects:        [TEAR_DROPS at high, LONG_NOSE_BLUSH at intense weeping],
        au_signature:   "AU1 + AU4 + AU15; high: +AU17",
    },
    DEFEAT: {
        eye_shape:      "de-focused/voided: large round eyes NO iris/highlight (white-eye),
                         OR flat circles / > < / = =; pupils shrink to dots or vanish",
        brow:           "flat or slightly raised inner corners 5-8 deg; sometimes \\__/ shape",
        eyelid:         "heavy droop, upper lid covers top 40-50%",
        mouth:          "small open oval; wavy/zigzag line (trembling mouth); tiny cat-mouth omega",
        nose:           NO_CHANGE,
        cheeks:         "sunken; dark bags under eyes common",
        head_tilt:      "forward, chin near chest (20-30 deg pitch)",
        effects:        [BAGS_UNDER_EYES, GHOST_WISP from mouth (comedic death/depression)],
    },
    RELAXATION: {
        eye_shape:      "half-lidded: upper lid covers top 40-50% iris, lower neutral;
                         sleepy/satisfied eye; flat horizontal line + small up-curve at outer edge",
        brow:           "neutral or very slightly lowered (soft arch); closer to eye than alert-neutral",
        eyelid:         "the lid IS the expression: heavy droop is defining feature",
        mouth:          "closed, slightly parted, corners up 5-10 deg (soft smile);
                         cat-mouth omega for peaceful/cute variant",
        nose:           NO_CHANGE,
        cheeks:         "relaxed; faint warm blush sometimes",
        head_tilt:      "any relaxed pose: chin on hand (15 deg yaw) OR tipped back 5-10 deg",
        effects:        [SIGH_MUSHROOM occasionally],
    },
    SERIOUS: {
        eye_shape:      "narrowed, sharp: horizontal lens (almond); iris visible but small;
                         hard outline, REDUCED highlight (dead eye); dead-fish-eye removes highlight entirely",
        brow:           "lowered+flattened 5-10 deg inner-down; straightened not angled;
                         single thick straight line very close to eye",
        eyelid:         "upper covers top 20-30%, lower flat",
        mouth:          "closed thin line, corners neutral (the dash mouth: single short horizontal line)",
        nose:           NO_CHANGE,
        cheeks:         "tense, hollowed; shadow band under eye common",
        head_tilt:      "NONE: head dead-level, direct forward gaze",
        effects:        [DARK_SHADING_OVER_UPPER_FACE, SHADOWED_EYES regardless of room lighting],
    },
    SURPRISE: {
        // TRANSIENT ONLY — never a held/resting state, so it has no intensity
        // tiers and no entry in XVII.5's threshold table. Required by the
        // Anticipation beat and the FAST_DOUBLE_BLINK punctuation (XVII.3),
        // both of which referenced this pose before it existed as data here.
        eye_shape:      "upper lid raised well above neutral, sclera visible above iris;
                         lower lid NEUTRAL (unlike Anger, no lower-lid tightening)",
        brow:           "raised toward hairline 15-25 deg, both together, no asymmetry",
        mouth:          "small open oval or dropped-open shape; no shout-width/shark-teeth variant",
        nose:           NO_CHANGE,
        cheeks:         NO_CHANGE,
        head_tilt:      "NONE",
        duration:       "2-4 frames (~80-165ms at 24fps reference) — held by duration, not by
                         intensity tier, since this pose is never a resting state",
        view_variants:  "FRONT only; reuse with standard per-view compression (XVII.4) at 3Q/profile,
                         unlike the seven held emotions which need dedicated per-zone assets",
    },
}

// CROSS-EMOTION DISTINGUISHABILITY — not checked anywhere below.
// ANGER and SERIOUS both key off a narrowed/hard-stare eye_shape; SADNESS
// and DEFEAT both key off downcast head_tilt + drooping eyelid. Before
// locking a character's set, diff each pair's asset on {eye_shape, brow,
// mouth, head_tilt} and confirm at least two of the four differ enough to
// read at a glance — see ValidateDistinguishability, XVII.6.
```

### XVII.2 Emotion Effect Elements (Manpu Catalog)

```text
// Overlay iconography — NOT part of the neutral face. Separate art pieces
// layered on top of the expression, with their own Z-depth + fade-in timing.
//
// ANCHOR DOMAIN: every "pos" field below is relative-to-the-face prose with
// no stated domain, unlike every constructed feature in I.6 (theta_0, phi_0,
// R_cranium/R_jaw). Anchor each symbol to its nearest feature's own anchor
// coordinate (sweat drop / anger vein -> near Brow's anchor; tear streams ->
// near Eye's anchor) so it inherits the same spherical projection (XIV.1)
// as everything else and rotates/parallaxes with the head instead of
// visibly detaching from the face mid-turn.
//
// VIEWER-FACING SIDE SWAP: sweat drop and anger vein flip sides as yaw
// crosses center (XVII.4) — the same job II.2 already does for Side Hair
// near/far with a Schmitt state flag. Effects aren't in any declared Swap
// Cohort (IV.0: Face Base/Mouth/Brow/Eyes/Projections only) and aren't in
// the XI/XII matrix. Reuse the existing yaw-crosses-center boolean for the
// trigger and cross-fade the flip rather than a hard positional pop.

EFFECT_SYMBOLS = {
    SWEAT_DROP: {  // ase
        shape:  "single oversized teardrop (point up, bulb down); blue/cyan; small highlight dot",
        pos:    "above+side of head, ~1.5-2 EW from eye center, VIEWER-FACING side",
        anchor: NEAR_BROW_ANCHOR,   // theta_0/phi_0 domain, I.6 — was undefined
        size:   "0.5-1.0 EW tall (low) up to 1.5+ EW (extreme)",
        meaning:"embarrassment, exasperation, confusion, dismay, speechless discomfort",
        appears_at: INTENSITY_MILD_PLUS,   // I >= 0.25, XVII.5
        z_depth: ABOVE_HAIR,
    },
    TEAR_STREAMS: {  // namida
        shape:  "small ovals/droplets; single tear at inner canthus (sad);
                 laughter tears at outer canthus + lower lid;
                 twin fountain (two vertical jets, 1-2 EW) at high intensity",
        color:  "blue/cyan",
        appears_at: INTENSITY_HIGH,
    },
    BLUSH: {
        variant_parallel_lines: "2-4 short diagonal slashes (30-45 deg, 0.3-0.5 EW) under each eye -> romance/drunk/fever",
        variant_solid_patch:    "red/pink oval, ~0.5 EW below outer eye corner -> rosy cute",
        variant_long_nose:      "red vertical stripe down nose bridge connecting cheeks -> intense weeping",
        color:  "red (embarrassment) or pink (cuteness)",
        appears_at: INTENSITY_MODERATE,
    },
    ANGER_VEIN: {  // ikari maku, cross-popping vein
        shape:  "cruciform/quatrefoil: 4 diamond shapes radiating from center (like # or 4-point starburst)",
        pos:    "above temple/forehead, ~1-1.5 EW from eye, VIEWER-FACING side; above hair on back-turned",
        anchor: NEAR_BROW_ANCHOR,   // theta_0/phi_0 domain, I.6 — was undefined
        size:   "0.5-1.0 EW; multiples stack for greater anger",
        color:  "bright red",
        meaning:"anger, irritation, frustration (NOT rage — rage uses eye changes + speed lines)",
        appears_at: INTENSITY_MODERATE,   // I >= 0.25, XVII.5
    },
    FEAR_LINES: {
        shape:  "3-6 thin VERTICAL parallel lines over/around head+face (often dark shaded); OR band under eye",
        meaning:"fear, horror, drained of color, shock; WAVY variant = disgust",
        stroke_length: "0.5-1.0 head-height",
    },
    HAPPINESS_LINES: {  // warai-sen (crow's feet)
        shape:  "2-3 short upward-curving lines radiating from outer eye corner (fan at 15/30/45 deg)",
        size:   "0.3-0.5 EW long",
        meaning:"genuine/strong happiness; reinforces closed-eye = smile not squint",
        appears_at: INTENSITY_MODERATE_PLUS,
    },
    BAGS_UNDER_EYES: {
        shape:  "2-3 short HORIZONTAL parallel lines directly under lower lash",
        pos:    "~0.2-0.3 EW below eye; span 0.5-0.8 EW; centered under iris",
        meaning:"fatigue, defeat, illness, world-weariness; brooding serious character's permanent marker",
    },
    SPEED_LINES: {  // shuchusen
        shape:  "radiating lines converging on focal point (face/hand/object); straight, evenly spaced",
        variant_converging: "emphasis, realization, camera pushes in",
        variant_on_character: "motion, determination, combat-readiness",
    },
    CAT_MOUTH: {  // neko-guchi
        shape:  "sideways 3 / omega: two small humps replacing mouth; ~0.5-0.8 of neutral width",
        meaning:"mischief, feistiness, cuteness, playful smugness; occasional contentment",
    },
    SHARK_TEETH: {
        shape:  "triangular serrated teeth filling open mouth (5-10 equilateral triangles per row, white + black outline)",
        meaning:"mischievous/trickster grin, manic energy, comedic rage, smug superiority",
    },
    SPIRAL_EYES: {  // uzumaki-me
        shape:  "Archimedean spiral replacing iris/pupil; 1.5-2 turns; ~0.5 EW diameter",
        meaning:"dizziness, confusion, disorientation, KO",
    },
    FLAT_EYES: {  // the > < / = = face
        shape:  "flat squiggles or mirrored > < V-bends; OR two flat underscores",
        meaning:"nervousness, embarrassment, exasperation, suppressed frustration",
    },
}

// Other notable symbols:
//   CROSS_X_EYES (x x) -> dead, unconscious, KO
//   ELLIPSIS_ABOVE -> silence, something unsaid
//   SIGH_MUSHROOM -> awkward relief or depression
//   GHOST_WISP_FROM_MOUTH -> comedic depression / figurative death
```

### XVII.3 Emotion Transition State Machine

```text
// Mirrors the rotation system (Part IV): discrete state swap + crossfade,
// never a vertex morph. Timing differs — emotion beats are dramatic.
//
// UNLIKE Part IV's crossfade widths (deliberately specified in rotation-parameter
// space, XIV.4, precisely so they don't drift with playback speed), the values
// below have no parameter-space equivalent to measure against. Store them as
// milliseconds against a stated reference frame rate, not raw frame counts —
// "frames_to_peak: 1" is a different real-world duration at 24fps than at 60fps,
// which reintroduces the exact speed-dependent pop this manual eliminated from
// rotation crossfades. Reference: 24fps.

EMOTION_TRANSITION_MODES = {
    SNAP: {
        // 1 frame (~42ms) hold at peak, at the 24fps reference. For comedic/emotional beats.
        // neutral -> rage is often a 1-frame snap; reset snap to neutral is also 1 frame.
        ms_to_peak: 42,
        crossfade: NONE,
        use_case: "comedic beat, sudden shock, reset",
    },
    EASE: {
        // 12-24 frames (~500-1000ms) at 24fps reference. For emotional drift (sadness welling up).
        ms_to_peak: [500, 1000],
        crossfade: PARAM_SPACE,    // same +/- window as rotation (XIV.4)
        ease_curve: SMOOTHSTEP,    // 3t^2 - 2t^3 minimum (XIV.2)
        use_case: "sadness welling, pride dawning, relaxation settling",
    },
    BLINK_PUNCTUATION: {
        // Closed-eye frame masks the swap, same job as a rotation crossfade's overlap window.
        // MANDATORY (not optional) for any emotion pair that fails the silhouette-delta
        // test below — was previously prose-only and excluded from the mode enum entirely,
        // so ValidateEmotionTransition (XVII.6) rejected a technique this section endorses.
        ms_to_peak: [125, 250],   // typical blink duration
        crossfade: NONE,          // the closed frame IS the mask, no separate crossfade needed
        requires: HAS_CLOSED_EYE_FRAME,
        use_case: "any pair failing SilhouetteDeltaTest; surprise/realization beats",
    },
    SMEAR: {
        // 1 HAND-DRAWN smear asset (face depicted stretched toward target, motion-blur
        // style) for one frame before snap-to-extreme — a separate authored drawing, like
        // the effect symbols, NOT a runtime stretch transform applied to the existing asset
        // (a runtime non-uniform stretch is a vertex morph, prohibited by X.4/X.7 #1).
        ms_to_peak: 42,            // one frame at 24fps reference, same as SNAP
        crossfade: NONE,
        use_case: "high-speed emotion changes, stylistic choice not default",
    },
}

// FEATURE LEAD ORDER (anime convention):
//   1. EYES LEAD: brows + upper lids move first, ~80-165ms ahead.
//   2. MOUTH FOLLOWS.
//   3. EFFECT SYMBOLS (tears/vein/sweat) appear AFTER face settles — punctuation, not anticipation.

// ANTICIPATION (the opposite-then-emotion beat):
//   Eyes widen into the SURPRISE pose (XVII.1) for ~80-165ms BEFORE snapping to anger.
//   Sells the "boiling point" — face briefly visits opposite pole before committing.

// HOLD FRAMES:
//   Held extreme = ~125-330ms on twos/threes at peak (24fps reference). This is where effect symbols appear.

// BLINKS AS EMOTION PUNCTUATION (the cleanest crossfade mechanism):
//   PASSING_BLINK: eyes close -> re-open in different expression.
//     The closed-eye frame MASKS the swap — exactly like rotation crossfade.
//   FAST_DOUBLE_BLINK: surprise.
//   LONG_SLOW_BLINK: weariness / processing grief.
//   SINGLE_BLINK_AFTER_BEAT: "the realization landed."

// SMEAR FRAMES:
//   High-speed emotion changes may show 1 HAND-DRAWN smear asset (face depicted stretched
//   toward target, motion-blur style) for one frame before snap-to-extreme — this is a
//   separate authored drawing, like the effect symbols, NOT a runtime stretch transform
//   applied to the existing asset (a runtime non-uniform stretch is a vertex morph and is
//   prohibited by X.4/X.7 #1 same as everywhere else in the rig). Use sparingly — stylistic
//   choice, not default.

// EMOTION-PAIR TOPOLOGY: with 7 held emotions there are 21 unordered pairs; nothing
// previously said which may crossfade directly vs. must route through NEUTRAL.
function SilhouetteDeltaTest(emotion_a, emotion_b, feature):
    // Same test as IV.0's Swoosh-vs-crossfade rule, applied to emotion pairs.
    delta = non_overlapping_outline_area(asset(emotion_a, feature), asset(emotion_b, feature))
    return delta > TOLERANCE   // true = pair FAILS plain crossfade, needs BLINK_PUNCTUATION or a NEUTRAL hop

// e.g. DEFEAT's void-eyes -> ANGER's narrow hard-stare, or JOY's closed upward-bow arc ->
// SERIOUS's flat dead-fish-eye: bigger silhouette jumps than anything in the 8-zone
// rotation matrix. Any pair where SilhouetteDeltaTest is true must use BLINK_PUNCTUATION
// or two SNAPs through NEUTRAL — never a raw PARAM_SPACE crossfade between the extremes.

// THE TRANSITION CONTRACT (same as rotation IV.0):
//   Each feature swap (eye shape, brow position, mouth shape) = discrete asset swap
//   gated by the emotion-state change, softened by ONE of the four EMOTION_TRANSITION_MODES
//   above (SNAP / EASE / BLINK_PUNCTUATION / SMEAR) — never left as an implicit two-mode choice.
//   Effect symbols (sweat/vein/tears) = separate overlay layers with own fade-in timing,
//   composited AFTER the face settles.
```

### XVII.4 Emotion × View Matrix (Per-View Variants Required)

```text
// An emotion must read at every view angle (Part XI). The interaction rules:

EMOTION_VIEW_RULES = {
    far_eye_brow_at_3Q: "mirrors near side but compresses;
                         asymmetric expressions (pride smirk, single raised brow) preserve
                         the asymmetric read on the near half (far side occluded);
                         far eye in Duchenne smile still shows up-arc lower lid, foreshortened",

    mouth_compression: {
        FRONT:    "full symmetric width",
        THREE_Q:  "70-80% width; far corner tucks under nose shadow;
                   cat-mouth omega + shark teeth compress to single visible hump/triangle row",
        PROFILE:  "single curve/angle; upper lip = short forward-projecting shape, lower = receding;
                   open-shout mouths = single vertical open shape",
    },

    effect_placement: {
        // View-DEPENDENT — swap sides when character turns; NOT bilaterally symmetric
        SWEAT_DROP:    "viewer-facing side",
        ANGER_VEIN:    "viewer-facing side",
        BLUSH:         "both cheeks at front; NEAR cheek only at 3Q/profile (far occluded, no wrap)",
        TEAR_STREAMS:  "originate primarily from NEAR eye",
    },

    per_view_variants_REQUIRED: {
        // Mouth geometry that breaks at profile needs a dedicated P90 asset.
        // Front shape CANNOT be uniformly scaled.
        OPEN_SHOUT:    [P00, P45, Pn45, P90, Pn90],
        SHARK_TEETH:   [P00, P45, Pn45, P90, Pn90],
        TONGUE_OUT:    [P00, P45, Pn45, P90, Pn90],
        // These compress acceptably — reuse ONE asset with horizontal scale:
        CAT_MOUTH:     [P00 only, scaled],
        CLOSED_MOUTH:  [P00 only, scaled],
        // 2D symbols — per-view variant only at extreme angles:
        SPIRAL_EYES:   [P00 only, except extreme P90],
        FLAT_EYES:     [P00 only, except extreme P90],
    },
}

// THE EMOTION x VIEW MATRIX:
//   Each emotion at each of the 8 yaw zones x 3 pitch bands needs the feature variants
//   + the effect symbols placed correctly. This is the same Full-Matrix Pre-Build of Part 0,
//   extended by ONE DIMENSION: emotion adds a third axis to the existing yaw x pitch grid.
```

### XVII.5 Emotion Intensity Thresholds

```text
// Each emotion has an INTENSITY AXIS: mild joy -> ecstatic; mild annoyance -> raging anger.
// Intensity drives which features change + when effect symbols appear.
//
// FIX: this axis previously had no numeric range anywhere in either document —
// four qualitative labels only — despite the Glossary claiming it "drives the
// emotion swap via the same Schmitt/crossfade contract as rotation" (XVII gloss).
// Define it as a continuous parameter and give it the same enter/exit hysteresis
// contract as IV.0/XIV.3, so THRESHOLD_TABLE lookups below are actually numeric
// and ValidateEmotionAsset's `asset.intensity < THRESHOLD_TABLE[symbol]` (XVII.6)
// compares number-to-number instead of number-to-symbolic-label.

INTENSITY: float in [0.0, 1.0]

INTENSITY_LEVELS = {
    MILD: {
        range:           [0.00, 0.25],
        feature_changes: "trace only — mouth corners up 5-10 deg, brow down 5 deg",
        effect_symbols:  [SWEAT_DROP],     // any awkwardness
    },
    MODERATE: {
        range:           [0.25, 0.55],
        enter: 0.25, exit: 0.20,           // Schmitt deadband, same pattern as IV.0
        feature_changes: "full AU combo; face reads clearly as the emotion",
        effect_symbols:  [ANGER_VEIN, BLUSH_PARALLEL_LINES, HAPPINESS_LINES_MOD_PLUS],
    },
    HIGH: {
        range:           [0.55, 0.85],
        enter: 0.55, exit: 0.50,
        feature_changes: "face DEFORMS — eyes change shape (closed-eye smile, void-white rage eye),
                          mouth geometry breaks (open shout, cat-mouth omega)",
        effect_symbols:  [TEAR_STREAMS, SHARK_TEETH, SPIRAL_EYES],
    },
    EXTREME: {
        range:           [0.85, 1.00],
        enter: 0.85, exit: 0.80,
        feature_changes: "SUPER-DEFORMED (SD) — entire character collapses to chibi proportions;
                          effect symbols take over the panel (bg solid red for rage, speed lines radiate)",
        effect_symbols:  [NOSEBLEED, SD_MODE],
        trigger:         "INTENSITY >= 0.85 (was: undefined qualitative 'exceeds expressive ceiling')",
    },
}

// THRESHOLD_TABLE now maps to the numeric enter value of each tier, not the
// symbolic label — ValidateEmotionAsset's comparison in XVII.6 is executable.
THRESHOLD_TABLE = {
    SWEAT_DROP:    0.25,   // MILD_PLUS
    ANGER_VEIN:    0.25,   // MODERATE
    BLUSH:         0.25,   // MODERATE
    TEAR_STREAMS:  0.55,   // HIGH
    SHARK_TEETH:   0.55,   // HIGH
    SPIRAL_EYES:   0.55,   // HIGH
    NOSEBLEED:     0.85,   // EXTREME
    SD_MODE:       0.85,   // EXTREME
}

// APPLICABILITY TO APPEAL VALIDATORS (Part XIII): Part XIII never mentions
// emotion, so it's undefined which of these tiers must still pass XIII.2/
// XIII.3/XIII.5. Non-comedic emotions (SADNESS, ANGER, PRIDE, SERIOUS) at
// MILD-HIGH should still pass; the EXTREME comedic breaks (SD_MODE,
// SPIRAL_EYES, void-eyes) are exempt by design. Tag each authored asset:
appeal_checked: bool   // per-asset, set at authoring time — see XIII.8
```

### XVII.6 Validation Pseudocode

```text
function ValidateEmotionAsset(emotion, asset, view):
    spec = EMOTIONS[emotion]

    // 1. Feature deltas match the spec for this emotion.
    //    FIX: spec.eye_shape etc. are natural-language strings (XVII.1) —
    //    ".matches()" against prose isn't executable the way every other
    //    validator in this manual checks a measurable property (stroke
    //    width, fill area, curvature). Require a parallel structured tag
    //    set per emotion (spec.eye_shape_tags = [CLOSED, UPWARD_BOW, ...])
    //    authored alongside the prose, and check membership against that —
    //    the prose stays as human authoring guidance, the tags are what
    //    the validator actually runs against.
    assert asset.eye_shape_tags.issuperset(spec.eye_shape_tags)
    assert asset.brow_tags.issuperset(spec.brow_tags)
    assert asset.mouth_tags.issuperset(spec.mouth_tags)

    // 1b. Emotion Pivot Anchor Uniformity (XIV.9/XVII.7's art_guide equivalent):
    //     every emotion variant of a feature must share the neutral asset's
    //     exact anchor coordinate, or an emotion swap firing off a rotation
    //     threshold (i.e. anywhere Local Delta Reset, XIV.5, isn't already
    //     running) pops. This was previously unenforced anywhere.
    assert asset.anchor == NEUTRAL_ASSETS[asset.feature].anchor

    // 2. Effect symbols present at the right intensity (now numeric, XVII.5).
    for symbol in spec.effects:
        assert asset.has_overlay(symbol) or asset.intensity < THRESHOLD_TABLE[symbol]

    // 3. Asymmetric emotions preserve asymmetry on the near half at 3Q.
    if emotion in {PRIDE} and view == THREE_Q:
        assert asset.near_side_asymmetry_preserved == true

    // 4. Mouth-geometry-break emotions have a dedicated P90 variant.
    if emotion_has_mouth_break(emotion):
        assert asset.has_variant(P90)

    // 5. Effect placement on viewer-facing side (not bilaterally symmetric).
    for symbol in [SWEAT_DROP, ANGER_VEIN]:
        if asset.has_overlay(symbol):
            assert symbol.placed_on_viewer_facing_side == true

function ValidateEmotionTransition(transition):
    // FIX: previously {SNAP, EASE} only — rejected BLINK_PUNCTUATION and
    // SMEAR, both of which XVII.3's own prose endorses as legitimate modes.
    assert transition.mode in {SNAP, EASE, BLINK_PUNCTUATION, SMEAR}
    assert transition.no_vertex_morph == true

    // Pairs failing the silhouette-delta test (XVII.3) must not use a raw crossfade.
    if SilhouetteDeltaTest(transition.from_emotion, transition.to_emotion, transition.feature):
        assert transition.mode in {BLINK_PUNCTUATION, SNAP}   // SNAP only if routed through NEUTRAL

    // Feature lead order: eyes first, mouth follows, effects after settle.
    // FIX: was raw frame count ("in [2, 4]", ambiguous set-membership syntax
    // for what prose describes as a continuous range) reintroducing the exact
    // framerate dependency XVII.3 fixed everywhere else in this Part. Now ms,
    // range-checked, against the same 24fps reference.
    assert 80 <= transition.eyes_lead_mouth_by_ms <= 165
    assert transition.effects_appear_after_face_settles == true

    // Blink punctuation is the cleanest crossfade (closed frame masks swap).
    if transition.mode == BLINK_PUNCTUATION:
        assert transition.has_closed_eye_frame == true

function ValidateEmotionViewMatrix(rig):
    // Every emotion at every yaw x pitch cell is authored or falls back to neutral.
    for emotion in EMOTIONS:
        for yaw_zone in YAW_ZONES:           // Z0..Z7
            for pitch_band in PITCH_BANDS:   // P_MINUS, P0, P_PLUS
                cell = rig.emotion_matrix[emotion][yaw_zone][pitch_band]
                assert cell.authored or cell.fallback_to_neutral

function ValidateDistinguishability(rig):
    // Referenced from XVII.1 but never previously implemented. Nothing else
    // checks that the 7 held emotions read as distinct from each other,
    // which is the actual functional requirement of an expression set.
    for (a, b) in all_pairs(EMOTIONS):
        differing_fields = 0
        for field in [eye_shape_tags, brow_tags, mouth_tags, head_tilt]:
            if EMOTIONS[a][field] != EMOTIONS[b][field]:
                differing_fields += 1
        assert differing_fields >= 2   // e.g. ANGER vs SERIOUS, SADNESS vs DEFEAT
```

---

## Glossary

**Anchor-critical** — a part whose silhouette carries the character's identity alone (Head, Bangs, Hair, Back Hair, Ears). Never fully hidden. Opposite of **bridge-safe**. (XII.4)

**Aerial perspective** — the depth cue where distant objects go lighter, cooler, lower-contrast. Modeled exponentially as `haze(Z) = 1 − e^(−k·Z)`. The basis of Part XV. (XV.1)

**Ahoge (アホ毛)** — a single (or 2–3) hair sprout(s) from the crown, ~0.15–0.25 head-heights tall, breaking the centerline. Reads as airheaded/clumsy/moe. The canonical controlled asymmetry. (XIII.4, XVI.4)

**Action Unit (AU)** — a standardized facial-muscle movement (Facial Action Coding System). Used in Part XVII to specify emotion feature deltas. (XVII)

**Anger vein (cross-popping vein)** — a cruciform/quatrefoil overlay of four diamond shapes above the temple, bright red, ~0.5–1.0 EW. Signals anger/irritation at moderate+ intensity. (XVII.2)

**Authoring radius (`R`)** — sphere radius in the spherical projection. `R_cranium` for upper-head anchors; `R_jaw` (= 1.5·R) for jaw-domain anchors. (I.6)

**Authority** — which system produces a layer's motion: PARALLAX (continuous slide within a cell) or SWAP_AT_BOUNDARY (discrete cohort replacement). (XI.6)

**Billboard / billboarded** — a flat 2D image kept perpendicular to the camera. Real 2D art is *always* billboarded in this rig. (X.1)

**Bishōjo (美少女)** — "pretty girl"; the anime/manga style category for attractive female characters. The style category this rig targets. (XVI)

**Blush (sekimen)** — the cheek redness overlay: parallel slash lines (romantic embarrassment) or a solid red/pink patch (cute warmth), ~0.5 EW below the eye. (XVII.2)

**Bridge-safe** — a part that may hide in walk-behind states without breaking the read (Eyes, Brows, Mouth, Nose, Teeth, Cheeks). (XII.4)

**Camera proximity (`F_prox`)** — a clamped 1/Z multiplier scaling parallax magnitude, never the source art's stroke. (III.5, XIV.7)

**Cardioidal strain** — the transformation enlarging top-of-head features and shrinking/raising bottom ones. The math that turns a realistic head anime. (XIII.1)

**Cat mouth (neko-guchi)** — a sideways "3" or omega "ω" replacing the mouth; mischief/cuteness/playful smugness. (XVII.2)

**Cohort (swap cohort)** — every layer scheduled to change at a given threshold, swapped together in the same crossfade window. (IV.0)

**Construction Cross** — centerline + browline, bowed per the spherical projection. The invariant against which every feature in every cell is placed. (I.3, XII.1)

**`C_peak` (`Peak`)** — a layer's signed displacement coefficient: +1.50 High-Proj, +1.00 Nose/Bangs, +0.60 Primary, 0.00 Face, −0.50 Base-Anchored Projections, −1.00 Back Hair. (III.1)

**Cutout animation** — 2D animation using flat articulated pieces jointed at pins/pegs. The tradition this rig belongs to. (X)

**Dere archetypes** — the anime personality-trope shorthand expressed visually: tsundere (tsurime, fierce), yandere (flat, calm), kuudere (jitome, aloof), dandere (tareme, shy), deredere (round, bright), himedere/ojou (hime cut, refined). (XVI.6)

**Deformer** — a rig primitive that transforms a piece. Rotation transform = rigid (legal); mesh-warp = bend (illegal under Zero-Morphing). (X.4)

**Hand-moved jitter** — an optional, off-by-default sub-pixel position noise on the parallax offset that reads as "breathing." The hand-moved-cutout quality, formalized in XV.6. (X.6, XV.6)

**Effect symbols (manpu)** — the overlay iconography layered on top of the expression (sweat drop, anger vein, blush, tears, etc.). Separate art pieces with their own Z-depth and fade-in timing. (XVII.2)

**Emotion parameter** — a single value selecting the active emotion and its intensity, analogous to the Master Rotation Parameter. Drives the emotion swap via the same Schmitt/crossfade contract as rotation. (XVII.6)

**Fade-then-hide** — layer leaving art fades opacity to 0 BEFORE the boundary, never holds at alpha 0. (X.2, X.3 cmd 9)

**Fold-don't-squash** — past the profile limb (cos Θ < 0), a feature HIDES (visibility 0), never squashes through zero width. (XIV.6)

**Haze (atmospheric)** — the translucent veil compositing each layer's color toward the haze color, opacity `haze(Z) = 1 − e^(−k·Z)`, scaled by a `mist` parameter. The depth-atmosphere treatment of Part XV. (XV)

**Happiness lines (warai-sen)** — 2–3 short upward-curving lines radiating from the outer eye corner when the eye is closed into a smile-arc. Signal genuine/strong happiness. (XVII.2)

**Hime cut (姫カット)** — straight blunt bangs + straight side locks framing the jaw + long back. Reads as elegant/ojou/refined/traditional. (XVI.4)

**Hysteresis** — system whose output depends on history. Schmitt trigger dead zone (±1.5°) prevents state chatter. (XIV.3)

**Jitome (ジト目)** — half-lidded, flat-top-lid eye shape. Reads as bored/annoyed/kuudere. (XVI.2)

**Baby schema (neoteny)** — the feature set (large eyes/cranium, small nose/mouth, chubby cheeks) that releases the caregiving response. The biological basis of cute. (XIII.3)

**Local Delta Reset** — per-zone rebasing `T(θ) = Peak·[sin θ − sin θ_fired]` zeroing translation at the zone anchor, guaranteeing velocity continuity. (III.6, XIV.5)

**Master Rotation Parameter** — the single `(yaw, pitch)` driving parallax + swaps in lockstep. No separate "which zone" control. (Part 0)

**Mirrored asset** — produced by horizontally flipping the positive-yaw partner. Shortcut for Z1/Z3/Z5; three exception classes must be re-authored. (III.3, XI.7, XII.5)

**Mist parameter (`mist`)** — a shot-level `[0, 1]` multiplier on the atmospheric haze intensity (XV.2). May be static, bound to a view state (the "melt-away"), or bound to camera proximity. Animates the veil's effective opacity per frame. (XV.3)

**Moe (萌え)** — the anime/manga concept of strong affection/cuteness release; tied to neoteny and the feeling of wanting to protect. Expressed through physical traits (large eyes, round face) or personality archetypes. (XIII.1, XVI.6)

**Monoline** — every line at a single uniform width, no tapering. Stroke fixed in screen-space pixels. (I.1)

**Multiplane parallax** — the depth technique of stacking multiple independently-translatable art layers separated by an optical gap; further layers slide slower, opposing slides read as rotation. The principle behind the parallax displacement system. (X.1)

**Atmospheric veil** — a translucent overlay whose opacity ramps with depth, dissolving receding features into a haze color rather than cutting them out. The principle formalized in Part XV. (X.5, XV)

**Owner's Preference sliders** — the 7-dial construction config (eye size, chin sharpness, hair volume, brow thickness, mouth size, neoteny level, neck width) each owner sets to express a specific character. (XVI.7)

**Parameter-Space Crossfade** — crossfade as a function of rotation parameter (±0.75° window), not frame count. Speed-independent. (III.6, XIV.4)

**Pin** — rigid attachment point. Three classes: Positional (flat), Root/Tip/Lag (projecting + secondary motion), Chain (ribbon hair). (II.3)

**Pivot Anchor Uniformity** — every asset in a swap cohort shares the same rigid anchor coordinate (the Reference Cross point), never bounding-box center. (III.6)

**Pose group (swap set)** — a set of parts of which only one is visible at a time; equivalent to this rig's swap cohort. (X.3)

**Residual Correction** — per-corner offset `E = P_art − P_math` nudging formula output onto hand-drawn anchor. (VI)

**Schmitt trigger** — dual-threshold comparator with memory. State-machine primitive preventing swap chatter. (XIV.3)

**Shark teeth** — triangular serrated teeth filling an open mouth (5–10 triangles per row). Signal manic energy, mischief, comedic rage. (XVII.2)

**Sweat drop (ase)** — a single oversized teardrop overlay above the temple, blue/cyan, ~0.5–1.5 EW. Signals embarrassment/exasperation/confusion. (XVII.2)

**Seam extension margin** — 8–12% (scaled by F_prox) extra fill past visible silhouette, preventing gaps during slide. (II.4)

**Shape contrast** — ~4 rounded : 1 sharp form ratio. (XIII.2)

**Silhouette Read Test** — flood-fill flat black; check single connected component, no ambiguous blob. Per-cell. (I.7, XII.6)

**Slot** — a container of which one attachment is visible at a time; the active swap set picks which. (X.3)

**Smoothstep** — easing family S₁/S₂/Sₙ with C¹/C²/C³ continuity. Replaces linear interpolation. (XIV.2)

**Spherical projection** — master formula turning live `(yaw, pitch)` into 2D screen offset. (III.4, XIV.1)

**Sprite impostor** — 2D image (or set per view angle) substituting for a 3D object, chosen by nearest angular cell. The hard-swap ancestor. (X)

**Sub-threshold** — swap point INSIDE a zone (22.5°, 67.5°), not at its boundary. Same mechanism as primary, lower visual impact. (Part 0, IV)

**Swoosh** — faster structural-gap transition (front ↔ back) bypassing normal crossfade. (Runtime)

**Tareme (垂れ目)** — eye shape with the outer corner slanting ~10–15° below the inner. Reads as gentle/shy/deredere/moe. (XVI.2)

**Tsurime (釣り目)** — eye shape with the outer corner slanting ~10–20° above the inner. Reads as tsundere/confident/fierce. (XVI.2)

**Swap cohort** — see Cohort.

**Top View / Bottom View** — the two pitch-endpoint assets (P+90 / P−90). Top = swap-and-stop; Bottom = swap-and-continue-parallaxing. (V.2, V.4)

**Uncanny valley** — the curve where near-human figures evoke revulsion just short of full realism. Empirical basis for no-deformation. (XIII.5)

**ViewCell** — a single cell of the Part XI matrix; carries cohort, visibility, depth, authority. (XI.1)

**Viseme** — mouth shape per phoneme. Needs its own asset at every yaw zone. (VII.2)

**Walk-behind states** — Z5/Z6/Z7 (|yaw| ≥ 135°), bridge-safe features hide, read via silhouette alone. (XII.4)

**Yaw / Pitch** — rotation axes. Yaw: −180..+180 around vertical. Pitch: −90..+90 around horizontal. (Part 0)

**Zero-Morphing Guarantee** — line art never rotated in 3D, skewed, or vertex-morphed. Rotation multiplier applies ONLY to (x,y) pin translation. (III.4, X.3 cmd 1)

**Z-depth stack** — ordered render list (0 = top, 11 = bottom). Permutates per cell. (II.1)