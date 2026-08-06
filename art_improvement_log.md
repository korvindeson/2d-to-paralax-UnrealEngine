# Art Improvement Review Loop & Log

## STRICT WORKFLOW PROTOCOL

**CRITICAL:** The following review loop MUST be executed in exact order. 
1. **Art Update:** Make modifications to feature geometry, paths, or logic.
2. **Mandatory Render Trigger:** ANY update of art MUST Re-run generate_art.py and trigger the `render_svg.py` script to generate fresh PNGs from the updated SVGs. Do not review outdated or raw code without rendered PNGs.
3. **Phase 1 - Feature-by-Feature Review:** STRICTLY review EACH individual face feature in isolation against both the `art_guide` (aesthetic/proportions) and `art_tech_guide` (layering/path constraints/stroke properties). A feature CANNOT be marked complete until all its specific success criteria are met.
4. **Phase 2 - Composed View Review:** SEPARATELY review each supported rendered view (Front, 3Q, Profile, Back, Top, UnderPlane, Mirrors) when the face features are fully assembled. Evaluate exact facial feature placement, integration, and pose-specific rules.
5. **Iteration:** Document every gap, execute fixes, and restart at Step 1 until all evaluations pass.

---

## PHASE 1: INDIVIDUAL FEATURE EVALUATION (PRE-ASSEMBLY)

*Requirement: Each feature must pass both `art_guide` and `art_tech_guide` constraints in isolation before composed assembly evaluation. Each feature's Geometry Spec (shape, placement, size, silhouette) is the ground truth the Success Criteria check against — every value is sourced from `art_guide.md` / `art_tech_guide.md`, never invented here.*

### Feature 1: Head (FaceBase)

**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** Top half = a perfect circle (the Cranium Anchor), radius `R` (I.2), drawn as smooth cubic-Bézier arcs — ZERO sharp corners on the cranium, ZERO straight segments. Chin = a slightly blunted V whose lowest point sits 0.5 cranium radii below the bottom of the cranium circle (I.2). Jaw curve = ONE continuous cubic Bézier per side from `(±R, 0)` to `(0, −1.5R)` with `CP1 = (R, −0.75R)` (initial tangent straight down — pushes the curve outside the cranium's own circular drop) and `CP2 = (0.4R, −1.42R)` (chin tangent ≈25–30° off vertical, which is what reads as a blunted V rather than a rounded cup) (tech I.2 fix). The Head ring is a CLOSED path; the cheek contours and jaw are one unbroken sweep with NO stitched polyline and NO polygonal angles (I.7 Curve Continuity). The ONLY sharp accent on the whole Head silhouette is the blunted chin V-apex (~25–30° off vertical — angular but rounded off, never a needle point). Hairline Arc (construction guide, may be un-inked) is radius-as-a-function-of-elevation `R_hairline(ψ) = R·(0.9 + 0.1·cos ψ)` — meets the jaw origin `(±R, 0)` at the equator with a flat tangent and is 10% inset at the crown (tech I.2 fix). A soft solid-fill cranium-sheen patch (an Order-0 ellipse fill, ~0.2 opacity, light tint, centered `(0.43, 0.15)`, r = `0.12/0.06`) reads the dome as glossy; it is gated to the front-view bbox so it never spills on Top/Bottom poses (tech A.10). On profile cells the Head ring carries an extra merged contour overlay — an OPEN Catmull-Rom cubic-Bézier chain (nose bridge dip → tip → philtrum → upper lip → mouth notch → lower lip → chin) that stands in for the hidden Nose/Mouth/Teeth.
*   **Placement:** Origin `(0, 0)` at the cranium center; equator = horizontal line `y = 0`; centerline = vertical `x = 0` (tech I.2). Cranium top at `+R`, chin apex at `(0, −1.5R)`. Jaw originates at the left/right equator intersections `(±R, 0)` (I.2). `W_face` is *derived* from the jaw Bezier at `y = −0.25R` ⇒ `W_face ≈ 1.964R` (tech I.4) — never hardcoded as an independent constant.
*   **Size:** Cranium : lower-face neoteny ratio — anime default `1 : 1`, range `1 : 0.8` (moe) to `1 : 1.4` (mature) (XVI.1, XVI.7). Head width : height = `3 : 4` anime default (XVI.1). Cranium : chin vertical = `1 : 1.6` to `2 : 1` anime target (XIII.2).
*   **Silhouette:** Silhouette Read Test (I.7): flatten to black, connected-component analysis yields exactly one component with no internal holes above the noise floor (tech I.7). Shape-contrast ~4:1 round:sharp — rounded set = cranium, cheek contours, jaw curve; sharp set = chin V-apex only (XIII.3). Crown curvature must match the front-view cranium circle at radius `R` across every view (V.2 Visual Reference).

*   **Success Criteria (`art_guide`):**
    *   **Cardioidal Strain (XIII.2):** Proportions must strictly adhere to: Eye baseline `y = 0.40–0.46`, Nose baseline `y = 0.62–0.68`, Mouth baseline `y = 0.74–0.82`.
    *   **Proportions (XIII.2):** Eye-to-face width ratio must be exactly 1:3.5 to 1:4. Cranium-to-chin ratio must be 1:1.6 to 2:1. Jaw taper must form a "pointy V".
    *   **Curve Continuity (I.7):** Crown arc, jaw curve, and cheek contours must be ONE continuous sweep with ZERO angular breaks or polygons.
    *   **Shape Contrast (XIII.3):** The overall silhouette must maintain a ~4:1 ratio of rounded forms (cranium) to sharp forms (chin V).
*   **Success Criteria (`art_tech_guide`):** 
    *   Base layer must be correctly assigned. Geometry must consist of closed paths with no overlapping duplicate nodes. Two authoring radii exist (`R_cranium` for `|y| ≤ R`, `R_jaw = 1.5R` for jaw-domain anchors) (tech I.6) — never mix radii across `x`/`y` of one anchor.

### Feature 2: Eye_Near
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** Fundamentally a TRIANGULAR WEDGE + a disconnected lower line + filled circles — the eye reads as a triangle with a curved top edge. The UPPER LASH is a CLOSED geometric wedge (I.6): two SHARP corner tips (the inner canthus and the outer corner) where straight `L` segments meet at a closed angle (endpoints terminate geometrically, NEVER by tapering — I.1), joined across the top by a smooth cubic-Bézier arch (Catmull-Rom, control pushed up `+0.7·W_eye`). The LOWER LASH is a DISCONNECTED open segment — a single quadratic Bézier (or a straight `L` if degenerate) that does NOT connect to the upper lash at the corners; this gap is what prevents the boxed-in look (I.6). Iris = closed shape whose top edge tucks slightly *under* the upper lash (the socketed read); iris and pupil are flat solid-fill concentric shapes, no gradient (I.6). PUPIL = a smaller concentric solid-fill ellipse (iris radius ≈ `0.45·W_eye`, pupil radius ≈ `0.20·W_eye`). HIGHLIGHTS = 1–3 solid light-fill ellipse patches (Order-0, painted first): one larger upper-outer primary (opacity ~0.85) plus one smaller lower-inner secondary bounce (opacity ~0.6) (XIII.5). Local geometry is eye-anchor-relative (tech I.6): upper lash Bezier from `(−0.5·W_eye, 0)` to `(+0.5·W_eye, 0)`. Stroke width is the uniform monoline `W` on every segment — contrast comes from fill area, NEVER stroke weight (tech XIII.4). Personality switch = outer-canthus angle (XVI.2): **Tsurime** (outer up 10–20°), **Tareme** (outer down 10–15°), **Jitome** (half-lidded flat), **Round** — locked once at construction and reproduced across every yaw-zone variant.
*   **Placement:** Eye baseline `y = −0.25R` (tech I.4 corrected; = `y ≈ 0.50` of head height, XVI.1). Eye center sits at `1.0·W_eye` from the centerline (midpoint of inner canthus `0.5·W_eye` and outer corner `1.5·W_eye`, tech I.6). 5-part grid front view: `| 0.5 margin | EYE | 0.8 gap | EYE | 0.5 margin |` (I.4, XVI.1). Spherical anchor (anime-default divisor 4): `theta_0_eye ≈ ±29.4°`, `phi_0_eye ≈ −14.5°` (tech I.6).
*   **Size:** Eye width : face width = `1 : 4` anime default (range `1 : 3.5` moe to `1 : 6` mature) (XIII.2, XVI.1). `W_eye ≈ W_face / 4` (tech I.4). Eye height = 70–80% of eye width (I.6); aspect `2 : 1` anime default (XVI.1). Iris : eye opening ≈ 70–85% (default 0.78; moe up to 0.95) (XVI.2). Pupil : iris diameter ≈ 1/3 to 1/2 (XVI.2). Upper-lash fill area > lower-lash fill area (≈2.5× ratio); iris outline has no wedge fill (XIII.5).
*   **Silhouette:** Bridge-safe feature (XII.4) — hides in walk-behind Z5/Z6/Z7 and at P+ Top. Five-variant yaw set (Eye_Front, Eye_Far_Narrow, Eye_Near_3Q, Eye_Far_Sliver, Eye_Profile) — the most-authored variant set in the matrix (XIII.6 rule 5). Foreshortening is *baked into the asset*, never computed per-frame (XII.3, XIV.6 "fold, don't squash"). Past the profile limb the far member hides at visibility 0% — never squashes through zero (XIV.6).

*   **Success Criteria (`art_guide`):**
    *   **Scale (XVI.2):** Width must be 1:3.5 to 1:4 of the face width. 
    *   **Iris Fill (XVI.2):** Iris MUST occupy 70-85% of the total eye opening area.
    *   **Construction (I.6):** Must feature a closed geometric wedge for the upper lash, a disconnected line for the lower lash, and a solid-fill iris tucked cleanly under the upper lash.
    *   **Highlights (XIII.5):** Must contain 1-3 highlights, specifically requiring an upper-outer primary highlight and a lower-inner secondary bounce highlight.
    *   **Canthus Lock (XVI.2):** The chosen outer-canthus angle (tsurime/tareme/jitome) MUST be reproduced at its own foreshortened angle in every yaw-zone variant — drift reads as a different character mid-turn.
*   **Success Criteria (`art_tech_guide`):** 
    *   Group structure must cleanly separate pupil, iris, and sclera. Correct z-index sorting over FaceBase. Stroke width of upper lash = lower lash = iris outline = `W` (the single monoline width); contrast comes from fill area, never stroke weight (tech XIII.4).

### Feature 3: Eye_Far
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape / Size / Silhouette:** Identical construction contract to Eye_Near (above) — the SAME triangular upper-lash wedge + disconnected lower lash + solid-fill iris/pupil/highlights. The ONLY difference is per-view foreshortening, and that foreshortening is BAKED INTO each authored card, never a per-frame squeeze. The far eye's own authored azimuth `theta_0 ≈ −23.1°` means its *true* total azimuth at +45° yaw is `Theta = −23.1° + 45° = 21.9°`, NOT 45° — compression factor `cos(21.9°) ≈ 0.928`, not `cos(45°) ≈ 0.707` (tech I.5 fix). Every per-view Eye_Far variant is independently hand-authored (Eye_Far_Narrow at Y22, Eye_Far_Sliver at Y67, Eye_Far_Fold at Y90+), never a squeeze of Eye_Far_Front. The sharp canthus tips and the smooth upper arch are preserved (not squashed) across every variant — "fold, don't squash" (XIV.6).
*   **Placement:** Mirrors Eye_Near about the centerline at front view; the spherical-projection sign of `delta_x` flips under the mirrored-yaw transform (III.3) for the left-half states.

*   **Success Criteria (`art_guide`):**
    *   Must meet all scale, construction, and highlight criteria of Eye_Near.
*   **Success Criteria (`art_tech_guide`):**
    *   Horizontal scaling and transform origins must perfectly mirror Eye_Near logic prior to composed foreshortening. The eye's `theta_0` is asymmetric to begin with — never substitute the raw yaw value for `Theta` in any per-view placement (tech I.5).

### Feature 4: HairFront (Bangs)
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** Ribbon Theory (I.6) — a jagged CLOSED polygon of overlapping ribbon wedges in a strict S-curve rhythm (27 front-glyph points). Two line types alternate: the hair-tip V-terminations are SHARP (auto-detected at a 45° threshold — each tapered tip is two uniform strokes meeting at a closed angle, NEVER a taper, per I.1), while the masses BETWEEN tips are smooth cubic-Bézier S-curves (Catmull-Rom). The crown carries a jagged zig-zag of paired valley/spike wedges. REQUIRED asymmetric element: a single **ahoge** (cowlick) — a 3-point SHARP upward spike breaking the centerline mirror (no `(1−x, y)` twin, tech A.8) — `1` (or `2–3`) sprouts from the crown, `~0.15–0.25` of head-height tall (XVI.4). The bangs *hem* (bottom edge) is a near-smooth line sitting above the brows (must clear `minY(BrowL)`). An inner ribbon boundary = an OPEN, thinner (×0.6) smooth decorative curve whose vertices are offset 12% toward the centroid — it never dashes (WrapCov = −1). A soft solid-fill crown-gloss ellipse patch (Order-0, ~0.3 opacity, light tint, centered just below the crown so it never spills upward — tech A.10) reads the fringe as glossy. The inner boundary sits 10–15% *outside* the cranium circle (`R_ribbon_inner ≈ 1.125R` midpoint default) before sweeping outward to the tip (tech I.6). Ribbon roots originate from the Hairline Arc (I.2), never from an arbitrary cranium-silhouette point. Bang types (XVI.4): Straight across / Center-parted / Side-swept / Hime cut / Baby bangs — pick one per personality.
*   **Placement:** Roots attach along `R_hairline(ψ)` parametrized by azimuth (tech I.6). Ahoge off-center, breaking the centerline mirror (XIII.4). Baseline Z-depth position 2 (over Hair Shadows, under Extended Projections only — II.1).
*   **Size:** Hair volume multiplier (head+hair ÷ head) — anime default `1.15` (range `1.05` mature to `1.25` moe) (XVI.7). Hair makes the head `~1.10–1.25×` taller (the "hair helmet") (XVI.4). Side locks `~1` face-length, width `0.25–0.35` of face width each (XVI.4).
*   **Silhouette:** Anchor-critical (XII.4) — never fully hidden. Identity layer — on back/back-3/4 views carries the whole read (XVI.4). Drawn as the **annulus** outer mass + face-shaped cutout together with HairBack (XVI.4). The ahoge must be preserved (and identically classified) across every cell that touches it — re-symmetrizing it on the 3/4 card while it stays asymmetric on front is the classic pop defect (I.7 Cross-Zone Asymmetry Continuity, XIII.4).

*   **Success Criteria (`art_guide`):**
    *   **Ribbon Theory (I.6):** Must define a clear outer mass and an inner face-cutout boundary sitting outside the cranium.
    *   **Asymmetry (XIII.4):** Must include an ahoge breaking perfect symmetry. Exactly one or two asymmetry cues per face — enough to read alive, not deformed.
    *   **Flow (I.7):** Inner boundary scalloping must use smooth, continuous curves with varied scallop sizes for visual rhythm.
*   **Success Criteria (`art_tech_guide`):**
    *   Must sit at the top-level z-index (over eyes). Crown highlight must be exactly 0.3 opacity. The silhouette Read Test (tech I.7) must pass per-cell independently, not only at front view — a clean front-view silhouette can still collapse into an ambiguous blob at profile if the near and far ribbons overlap into one shape with no separating gap.

### Feature 5: Hair (Side / Main Mass — the Annulus)
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** The hair ANNULUS — a single CLOSED polygon (48 front-glyph points) that traces the OUTER mass boundary first, then the INNER face-cutout boundary back, so the face shows through the hole (a ring, not a blob) (I.6, XVI.4). Two line types: the outer-boundary hair-tip V-terminations are SHARP (45° threshold — closed-angle tips, no tapering), and the rest of the outer + inner boundaries are smooth cubic-Bézier S-curves (Catmull-Rom rhythm). Same ribbon construction as Bangs: an inner decorative ribbon boundary (OPEN thinner ×0.6 curve, vertices offset 12% toward the centroid, never dashes — WrapCov = −1) and a soft solid-fill crown-gloss ellipse patch (Order-0, ~0.3 opacity, light tint, centered just below the crown so it never spills upward — tech A.10). Ribbon roots originate from the Hairline Arc (I.2), never an arbitrary cranium-silhouette point.
*   **Placement:** Outer boundary sits strictly OUTSIDE the FaceBase/cranium silhouette; the inner face-cutout hugs the face shape. Baseline Z-depth positions 5 (Side Hair near) and 8 (Side Hair far) — the near/far halves reorder across yaw (II.1, II.2). The mid hair band moves WITH yaw at its own depth rate.
*   **Size:** Volume multiplier `1.10–1.25×` head (the "hair helmet", XVI.4, XVI.7). Hair-length categories (XVI.4): Short `<1.0`, Bob `1.0–1.5`, Medium `1.5–2.5`, Long `2.5–4.0+` head-heights; Twin-tail / Ponytail tail mass `~1–2` head-heights. Side locks `~1` face-length, width `0.25–0.35` of face width each (XVI.4).
*   **Silhouette:** Anchor-critical (XII.4) — never fully hidden. Identity layer: on back / back-3/4 views it carries the whole read alongside BackHair. The Silhouette Read Test (I.7) must pass per-cell — the outer mass must stay one connected component with no ambiguous blob where near/far ribbons overlap.

*   **Success Criteria (`art_guide`):**
    *   Must define a clear OUTER mass AND an INNER face-cutout (the annulus read), with ribbon S-curve rhythm and at least one preserved asymmetry (I.6, XIII.4).
*   **Success Criteria (`art_tech_guide`):**
    *   Must render as one closed polygon whose even-odd interior is the face hole (the mask-read yields the face cutout, not a solid blob). Crown gloss contained inside the ring bbox (A.10). Depth class `Back`; the near/far halves reorder on the same Schmitt state flag as their asset swap (tech II.2).

### Feature 6: BackHair (Back Drape / Backdrop Mass)
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** A simple CLOSED polygon (8 front-glyph points) — the lower back-of-neck / shoulder drape, a wide trapezoid-ish mass ("the solid back mass"). Smooth cubic-Bézier contour with auto-detected sharp corners at the default 40° threshold; a few hair-tip V-terminations may stay sharp where the drape ends. UNLIKE Bangs/Hair it gets NO ribbon inner-boundary and NO crown gloss — it is the backdrop drape, not a front-facing ribbon mass.
*   **Placement:** Strictly behind the FaceBase layer. Baseline Z-depth position 11 (bottom of stack) at front view (II.1). At Z7 (back view, 180° hard swap) BackHair **promotes to stack position 1**; FaceBase demotes to back (II.2 reorder).
*   **Size:** Same volume multiplier as the rest of the hair (`1.10–1.25×` head). Hair-length categories (XVI.4): Short `<1.0`, Bob `1.0–1.5`, Medium `1.5–2.5`, Long `2.5–4.0+` head-heights.
*   **Silhouette:** Anchor-critical (XII.4). Displacement Peak `−100%` (III.1) — slides *opposite* to Nose/Bangs to fake rotation (multiplane rule X.1). At Z7 the BackHair/backdrop is the **stationary layer** — it never slides ("backdrop never slides", commandment 4 / X.1).

*   **Success Criteria (`art_guide`):**
    *   Must provide a cohesive outer hair mass behind the head (I.6), acting as the outermost silhouette carrier / backdrop.
*   **Success Criteria (`art_tech_guide`):**
    *   Must sit strictly behind the FaceBase layer with no bleeding edges. NO ribbon/gloss decoration (it is a backdrop, not a ribbon mass). Animated Z-order (commandment 13): the Z7 reorder must be driven by the same Schmitt state flag that fires the 180° asset swap, never by a re-derived raw-angle test (tech II.2 fix).

### Feature 7: Mouth
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** NOT a closed outline — TWO OPEN curves with a dead-center gap, no corner dots (I.6). The UPPER LIP is an open Catmull-Rom cubic-Bézier chain and the LOWER LIP is a SEPARATE open Catmull-Rom chain; the two do NOT meet at the center — a small fixed gap at `x = 0` (`gap_width ≈ 0.1·W_eye`) produces the dead-center split rather than a single notched curve (tech I.6). ZERO sharp corners — the mouth is smooth throughout (a rounded form in the ~4:1 contrast ratio). A decorative center tick (a tiny quadratic Bézier) tucks under the dead-center gap (XVI.5 lower-lip tick). For the open visemes (A, U) a dark solid-fill interior is painted BEHIND the lips as a closed ring fill (Order-0, dark `#16181d`). Smile corners must not exceed the inner edge of the eyes (XVI.5). Open mouth = a small rounded shape (the "cat mouth" ω or 3) (XVI.5). Viseme set: Closed, Wide-Open (A), Narrow-Wide (I), Rounded-Small (U), Neutral-Rest (VII.2).
*   **Placement:** Mouth baseline `y = −1.28R` (tech I.4, midpoint of the 80–85% band); range `−1.25R` (80%) to `−1.3125R` (85%). Dead-center on the centerline at `(0, y_mouth)` (tech I.6). Spherical anchor (R_jaw domain): `theta_0 = 0`, `phi_0 ≈ −58.6°` (tech I.6).
*   **Size:** Default width `0.3–0.5` of one eye-width (XVI.5); 50–70% size reduction from the realistic full-lip width (XIII.2). *(Note: I.6 gives a default width of one full eye-width — XVI.5's narrower anime-default range supersedes it for this style.)*
*   **Silhouette:** Bridge-safe (XII.4) — hides in walk-behind Z5/Z6/Z7 and at P+ Top. At 3/4 swaps to `Mouth_3Q` (a compressed, off-center curve — Zone 2). At profile either fully merged into the contour line or dropped to 0% visibility (Zone 4). Each viseme needs its own asset at every yaw zone — mouth shape is a function of both phoneme and head angle (VII.2).

*   **Success Criteria (`art_guide`):**
    *   **Shape (I.6, XVI.5):** Must be a shallow curve with a dead-center gap, zero corner dots, and a default width of 0.3 to 0.5 of one eye-width.
    *   **Visemes (VII.2):** Must cleanly distinct states for Closed, A, I, U, and Neutral-Rest.
*   **Success Criteria (`art_tech_guide`):**
    *   Vector stroke width must remain perfectly consistent across all viseme state swaps. Teeth are a conditional sub-shape of the Mouth asset (only for visemes A and I) — they inherit the Mouth anchor and have no coordinate of their own (tech I.6, VII.2).

### Feature 8: Nose
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** Basically a tiny TRIANGLE (or a dash) — and it is the SHARPEST accent in the whole shape-contrast ratio. EVERY vertex is forced sharp, so EVERY segment is a straight `L` — there are NO curves in the nose at all (tech §10 routes Nose with `Sharp.assign(N, 1)`, fully angular). Max 3 nodes for the triangle, 2 for the dash. At profile: a small triangular wedge protruding from forehead to lip (XVI.3, XVI.5) — at profile the nose tip is the absolute leading edge of the silhouette (max X bounding box). Frequently omitted entirely in front view (XVI.5).
*   **Placement:** Sits on the **Center-Face Coordinate** (I.5) — the intersection of the centerline and the browline; the centerline segment between browline and nose dash *is* the bridge line. Nose baseline `y = −1.00R` (tech I.4 corrected; = `y ≈ 0.65–0.70` of head height, XVI.1). Spherical anchor (R_jaw domain): `theta_0 = 0`, `phi_0 ≈ −41.8°` (tech I.6). At profile the nose tip sits at `x ≈ 1.12R` (derived) — `1.15R` is an acceptable small artistic exaggeration (tech I.5).
*   **Size:** `0.05–0.10` of one eye-width (XVI.5); art_tech I.6 illustrative default `width ≈ 0.08·W_eye`, `height ≈ 0.12·W_eye`. Profile protrusion `~0.05–0.08` of head-width (XVI.5). 80–95% size reduction from realistic (XIII.2).
*   **Silhouette:** Bridge-safe (XII.4). At profile (Zone 4) the nose either fully merges into the silhouette contour line or drops to 0% (IV). Frequently omitted entirely in front view (XVI.5).

*   **Success Criteria (`art_guide`):**
    *   **Minimalism (I.6, XVI.5):** MUST be a microscopic geometric indicator (either a tiny triangle or a dash). Size must not exceed 0.05 to 0.10 of one eye-width.
*   **Success Criteria (`art_tech_guide`):**
    *   Path must be absolutely minimal (maximum 3 nodes for triangle, 2 for dash). At profile the nose tip is the absolute leading edge of the silhouette (max X bounding box value), but the indicator still obeys the locked stroke width `W`.

### Feature 9: Teeth
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** A simple two-line upper/lower ridge — a small CLOSED shape (5 points) at the locked stroke width `W` (I.6). Sharp corners auto-detected at a 50° threshold (the ridge ends stay angular); smooth cubic-Bézier on any rounded runs. Straight `L` segments form the two ridge lines. Stroke only, no fill.
*   **Placement:** Contained entirely inside the mouth shape layer mask (I.6, VII.1) — the mouth's solid sclera-like fill acts as the alpha matte. Inherits the Mouth anchor — no coordinate of its own (tech I.6).
*   **Size:** Bounded by the mouth shape's interior.
*   **Silhouette:** Conditional sub-shape of the Mouth — only drawn for the open-mouth visemes **A** and **I** (I.6, VII.2); not drawn for Closed, U, or Neutral-Rest. The "shark teeth" manpu (XVII.2) is a separate stylized emotion-effect asset and does NOT share this geometry (I.6).

*   **Success Criteria (`art_guide` & `art_tech_guide`):**
    *   Must be a simple two-line upper/lower ridge contained entirely inside the mouth shape layer mask (I.6), drawn only for visemes A and I.

### Feature 10: Chin
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** A slightly blunted V (I.2) — part of the FaceBase silhouette, not a separate layer. The jaw Bezier's `CP2 = (0.4R, −1.42R)` produces the chin tangent at ~25–30° off vertical: a straight `L` segment at the apex (the ONE sharp accent on the Head silhouette) with cubic-Bézier curves on the rounded portions either side — legible V-angle with the sharp point rounded off, never a needle (tech I.2 fix). The chin V-apex is one of the sharp set in the ~4:1 round:sharp ratio (XIII.3). Variants (XVI.6): Sharp/pointed V (mature, elegant) vs Round/soft (young, cute).
*   **Placement:** Chin apex at `(0, −1.5R)` (tech I.2). Spherical anchor (R_jaw pole): `theta_0 = 0`, `phi_0 = −90°` exactly (tech I.6). Always on the centerline — the Chin Apex anchor registration never moves off-center (XII.2).
*   **Size:** Lowest point sits `0.5` cranium radii below the bottom of the cranium circle (I.2).
*   **Silhouette:** Part of the FaceBase silhouette (not a separate layer). The chin V-apex is a sharp accent in the ~4:1 round:sharp ratio (XIII.3). Visible in zones Z0–Z4 and Z6; hidden at Z7 by the featureless sphere (XII.2).

*   **Success Criteria (`art_guide`):**
    *   Must form a slightly blunted V shape exactly 0.5 cranium radii below the cranium bottom (I.2).

### Feature 11: Brow_Near & Feature 12: Brow_Far
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** A single uniform THIN stroke — one CLOSED ring drawn as a gentle arch (round caps, no fill). Sharp corners auto-detected at a 50° threshold: the two brow tips stay slightly angular, the mid-section is a smooth cubic-Bézier (Catmull-Rom) arch. Constructed as one Bezier from `(x_eye_inner, y_brow)` to `(x_eye_outer, y_brow)` with control point offset upward `≈0.15·W_eye` for arch (`0` for a straight variant) (tech I.6). Round caps. Optional expression set: Neutral, Raised, Furrowed (VII.4).
*   **Placement:** One full eye-height above the eye's upper lash (I.6). `y_brow = y_eye_baseline + 2·H_eye` where `H_eye = 0.75·W_eye` (midpoint of 70–80% range, tech I.6). Gap `0.02–0.04` head-height above the upper lash (XVI.5). Spherical anchor (R_cranium): `theta_0 ≈ theta_0_eye`, `phi_0_brow = arcsin(y_brow / R_cranium)` (tech I.6). Gentle peak over the outer third of the eye (XVI.5). Tilt-asymmetry between L and R brows (I.6): vary the control-point offset by a small fixed delta between sides (e.g. left `0.15·W_eye`, right `0.12·W_eye`) rather than mirroring identically (tech I.6).
*   **Size:** Thin — single tapered stroke, `1–2`px (moe = hair-thin) (XVI.5). Brow thickness slider (XVI.7): medium / thin / hair-thin / medium-thick.
*   **Silhouette:** Bridge-safe (XII.4). Brow_Far at 3/4 swaps to `Brow_Far_3Q` — shortened to match the compressed browline (Zone 2, IV). Brow-tilt difference is one of the canonical controlled-asymmetry cues (XIII.4: "brow_tilt_difference").

*   **Success Criteria (`art_guide` & `art_tech_guide`):**
    *   Must be a single, uniform, gently arched, thin stroke (I.6, XVI.5). Strokes must use round caps. Tilt asymmetry between the two brows is required (a few degrees of difference reads alive; true mirror is reserved for a deliberately blank expression).

### Feature 13: Cheek_Near & Feature 14: Cheek_Far
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** The cheek contour is the rounded portion of the FaceBase jaw Bézier — ONE continuous cubic-Bézier sweep (8 points), ZERO straight lines, ZERO polygonal angles (I.7 Curve Continuity). A CLOSED contour. Auto-detected sharp threshold is 40°, but a well-formed cheek has NO sharp corners at all — it is a pure rounded form in the ~4:1 round:sharp ratio (XIII.3 lists cheek contours in the rounded set). Continuous with the jaw curve, never a separate stitched polyline.
*   **Placement:** Runs from the cranium equator `(±R, 0)` down to the chin apex `(0, −1.5R)` via the jaw Bezier (tech I.2).
*   **Size:** Baby-schema cheek chub rule (XIII.3): `cheek_gap ≥ nose_mouth_cluster_width`.
*   **Silhouette:** Rounded form in the ~4:1 round:sharp ratio (XIII.3 — cheek contours are listed in the rounded set). Continuous with the jaw curve — never a separate stitched polyline (I.7 Curve Continuity).

*   **Success Criteria (`art_guide` & `art_tech_guide`):**
    *   **Continuity (I.7):** Silhouette must be ONE continuous Bezier sweep. Zero straight lines, zero polygonal angles.

### Feature 15: Ear_Near & Feature 16: Ear_Far
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** A CLOSED monoline shape (6 points) — a rounded outer cubic-Bézier curve tucked between the Hairline Arc and the jaw origin, with a SINGLE interior fold-line; no separate lobe unless the character design calls for it (I.6). Sharp corners auto-detected at the 40° default — the ear TIP is allowed sharp (it is one of the sharp accents in the ~4:1 ratio, XIII.3), but the outer curve stays smooth. Stroke only, no fill.
*   **Placement:** Top edge level with the browline; bottom edge level with the base of the nose indicator (I.6, XII.2 "Ear Tops" anchor). Spherical anchor (R_cranium): `theta_0 ≈ theta_0_brow + 0.15R`; `phi_0` spans `phi_0_brow` (top) down to `phi_0_nose` (bottom) (tech I.6). Author an explicit Parietal-adjacent coordinate for the ear root (I.5) rather than eyeballing per view. At profile (Zone 4) the near ear sits at the vertical and horizontal center `(X=0.5, Y=0.5)` of the visible cranium hemisphere. The neck meets the skull behind the ear at profile (XVI.3).
*   **Size:** Spans the full browline-to-nose-baseline distance (I.6).
*   **Silhouette:** **Anchor-critical** (XII.4) — ears must never drop to 0% visibility on any cell where hair doesn't fully occlude them. Past profile they fold to a flat back-fuzz plane (Z5+) but visibility stays > 0. The ear tip is a sharp accent in the ~4:1 round:sharp ratio (XIII.3). Displacement Peak `−50%` (III.1 — Base-Anchored Projection), moving opposite the high-projection layer at roughly a third of its magnitude.

*   **Success Criteria (`art_guide`):**
    *   Must be a rounded outer curve tucked cleanly between the Hairline Arc and jaw origin, featuring a single interior fold-line (I.6).
*   **Success Criteria (`art_tech_guide`):**
    *   Anchor-critical (XII.4): `cell.visibility[Ear] > 0.0` for every zone; a swap that fully hides an ear is always a defect.

### Feature 17: Neck
**Geometry Spec (shape, placement, size, silhouette):**
*   **Shape:** Its OWN CLOSED shape (4 points) — the Neck Outline, independent of the jaw curve so II.2 can reorder it independently (tech I.6). Two cubic-Bézier curves dropping from the jaw origin points, with a slight widen toward the shoulders (I.6); sharp corners auto-detected at the 40° default (the shoulder drops may be slightly angular, the side curves smooth). A separate Neck Patch (skin fill behind the jaw) sits at Z-depth 9; the Outline at Z-depth 10. The Outline is stroke-only; the Patch is a flat solid fill. At back view narrows as it rises into the skull; at profile meets the skull behind the ear (I.6, XVI.3).
*   **Placement:** Drops from the jaw origin points `(±R, 0)` (tech I.6). Bottom-view insertion point `≈ (0, −1.6R)` — slightly past the chin apex, giving headroom for the seam margin to stretch toward `−90°` (tech I.5 fix). Baseline Z-depth: Neck Patch (skin fill behind jaw) position 9, Neck Outline position 10 (II.1).
*   **Size:** Width `≈ 0.4` of head width at rest (anime default, XVI.7; range `0.3` moe to `0.5` mature).
*   **Silhouette:** Bridge-safe (implicit — not in the anchor-critical set, XII.4). At Bottom View (V.4) the Neck Patch layer expands to carry the jaw underside — the most-stretched layer at `−90°`, doing the most seam-margin work of any layer in the rig (V.4 Visual Reference).

*   **Success Criteria (`art_guide`):**
    *   Must consist of two curves dropping from jaw origin points. Width must be ≈ 0.4 of head width (I.6, XVI.7).
*   **Success Criteria (`art_tech_guide`):**
    *   Drawn as its own closed shape (Neck Outline) independent of the Jaw Curve so it can be reordered in the Z-stack (tech I.6, II.2). At the under-plane hard swap (V.4), the Neck Patch expands with proximity-scaled seam margin (II.4 + III.5 floor) to survive the stretch to `−90°` without opening a gap.

## PHASE 2: COMPOSED VIEWS — DETAILED PLACEMENT EVALUATION (POST-ASSEMBLY)

*Requirement: Review EACH assembled view. Strict adherence to exact XYZ coordinate mapping and feature compression logic is mandatory.*

### 1. View_Front (Y00 P00)
*   **Placement Success Criteria:**
    *   **Eyes:** Anchored perfectly horizontally on the eye baseline (Y=0.40-0.46). Inter-ocular gap must be EXACTLY 0.8 eye-widths. Eyes equidistant from face center.
    *   **Brows:** One full eye-height above each upper lash (`y_brow = y_eye + 2·H_eye`, I.6). Single uniform gently-arched strokes with round caps. REQUIRED tilt-asymmetry between L/R brows — a few degrees of difference, NOT a true mirror (XIII.4 "brow_tilt_difference"; reserve mirror for a deliberately blank expression).
    *   **Nose:** Anchored at dead center X, vertically at Y=0.62-0.68. Microscopic indicator (0.05-0.10 eye-width); frequently omitted entirely in front view (XVI.5).
    *   **Mouth:** Anchored at dead center X, vertically at Y=0.74-0.82. Shallow curve with a dead-center gap, width 0.3-0.5 eye-width, no corner dots.
    *   **Teeth:** Drawn ONLY for the open-mouth visemes A and I (VII.2) — a two-line ridge inside the mouth mask; absent for Closed/U/Neutral.
    *   **Ears:** Vertically spanning the space between the eye baseline (top edge at browline) and the nose baseline (bottom edge at nose base) (I.6, XII.2). Both visible, symmetric about the centerline, tucked between the Hairline Arc and the jaw origin.
    *   **Bangs (HairFront):** Ribbon-theory outer mass + inner face-cutout sitting 10-15% OUTSIDE the cranium circle (I.6). REQUIRED ahoge breaking the centerline mirror (XIII.4). Inner-boundary scallops use varied sizes for rhythm (I.7).
    *   **HairBack / BackHair:** Cohesive outer hair mass strictly BEHIND FaceBase (baseline Z-depth 11, bottom of stack — II.1). Volume multiplier 1.10-1.25× head (XVI.4).
    *   **Cheeks:** Both cheek contours continuous Bezier sweeps from the cranium equator `(±R, 0)` down to the chin apex — zero straight lines, zero polygonal angles (I.7 Curve Continuity).
    *   **Chin:** Slightly blunted V at `(0, −1.5R)`, exactly 0.5 cranium radii below the cranium bottom, on the centerline (I.2).
    *   **Neck:** Two curves dropping from the jaw origin points `(±R, 0)`, width ≈ 0.4 head width; drawn as its own closed shape behind the jaw (Z-depth 9/10, I.6).
*   **Integration Success Criteria:**
    *   Must read as highly neotenous (XIII.1). Vertical spacing (Gap Rhythm I.7) must feel intentional, driven by the massive cranium and microscopic lower facial features.
    *   **Gap Rhythm (I.7):** the eye gap (0.8 eye-widths), the brow-to-eye gap (one eye-height), and the nose-to-mouth gap must all read as ONE consistent unit of measure repeating down the face, not three unrelated numbers (deviation ≤ ~15% from their mean).
    *   **Shape Contrast (XIII.3):** ~4:1 rounded (cranium, cheeks, irises, ear curves, hair-mass outer boundary, jaw curve) to sharp (chin V, nose tip, ear tips, hair-tip V-terminations, brow points).
    *   **Z-Depth Stack (II.1):** ordering must read front-to-back as Bangs → Hair Shadows → Eyes/Brows/Mouth/Nose (Primary Features) → Side Hair (near) → FaceBase → Ears → Side Hair (far) → Neck Patch → Neck Outline → BackHair, with no layer bleeding through the wrong neighbor.
    *   **Eye Masking (VII.1):** any hair sweeping over the eyes is clipped by the sclera/iris solid fill (a clean alpha matte) — no destructive edge erasure, no overlapping stray lines.
    *   **Five Anchor Registrations (XII.2):** pupil centers, nose tip, mouth center, chin apex, and ear tops all sit on the front Reference Cross (straight centerline + straight browline) within residual tolerance.
    *   **Deliberate asymmetry (XIII.4):** exactly one or two controlled cues (ahoge + brow tilt) — enough to read alive, not deformed; true bilateral mirror reserved for cranium/jaw hard geometry only.

### 2. View_Narrow (Y22 P00)
*   **Placement Success Criteria:**
    *   **Eyes:** Near eye shifts slightly inward toward X-center. Far eye swaps to `Eye_Far_Narrow` (a genuine pre-foreshortened asset, NOT a squeeze of the front art — XIV.6), compressing to ~85% width and moving closer to the far edge contour. Compression uses `cos(Theta)` at the far eye's own `theta_0 ≈ −23.1°` (`Theta ≈ 21.9°`, not the raw 22.5° — tech I.5).
    *   **Brows:** Far brow compresses to match the narrowed far eye's browline (paired with the `Eye_Far_Narrow` swap). Near brow unchanged. Tilt-asymmetry between L/R preserved across the swap (no accidental re-symmetrization — XIII.4 cross-zone continuity).
    *   **Nose:** Shifts laterally toward the far side but MUST remain entirely within the cheek silhouette contour.
    *   **Mouth:** Shifts laterally; the far-side half of the mouth curve undergoes slight foreshortening. Dead-center gap stays dead-center on the bowed centerline.
    *   **Teeth:** If the active viseme is A or I, the two-line ridge follows the shifted/foreshortened mouth anchor (VII.2).
    *   **Ears:** Near ear shifts backward along the cranium sphere. Far ear becomes partially occluded by the far cheek/jaw contour (still visibility > 0 — anchor-critical, XII.4).
    *   **Bangs (HairFront):** Subtle lateral shift with the turn; the ahoge stays asymmetric and is NOT re-symmetrized (XIII.4 Cross-Zone Asymmetry Continuity).
    *   **HairBack / BackHair:** Slides opposite the near-side features per Displacement Peak `−100%` (III.1) — the multiplane cue begins here.
    *   **Cheeks:** Far cheek recedes very slightly (cosine foreshortening); near cheek unchanged.
    *   **Chin:** On the bowed centerline (Reference Cross I.3), no lateral drift.
    *   **Neck:** Subtle shift with the cranium; remains dropping from the jaw origins.
*   **Integration Success Criteria:**
    *   Must read as a subtle, natural head turn seamlessly blending Front and 3Q rules. No profile ghosting.
    *   **Reference Cross continuity (XII.1):** the centerline begins to bow and the browline begins to compress per the spherical formula (III.4); every feature (eyes, brows, nose, mouth, ears, chin) follows the SAME bowed cross, so no single feature drifts relative to the others.
    *   **Z-Depth Stack (II.1):** ordering unchanged from Front — only slide magnitudes change (Bangs/Nose slide most, FaceBase anchors, BackHair slides opposite). No layer reorders inside Zone 1.
    *   **Read contract (XII.4):** bridge-safe features (eyes, brows, nose, mouth, teeth, cheeks) all stay > 0 visibility; anchor-critical ears/hair stay > 0. The far eye narrows via a genuine `Eye_Far_Narrow` swap, never a per-frame squeeze.
    *   **Sub-threshold cohort (IV.0):** the `Eye_Far_Narrow` swap at 22.5° fires under Schmitt hysteresis (±1.5°) and its Local Delta Reset zeroes the incoming asset's offset — no spatial pop at the trigger.
    *   **Gap Rhythm (I.7):** the brow-to-eye and nose-to-mouth gaps compress with the turn but stay proportional to the (now slightly compressed) eye gap.

### 3. View_3Q (Y45 P00)
*   **Placement Success Criteria:**
    *   **Eyes:** Far eye applies rigorous Cosine Foreshortening (XIV.6), compressing significantly to `Eye_Far_3Q`. It anchors tightly against the far cheek contour without breaking the boundary. Near eye remains on its front-ish asset (near-side compresses only ~7-20%, I.4).
    *   **Brows:** Far brow swaps to `Brow_Far_3Q` — shortened to match the compressed browline (Zone 2). Near brow unchanged. Tilt-asymmetry preserved (XIII.4).
    *   **Nose:** Shifts to the far side. Tip of the nose contour must either gently touch or slightly break the far cheek line, but not protrude excessively.
    *   **Mouth:** Swaps to `Mouth_3Q` — a compressed, off-center curve anchored to the shifted nose centerline; the far half of the curve is heavily foreshortened.
    *   **Teeth:** If the active viseme is A or I, the ridge follows the `Mouth_3Q` anchor (mouth shape is a function of both phoneme AND head angle — VII.2).
    *   **Ears:** Near ear shifts rearward, approaching the visual back-edge of the cranium (X ≈ 0.8). Far ear is entirely hidden.
    *   **Bangs (HairFront):** Turns to show the side plane; near-side ribbons slide toward the viewer-facing edge. Ahoge preserved asymmetric (XIII.4).
    *   **HairBack / BackHair:** Slides opposite the near-side features (Peak `−100%`, III.1) — the opposing-slide rotational cue is strongest here.
    *   **Cheeks:** Far cheek now carries the indented eye socket + cheekbone contour (from `Face_Base_3Q`) as real geometry, not implied by a sliding eye (Zone 2 Visual Reference).
    *   **Chin:** On the bowed centerline.
    *   **Neck:** Begins meeting the skull behind the ear (XVI.3 profile relationship emerging).
*   **Integration Success Criteria:**
    *   **No Ghosting (XI.11):** Profile construction lines (nose bridge/lips from FaceBase) MUST NOT render as a visible "second face" overlay. Overlapping layer hierarchy must be clean.
    *   This is the canonical 3/4 pose and the single most load-bearing reference pose in the yaw sweep (Zone 2 Visual Reference) — every feature must read as one coherent turn.
    *   **Swap Cohort (IV.0):** FaceBase, Mouth (`Mouth_3Q`), Far Brow (`Brow_Far_3Q`), Projections and Far Eye (`Eye_Far_3Q`) all swap together in the same keyframe + crossfade window — no stagger of parts arriving at different times.
    *   **Z-Depth Reorder (II.2):** the 45.1° swap may reorder the near-side projection relative to `Face_Base_3Q` (check per design); the reorder shares the same Schmitt state flag as the asset swap (no frame where art and depth disagree).
    *   **Read contract (XII.4):** the far EAR hiding is correct (anchor-critical ears stay > 0 only where hair doesn't occlude — here the far ear is occluded, allowed); the far eye/brow compress via authored cards (fold, don't squash — XIV.6); the mouth goes off-center as the ONE controlled asymmetry cue on this card (XIII.4).
    *   **Five Anchor Registrations (XII.2):** pupil centers trace the bowed browline (inter-ocular gap = `gap₀·cos(Θ)`); nose tip + mouth center + chin apex stay on the bowed centerline; near ear top stays at the browline — none drift off their cross.
    *   **Hair opposing slide (III.1, X.1):** Bangs slide with the turn (+100%), BackHair/Hair slide opposite (−100%) — the opposing slides ARE the rotational cue at this angle.

### 4. View_Sliver (Y67 P00)
*   **Placement Success Criteria:**
    *   **Eyes:** Far eye must be ENTIRELY HIDDEN by the bridge of the nose / cheek mass ("fold, don't squash" per XIV.6 — never squashed through zero). Near eye swaps to `Eye_Near_3Q` (mildly compressed to reflect foreshortening — it isn't exempt just because it isn't occluded).
    *   **Brows:** Far brow hidden with the far eye. Near brow preserved, mildly compressed.
    *   **Nose/Mouth:** Forms a near-profile stepped edge, heavily compressed against the far boundary. The nose/projection bridge now occupies the position the far eye used to be visible past.
    *   **Teeth:** If the active viseme is A or I, the ridge follows the heavily compressed mouth anchor (VII.2).
    *   **Ears:** Near ear approaching the back-edge of the cranium; far ear hidden.
    *   **Hair (Bangs + HairBack):** Correctly dominates the visual frame, wrapping around the cranium volume. Ahoge preserved (XIII.4).
    *   **Cheeks:** Far cheek fully receded into the contour; near cheek carries the silhouette edge.
    *   **Chin:** Near-profile, on the bowed centerline.
    *   **Neck:** Subtle; beginning to meet the skull behind the ear (XVI.3).
*   **Integration Success Criteria:**
    *   By 90°, everything must be poised to hand off cleanly to the true profile silhouette with no leftover far-side geometry still trying to peek through (Zone 3b Visual Reference).
    *   **Sub-threshold cohort (IV.0):** the 67.5° swap (`Eye_Far_Sliver` + `Eye_Near_3Q`) fires under Schmitt hysteresis; the far eye fades to fully hidden via Fade-Then-Hide (commandment 9), never alpha-zero-hold.
    *   **Read contract (XII.4):** far eye, far brow, far ear all correctly hidden; near eye/brow/ear and ALL hair (anchor-critical) stay > 0. No bridge-safe feature lingers as a ghost on the far side.
    *   **Hair dominance:** the hair annulus (Bangs + Hair) now dominates the visual frame; its silhouette Read Test (I.7) must still pass — near/far ribbons must not collapse into one ambiguous blob with no separating gap.
    *   **Z-Depth (II.2):** Side Hair near/far may begin reordering if bangs cross the centerline — driven by the Schmitt flag, not a raw-angle test.
    *   **Reference Cross (XII.1):** the surviving near-side features (near eye, near brow, nose, mouth, chin) all sit on the same heavily-bowed cross.

### 5. View_Profile (Y90 P00)
*   **Placement Success Criteria:**
    *   **Eyes:** Near eye compressed to a wedge/sliver and anchored at the extreme front vertical contour, intersecting the bridge indentation. Far eye hidden (or the near eye also drops to 0% if fully occluded by the projection bridge — check per design, Zone 4).
    *   **Brows:** Near brow present at the profile; far brow hidden/merged into the contour.
    *   **Nose:** Forms the absolute leading edge of the silhouette profile. Max X bounding box value (`≈ 1.12R`, derived; `1.15R` is an acceptable small artistic exaggeration — tech I.5).
    *   **Mouth/Chin:** Forms the definitive stepped profile curve (upper lip protrusion, philtrum indentation, lower lip recession, chin taper).
    *   **Teeth:** If the open-mouth viseme (A or I) is active, the ridge follows the profile mouth shape (VII.2).
    *   **Ears:** Near ear MUST sit perfectly at the vertical and horizontal center (X=0.5, Y=0.5) of the visible cranium hemisphere (XVI.3). Far ear hidden.
    *   **Hair (Bangs + HairBack):** One continuous outer contour from crown to chin; hair wraps the cranium. Ahoge preserved (XIII.4).
    *   **Neck:** Meets the skull behind the ear (XVI.3) — the profile jaw/ear hinge sits at a visible surface point `(≈ 0.978R, −0.208R)`, NOT the cranium center (tech I.5).
    *   **Cheeks:** Merged into the single silhouette contour line (no separate stitched polyline — I.7).
*   **Integration Success Criteria:**
    *   Internal construction lines must be masked to prevent visual texture confusion.
    *   Narrowest canvas footprint of the whole yaw sweep — confirm the bounding-box shift doesn't clip any Root/Tip lag offset on a long projection (Zone 4 Visual Reference).
    *   **Profile cohort (IV.0, Zone 4):** `Face_Base_3Q` → `Face_Base_Profile`; Nose/Mouth/Teeth/Projections either fully merge into the contour line or drop to 0% — all in the same 90.1° keyframe under Schmitt hysteresis.
    *   **Merged contour overlay:** where Nose/Mouth/Teeth hide, the Head ring carries the single open Catmull-Rom merged profile curve (bridge dip → tip → philtrum → lips → chin) so the read is continuous, not a featureless edge.
    *   **Read contract (XII.4):** one single visible eye (or none, per the per-design occlusion check); far eye/brow/ear/cheek hidden; the anchor-critical near ear sits at `(0.5, 0.5)` of the visible hemisphere and ALL hair stays > 0.
    *   **Five Anchor Registrations (XII.2):** nose tip = absolute leading edge (max X, `≈1.12R`); chin apex + mouth center on the centerline; near-ear top at the browline; pupil center on the browline arc — none drift.
    *   **Solid Drawing (commandment 16):** the profile silhouette stays as WIDE at the eye line as the front view — pose selection preserves volume; the turn must not lose weight.

### 6. View_Back3Q (Y135 P00)
*   **Placement Success Criteria:**
    *   **Blanking (IV.5):** ALL facial features at 0% visibility — eyes, brows, nose, mouth, teeth, cheeks, and chin are fully hidden (bridge-safe features MUST hide in walk-behind Z5/Z6/Z7 per XII.4, not just "may").
    *   **Ears:** Near ear may be partially visible from the back as a flat back-fuzz plane (projections rotate to back-fuzz at 135°, Zone 5). Anchor-critical (XII.4) — visibility stays > 0 unless fully occluded by BackHair per the design.
    *   **Bangs (HairFront):** Identity layer, anchor-critical (XII.4) — never fully hidden; carries the read alongside the outer hair mass.
    *   **HairBack / BackHair:** The outer hair silhouette MUST completely carry the visual read.
    *   **Neck:** Narrows as it rises into the skull (I.6), visible from the back turn.
*   **Integration Success Criteria:**
    *   The Silhouette Read Test (I.7) matters most here — this pose carries the entire read with zero interior facial detail, so the hairstyle silhouette alone must stay identifiable.
    *   **Read contract (XII.4):** ALL bridge-safe features (eyes, brows, nose, mouth, teeth, cheeks, chin) are verifiably 0% — a single peeking eye/mouth edge is a defect; the anchor-critical silhouettes (Bangs, Hair, BackHair) and ears (as back-fuzz) stay > 0.
    *   **Back-fuzz cohort (Zone 5):** projections rotate to flat back-fuzz planes at 135°; the BackHair/Hair annulus completely carries the read. No facial interior line may bleed through the hair mass.
    *   **Z-Depth (II.2):** BackHair is preparing its promote to stack position 1 (completes at 180°); FaceBase begins demoting — both driven by the Schmitt flag.
    *   **Cross-Zone Asymmetry (XIII.4):** any hand-drawn back-fuzz ribbon curl is re-authored for this cell (mirror exception class), NOT ridden through the mirror shortcut.

### 7. View_Back (Y180 P00)
*   **Placement Success Criteria:**
    *   **Face:** Swaps to a featureless cranium sphere (`FaceBase_Back`) — eyes, brows, nose, mouth, teeth, cheeks, and chin are ALL at 0%.
    *   **Ears:** Hidden or showing extreme rear-profile slivers depending on hair coverage (anchor-critical — a sliver may persist, XII.4).
    *   **Bangs (HairFront) / HairBack / BackHair:** Back Hair promotes to stack position 1; Face Base demotes to back (II.2 reorder). The back-half hair annulus is the dominant read.
    *   **Neck:** Narrows rising into the skull, visible at the base of the sphere.
    *   **Crown Highlight:** Visible and correctly positioned horizontally centered (0.3 opacity, I.6).
*   **Integration Success Criteria:**
    *   The featureless sphere asset MUST honor the same cranium-circle proportions from I.2 so the crown curvature doesn't visibly mismatch the front view (Zone 5 Visual Reference).
    *   Backdrop never slides (multiplane rule X.1 / commandment 4) — the Z-5 BackHair layer is stationary.
    *   **Back cohort (IV.0, Zone 7):** the 180° swap is the single largest silhouette gap in the matrix — verify the Swoosh-vs-crossfade test (X.7 #17): if the outgoing front/incoming back non-overlapping outline area exceeds the tolerance within one seam margin, a Swoosh (not a plain crossfade) is required.
    *   **Z-Depth Reorder (II.2):** BackHair promotes to stack position 1, FaceBase demotes to back — both on the same Schmitt flag as the 180° asset swap; the emotion-effects tier re-anchors relative to the new stack top (never a fixed absolute index).
    *   **Read contract (XII.4):** ALL bridge-safe features verifiably 0% (no eye/nose/mouth ghost on the featureless sphere); anchor-critical hair + ear slivers may persist.

### 8. View_Top (Y00 P90)
*   **Placement Success Criteria:**
    *   **Crown / Hair-Whorl:** Must display the crown / hair-whorl silhouette exclusively (V.2). The crown highlight patch (I.6) is the primary readable shape.
    *   **Eyes / Brows / Nose / Mouth / Teeth:** ALL Primary Features drop to 0% visibility in the same keyframe as the +45.1° hard swap (Swap Cohort, IV.0; depth reorder II.2). In the pre-swap parallax (V.1) they sit at the extreme lower boundary (Y ≈ 1.0) and are almost entirely occluded by the upper hair layers.
    *   **Nose:** Tip may slightly break the bottom boundary during the pre-swap parallax phase only.
    *   **Ears:** Folded/hidden from above.
    *   **Bangs (HairFront) / HairBack:** The hair-whorl pattern dominates the upper silhouette; Front Bangs and Hair Shadows reorder around the crown asset so the whorl becomes the dominant visible layer (II.2 pitch reorder).
    *   **Neck:** Minimal / hidden from directly above.
*   **Integration Success Criteria:**
    *   The Top asset's outer silhouette circularity must match the cranium-circle proportion from I.2 at the same radius R used everywhere else (Zone V.2 Visual Reference).
    *   **Top cohort (IV.0, V.2):** the +45.1° hard swap drops ALL Primary Features (eyes, brows, nose, mouth, teeth) to 0% in the same keyframe as the `FaceBase_Top` swap — the depth reorder (II.2) pulls Front Bangs + Hair Shadows around the crown so the whorl dominates.
    *   **Read contract (XII.4):** no eye/nose/mouth art bleeds through the crown hair — P+ Top is a hard-blank zone for bridge-safe features; the hair whorl + crown highlight patch (I.6) are the sole read.
    *   **Pole limit (III.4):** horizontal yaw parallax collapses under `cos(90°)=0`; any residual crown motion (hair whorl drift) is planar deformation of the single Top asset, NOT new swaps — there are no further tiers from +45.1° to +90°.

### 9. View_UnderPlane (Y00 Pn45)
*   **Placement Success Criteria:**
    *   **Jaw/Chin:** Jaw arc moves upward, revealing the distinct underside plane of the chin/neck connection. The Neck Patch layer expands to show the jaw underside (V.4).
    *   **Nose/Mouth:** Shifts dramatically upward. Nose must reveal the under-plane / nostrils. Mouth shifts up with the jaw complex (R_jaw domain, tech I.6).
    *   **Teeth:** If the active viseme is A or I, the ridge follows the upward-shifted mouth anchor (VII.2).
    *   **Eyes:** Flattened along the Z-axis curve, shifted upward, maintaining the 0.8 gap but visually compressed vertically.
    *   **Brows:** Shift upward with the eye group; may be occluded by the brow ridge from below.
    *   **Ears:** Visible from below; bottom edge still level with the nose baseline (I.6).
    *   **Bangs (HairFront) / HairBack:** Recedes upward, less visible from below.
    *   **Neck:** The Neck Patch is the most-stretched layer at `−90°`, doing the most seam-margin work of any layer in the rig — its proximity-scaled seam margin (II.4 + III.5 floor) must survive the stretch without opening a gap (V.4). Neck insertion point `≈ (0, −1.6R)` (tech I.5).
*   **Integration Success Criteria:**
    *   No second dedicated "true bottom" asset exists — the Under-Plane asset stretches all the way to nadir via parallax alone (V.4), so its fill shapes need enough headroom to reach `−90°` without a seam opening.
    *   **Bottom cohort (IV.0, V.4):** the −45.1° swap to `FaceBase_UnderPlane` is swap-AND-continue-parallaxing (asymmetric vs Top's swap-and-stop); from −45.1° to −90° the Neck Patch keeps expanding and the nose/mouth keep translating, all via parallax with NO further swap.
    *   **Z-Depth Reorder (II.2):** Neck Patch + Neck Outline move forward in the stack as the jaw view opens; the Nose projection reorders relative to the jaw plane — both on the Schmitt flag, no adjacent-frame disagreement.
    *   **Read contract (XII.4):** eyes/brows flatten and shift up (still > 0 until occluded by the brow ridge from below); nose reveals the under-plane/nostrils; the Neck Patch carries the jaw underside — no layer leaves art behind without an incoming asset (Fade-Then-Hide, commandment 9).
    *   **Seam margin across ALL layers (II.4 + III.5):** every layer's extension margin scales with the proximity factor and survives the stretch to `−90°` — the Neck Patch does the most work, but the jaw/nose/mouth fills must not open a gap either.

### 10. Mirrored Views (Y_L Variants: 3Q_L, Profile_L, Narrow_L, Sliver_L, Back3Q_L)
*   **Placement Success Criteria:**
    *   Every visible feature (eyes, brows, nose, mouth, teeth, ears, bangs, hair, cheeks, chin, neck) must be the mathematically exact horizontal mirror of its right-side partner geometry — `theta_0 → −theta_0`, X-axis signs flip, pitch terms untouched (III.3).
    *   **Paired features** (Eye_L ↔ Eye_R, Brow_L ↔ Brow_R, Cheek_L ↔ Cheek_R, Ear_L ↔ Ear_R) resolve to the PARTNER's ring mirrored (slot-for-slot L == mirror(R); the P45 role split is the only role-split slot).
    *   **Asymmetrical elements** (ahoge, specific hair swoops, brow tilt, single earrings) must be correctly re-symmetrized OR preserved per the technical limits of the pipeline (XIII.4) — the three mirror-exception classes (asymmetric design flags, hand-drawn back-fuzz curls, bridge-safe residual corrections) must be re-authored separately, NOT ridden through the mirror shortcut (XI.7).
*   **Integration Success Criteria:**
    *   The `−45°` view must stay the exact horizontal mirror of `+45°` with the near card riding the left side — no feature should read as a different character on the mirrored side.
    *   **Mirror transform (III.3):** `theta_0 → −theta_0`, X-axis signs flip, Y/pitch terms untouched — applied identically regardless of `R_cranium` vs `R_jaw` domain. Every feature (eyes, brows, nose, mouth, teeth, ears, cheeks, chin, neck, all hair) mirrors as a unit.
    *   **Five Anchor Registrations (XII.2):** pupil centers, nose tip, mouth center, chin apex, and ear tops on the mirrored Reference Cross match the partner cell's at the shared boundary within `EPSILON_CROSS` (~0.5 px) — no drift across the mirror seam.
    *   **Paired-feature resolution:** Eye_L ↔ Eye_R, Brow_L ↔ Brow_R, Cheek_L ↔ Cheek_R, Ear_L ↔ Ear_R resolve to the partner's ring mirrored (slot-for-slot L == mirror(R)); only the P45 role-split slot differs.
    *   **Mirror exceptions (XI.7):** asymmetric design flags (ahoge, single earring), hand-drawn back-fuzz curls, and bridge-safe residual corrections are re-authored/recomputed for the mirrored cell — NOT flipped copies (geometry mirrored, correction recomputed against the mirrored anchor).

---
## PHASE 3: SUMMARY OF ALL GAPS & FIX PRIORITY

### Critical Blockers (Must fix to pass `art_guide` aesthetics)
1. **Eye & Iris Enlargement:** Eyes need 1.5-2x scale up (1:3.5-4 ratio). Iris must fill 70-85% of eye opening. Ensure baseline Y anchors are maintained during scale up.
2. **Nose Reduction:** Replace visible pentagon with microscopic triangle/dash (0.05-0.10 eye-width).
3. **Silhouette Smoothing (Cheeks/Jaw):** Remove polygons/angles. Convert to continuous bezier sweeps.
4. **Cranium Volume:** Increase cranium-to-chin ratio from 1:1.2 to 1:1.6-2:1.
5. **Shape Contrast Correction:** Fix the round:sharp ratio from ~1:2 to the required ~4:1 by smoothing the jaw/cheeks.
6. **3Q/Profile Artifacts:** Remove/fade the profile merge overlay (nose bridge/lips) in 3Q/Narrow views to prevent "second face" clutter.
7. **View-Specific Feature Placement:** Correct the inter-ocular gap in Front view to exactly 0.8 eye-widths.

### Minor Refinements (To tackle after Critical Blockers)
8. **Highlights:** Add lower-inner bounce highlight to eyes.
9. **Bangs:** Introduce varied scallop sizes on the inner boundary.

*(Note: All fixes require modifying geometry via `FaceParallaxSchematic.h` or the rendering pipeline. Remember to Re-run generate_art.py and execute `render_svg.py` immediately after code changes to validate against this loop.)*
