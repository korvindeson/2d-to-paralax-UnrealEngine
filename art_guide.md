# Anime Head Line Art & 2D Pin-Rig Animation Manual

A construction and rigging reference for building a monoline anime character head as a fully rotatable, parallax-driven 2D pin rig with hard asset swaps.

### Revision Notes

This manual is a principle-based construction and rigging reference for building a monoline anime character head as a fully rotatable, parallax-driven 2D pin rig with hard asset swaps. It is organized as follows:

- **Parts 0–IX** — the core contract: scope and axes (0), facial construction geometry (I), layer architecture and pin hierarchy (II), the parallax displacement system (III), the yaw rotation zones (IV), pitch rotation (V), the combined-angle blend space (VI), masking and expression states (VII), asset naming (VIII), and the template/target art parity system (IX). These parts fix the monoline/no-morphing contract, the spherical projection math, the per-zone parallax easing, the hard-swap state machine with Schmitt-trigger hysteresis, the camera-proximity behavior, and the per-part replacement workflow.
- **Part X — Cutout Rigging Principles.** The consolidated, auditable rule set the no-deformation contract depends on: the multiplane parallax principle (further layers slide slower; opposing slides read as rotation), the art-swap/angular-discretization principle (a flat image cannot rotate in 3D, so the turn is faked by per-view swaps with an angular crossfade), the swap-set and crossfade principles (fade-then-hide; divorce swap resolution from crossfade weights; crossfade in parameter space), the articulated-cutout principle (fixed outline shapes, only transforms change), the atmospheric-veil principle (a translucent overlay whose opacity ramps with depth), the hand-moved jitter principle (an optional sub-pixel noise for the "breathing" quality), and the **18 commandments** audit checklist.
- **Part XI — The Full View Matrix.** One consolidated enumeration of every view the rig must author: 8 yaw zones × 3 pitch bands × 2 sub-thresholds, mirrored for negative yaw, with the per-cell asset cohort, visibility/depth contract, and parallax/swap authority.
- **Part XII — Cross-View Consistency Contract.** The rules keeping the character readable across 360°: the invariant Construction Cross, the five anchor registrations, the foreshortening math, the anchor-critical vs. bridge-safe read contract, and the mirror-vs-reauthor decision tree.
- **Part XIII — Attractiveness Engineering.** The appeal rules grounded in their principle basis: neoteny and the baby schema, the cardioidal-strain transformation (the math that turns a realistic head into an anime head), the ~4:1 round-to-sharp shape-contrast ratio, the eye-highlight conventions, and the uncanny-valley design rules that govern why a no-deformation rig is the *only* motion model that keeps a stylized head out of the valley.
- **Part XIV — Context-Aware Math Foundation.** The canonical forms of every formula the rig depends on, each paired with the context that selects it: the spherical→screen projection, the sine/cosine velocity hierarchy, the smoothstep family, the directional Schmitt trigger, the parameter-space crossfade, the local-delta reset, the cosine foreshortening clamp, and the clamped inverse-proximity law.
- **Part XV — Atmospheric Perspective & Depth Haze.** The per-layer translucent haze overlay (opacity `1 − e^(−k·Z)` scaled by a `mist` parameter), the per-layer compositing rules, the melt-away crossfade, the seam-margin interaction, and the optional hand-moved jitter.
- **Part XVI — Anime Girl Proportions & Personality (Owner's Preference).** A simple, proportion-centric art reference: the canonical front-view numbers, eye-shape variants and their personality signals, per-view proportion changes across all 7 views, hair design, the personality archetypes as visual shorthand, and the Owner's Preference slider table.
- **Glossary** of the rig's domain terms.

**Prior edition (rotation-math hardening).** Closed implementation gaps in the rotation math, formalized the placement/no-jump guarantees, and added camera-proximity behavior:

- Replaces the vague "rotation matrix projection" instruction with one explicit spherical-coordinate formula, plus a fixed yaw-then-pitch rotation order, used consistently by parallax, the Reference Cross, and blend space.
- Corrects the easing description in III.2 (true sphere-limb motion decelerates monotonically toward profile — it isn't a symmetric slow-fast-slow ease) and gives the exact per-zone formula that guarantees velocity continuity at every swap for free.
- Assigns concrete trigger angles (22.5°, 67.5°) to the two mid-zone eye/projection swaps Part IV already implied but never pinned down, and formalizes hysteresis as a directional Schmitt trigger.
- Adds a full Camera Proximity behavior model: a clamped proximity formula (the raw 1/Z term was an unguarded singularity), a per-layer-type sensitivity table, and the rule that hard-swap threshold angles never move with proximity.
- Adds a Diagonal Cohort Crossing rule for simultaneous yaw+pitch threshold crossings, and a Residual Correction system reconciling hand-drawn corner art against the formula.
- Adds a "Visual Reference" description to every zone/pose so each view's expected look — and how parallax hands off to the next art swap — is explicit rather than implied.
- Adds numeric starting defaults for pin lag/chain decay, and ties seam-extension margins to the proximity factor.

---

## PART 0 — Scope, Axes & Conventions

Coverage: single character head/neck unit, monoline line art, non-destructive 2D pin rig (deformer + hard-swap hybrid cutout rig).

Rotation Parameters:

- Yaw (X-axis turn): −180° to +180°, where 0° = full front, +180° = full back. Positive values turn the character toward camera-right.

- Pitch (Y-axis tilt): 0° = eye-level neutral. Range extends to true zenith at +90° (camera directly above, looking straight down at the crown — the Top View) and true nadir at −90° (camera directly below, looking straight up at the underside of the jaw — the Bottom View). +45.1° is the hard-swap threshold into the single Top View asset; −45.1° is the hard-swap threshold into the Under-Plane asset, which then carries the rig the rest of the way to −90° through parallax alone, with no further swap (see Part V).

- Yaw Sub-Thresholds: in addition to the five hard-swap thresholds below, two lower-magnitude swap points sit mid-zone — 22.5° (Zone 1's internal swap: `Eye_Far_Narrow`, compressed projection) and 67.5° (Zone 3's internal swap: `Eye_Far_Sliver`, `Eye_Near_3Q`). These exist because the yaw system was never purely continuous inside 0°–45° and 45.1°–90° — Part IV already calls for intermediate assets in both spans; this edition pins down exactly where they fire. They follow the identical Hard-Swap Transition Rule (hysteresis + crossfade, Part IV.0) as the five primary thresholds — lower visual impact, not lower rigor. See Part IV, Zones 1 and 3.

Projection Model & Camera Independence: every rotating anchor (eye, brow, nose dash, projection root, hair root) is authored with an initial spherical position on the cranium sphere — an azimuth angle θ0 (horizontal offset from the centerline, front view) and an elevation angle φ0 (vertical offset from the browline, front view), both measured from the sphere's front pole (see I.5's Coordinate Locking). The Master Rotation Parameter (yaw θ, pitch φ) rotates this spherical position; Part III.4 defines the exact formula that turns (θ0+θ, φ0+φ) into a 2D screen offset. Camera proximity (Part III.5) is a separate, camera-level parameter that scales the *magnitude* of the resulting 2D offset — it never changes which hard-swap threshold fires, since thresholds are defined purely in rotation-parameter degrees, never in screen distance.

Symmetry Axis: A vertical axis runs through the Head Pin. Full asset sets are authored for 0° → +180° yaw only. The −180° → 0° range is produced by horizontally mirroring the positive-side assets (see Part III.3, which gives the exact sign-flip this mirror applies). Any element that breaks left/right symmetry in the base design (off-center part, single earring, asymmetric bang length) must be flagged at the construction stage and given a dedicated, non-mirrored asset for the opposite turn — do not rely on the mirror shortcut for asymmetric elements; see I.7's Cross-Zone Asymmetry Continuity check for verifying these stay correctly asymmetric across every asset that touches them.

Asset Naming: see Part VIII. All swap states referenced below resolve to a file name using that schema.

Template & Target Art: this rig is built and validated against a placeholder template character first. Every part of that template is intended to later be replaced, piece by piece, with final target character art (see Part IX). The template is not throwaway scratch art — it is built to the exact same construction contract the target art must also follow, so the rig's behavior doesn't need to be rebuilt when real art drops in.

Full-Matrix Pre-Build: this rig is a pre-built asset selector, not a generative one. Nothing is drawn or interpolated into new line art at runtime — every zone in the corner-pose grid (Part VI) must exist as finished, complete art (front, 3/4L, 3/4R, profL, profR, back, and their pitch combinations) before the rig is functional, for both the template and, later, the target art. This includes the yaw sub-thresholds (22.5°, 67.5°) even though they sit inside a zone rather than at its boundary — a rig with the five primary thresholds built but the two sub-thresholds missing still has gaps in the matrix. A rig with gaps in that matrix simply has no asset to show at that rotation value.

Single Master Rotation Parameter: one control value (e.g., a UI rotation slider, −180° to +180° yaw, paired with a pitch value) drives both systems in lockstep — the continuous parallax transforms in Part III and the discrete zone swaps in Part IV/V read from the same value. There is no separate "which zone" control; the zone is derived from the same number that drives the parallax offset. Camera proximity, by contrast, is never part of this value — it composes with the parallax output afterward (III.5) and has no swap authority of its own.

---

## PART I — Facial Construction Geometry

### I.1 The Monoline Constraint

Every line — outer silhouette or inner detail — is drawn at a single uniform width with no tapering. Endpoints that would otherwise taper (hair tips, lash edges) are built geometrically instead: two uniform lines meeting at a sharp closed angle rather than a point. Occlusion and depth are conveyed entirely through solid-fill patch layers placed behind line art, not through line weight. The same fill-shape approach carries the appeal side of the equation: specular highlights (eye shine, hair gloss) are solid light-fill shapes layered within or in front of the relevant feature — the same technique as an occlusion patch, just inverted in tone and depth position. Every appeal cue in this manual is built from shape and placement, never from how the line itself is rendered. This is also what keeps the rig compatible with camera-proximity scaling (III.5): because depth is never expressed as a line-weight change, nothing about proximity math ever needs to touch stroke rendering at all.

### I.2 Cranium & Jaw Foundation

- Cranium Anchor: the top half of the head is a perfect circle. This is the primary volume anchor for the whole rig and the origin point for every coordinate defined below. Its radius, R, is also the authoring radius used by every spherical-projection calculation in Part III — record it once, explicitly, rather than letting it be implied by the drawing.

- Equator & Jaw Origin: a horizontal line through the circle's center. The jaw originates at its left/right intersections with the silhouette.

- Jaw Curve: drops in a smooth curve, pushing slightly outside the cranium's vertical drop before cutting inward to the chin.

- Chin (V-Apex): a slightly blunted V. Its lowest point sits 0.5 cranium radii below the bottom of the cranium circle.

- Hairline Arc: a second arc, concentric with the cranium circle, inset 10% inward from the silhouette at the crown, widening outward to meet the jaw origin points at the equator. This arc is the missing piece needed for the anchor coordinates in I.4 — it doesn't need to be inked, but it must exist as a construction guide.

Every coordinate defined in this section is captured, at construction time, as a spherical azimuth/elevation pair per I.5's Coordinate Locking model — this is what lets Part III's rotation formula move these points correctly once yaw and pitch are introduced.

### I.3 The Rotational Reference Cross

Before any swap asset is drawn, establish a centerline (vertical, crown to chin) and a browline (horizontal, through the eye baseline) — together, the Reference Cross. In front view these are straight lines. As yaw increases, both bow to follow the surface of the cranium sphere: the centerline curves toward the turn direction, and the browline compresses horizontally on the far side.

Bow & Compression, Formalized: the centerline's bow and the browline's compression are not freehand approximations — both are the visible trace of the same spherical projection used everywhere else in this manual (III.4). At yaw θ, a point authored at azimuth θ0 projects to a horizontal screen position proportional to sin(θ0+θ), and its apparent width relative to a same-sized point at the front pole compresses by a factor of cos(θ0+θ) as the surface foreshortens away from camera. Draw the Reference Cross at each hard-swap threshold by plotting azimuth samples across the visible half of the sphere (every 15° is a reasonable sampling interval) through this pair of formulas, then connecting them with the Curve Continuity standard (I.7) — one smooth sweep, not a stitched polyline.

Pitch bows the cross the same way, orthogonally: as pitch φ moves away from 0°, the browline's vertical position shifts per sin(φ) and the whole cross compresses vertically toward whichever pitch pole it's approaching (crown at +90°, jaw underside at −90°), per cos(φ). A combined yaw+pitch pose bows the cross on both axes at once — always compute yaw's contribution first, then pitch's, per the rotation-order convention in III.4, so a diagonal Reference Cross is unambiguous rather than order-dependent.

Redraw or reference this cross at every hard-swap threshold, including the two yaw sub-thresholds (22.5°, 67.5°) whenever those zones introduce a new intermediate asset — not only at the five primary thresholds. Any asset that gets its own swap state needs its own verified Reference Cross, because that cross — not the previous asset's silhouette — is what the new asset's features are placed against. It's what keeps eyes, nose, and mouth landing in the same relative position across every swap asset instead of drifting asset-to-asset. Every anchor coordinate and swap-zone placement in this manual is defined relative to this cross, not to the flat page.

### I.4 Universal Feature Placement

- Absolute Midline: the exact midpoint between the top of the cranium circle and the bottom of the chin is the baseline for the bottom of the eyes.

- 5-Part Width Rule (front view only): the face width at the eye baseline is five segments — outer margin, eye, gap, eye, outer margin — but the segments are not equal, and dividing them equally is a defect, not a variant: equal segments reproduce the realistic 1:5 eye-to-face ratio (XIII.2's classical, pre-cardioidal-strain column), not the anime target this manual is built to produce. Use the anime-default proportions instead: margin = 0.5 eye-widths, gap = 0.8 eye-widths, each eye = 1 eye-width (total face width = 3.8 eye-widths, an eye:face ratio of ≈1:3.8, matching XVI.1's anime-default 1:4 target within rounding). This is the same grid XVI.1 and XVI.8 build from — the two sections describe one construction rule, not two independent ones; do not average or split the difference between "5-part" and the numbers below.

- Nose & Mouth Baselines: measured along the centerline between the eye baseline and the chin tip. The nose indicator sits at roughly 60% of that distance — closer to the chin than to the eyes. The mouth sits at roughly 80–85% of that distance, close to but not touching the chin curve. Keeping both baselines shy of the chin, rather than crowding downward, is what keeps a classic anime face reading as open rather than compressed.

- Compressed Grid (3/4 and profile): the 5-part grid does not scale down uniformly as yaw increases — it wraps around a sphere, so the near-side segments compress modestly (roughly 20% at the 3/4 threshold) while far-side segments compress much more sharply (45–60%, approaching zero as the far eye disappears behind the nose bridge near profile). Do not linearly interpolate the front-view grid to place 3/4 or profile features — place them against the Reference Cross (I.3) at each hard-swap threshold instead.

  These percentages aren't arbitrary — they're the cosine foreshortening from I.3's formula, read at each segment's own azimuth rather than at the yaw value alone. A near-side segment centered around azimuth 20°–25° from the pole compresses by roughly 1 − cos(22°) ≈ 7% from cosine alone, but because the whole near half of the face is also sliding per the Sine Rule (III.4) and the segment boundaries themselves are moving, the visible width change compounds to the ~20% figure above. The far-side segment sits at a much steeper azimuth (60°–80°+), where cosine falloff alone approaches 1 − cos(70°) ≈ 66%, consistent with the stated 45–60% (and asymptotically total) far-side compression. Treat the stated percentages as the validated hand-placement targets, and the cosine relationship as the reason they're correct — not as two competing systems that need reconciling on their own.

### I.5 Volumetric Anchor Coordinates

- Parietal Coordinate (high-projection anchor — animal ears, horns): the point where the Hairline Arc (I.2) intersects the cranium sphere nearest the feature's natural base.

- Center-Face Coordinate (central-projection anchor — muzzles, snouts): the intersection of the centerline and the browline. This is also where the nose indicator sits — there is no separate nose-bridge structure to draw; the centerline segment between the browline and the nose dash *is* the bridge line for anchoring purposes.

- Coordinate Locking: a feature's root is pinned to its coordinate on the skull sphere, not floated on the face surface, so it rotates with the cranium regardless of angle. Record θ0 and φ0 for every locked coordinate (Parietal, Center-Face, and any custom projection root) in the asset's construction notes alongside the coordinate itself — the rig has no way to derive these angles after the fact from a flat front-view drawing alone, and every downstream formula in Part III and Part VI depends on having them.

### I.6 Feature Construction

- Eyes: upper lash is a closed geometric wedge; lower lash is a disconnected line segment (prevents a boxed-in look). Eye width follows the 5-part grid (I.4); eye height runs 70–80% of eye width — tall enough to read as rounded and expressive, short enough to avoid a slit. The iris is a closed shape whose top edge tucks slightly under the upper lash rather than floating clear of it, which is what sells the eye as socketed rather than pasted on. Iris and pupil are flat, solid-fill concentric shapes — no blended gradient. Highlights are solid light-fill shapes, not lines: one larger highlight in the upper-outer quadrant of the iris plus one or two smaller secondary highlights is the standard sparkle pattern, and it's the single strongest appeal lever available under the monoline constraint, since none of it depends on how any line is drawn. How each eye variant (Narrow, 3Q, Sliver, Profile) modifies this base construction is specified per zone in Part IV; every variant still obeys the socketed-iris and wedge-lash rules above.

- Eyebrows: a single uniform stroke, gently arched or straight, sitting one full eye-height above the eye's upper lash at rest. A few degrees of difference in tilt or arch between the two brows reads as far more alive than a perfectly mirrored pair — reserve true mirror symmetry for a deliberately neutral, blank expression only.

- Nose: a microscopic geometric indicator (triangle or dash) sitting on the Center-Face Coordinate, at the Nose Baseline (I.4).

- Mouth: a shallow curve with a dead-center gap, no corner dots. Default width = one eye-width, centered on the centerline, at the Mouth Baseline (I.4).

- Hair (Ribbon Theory): overlapping ribbon polygons in a strict S-curve rhythm. Inner boundary sits 10–15% outside the cranium circle. Ribbon roots originate from the Hairline Arc (I.2), not from an arbitrary point on the cranium silhouette. Break perfect left/right mirror symmetry with at least one asymmetric element — a single cowlick (ahoge) escaping the main silhouette, an uneven part, or one longer lock on one side — a hairstyle that mirrors exactly down the centerline reads as stiff no matter how clean the construction is. A soft solid highlight patch across the crown, built with the same fill technique as I.1's specular highlights, is the standard way to read hair as glossy rather than flat. How ribbon roots and the S-curve rhythm redistribute across the back-turn assets (135°, 180°) is specified in Part IV, Zone 5.

- Ears: a rounded outer curve tucked between the Hairline Arc and the jaw origin; top edge level with the browline, bottom edge level with the base of the nose indicator (this is the Ear Tops anchor registered in XII.2 — that table has always assumed this shape exists, it was just never specified here). Drawn as one closed monoline shape with a single interior fold-line; no separate lobe unless the character design calls for it. Ears are anchor-critical (XII.4) — they must never drop to 0% visibility on any cell where hair doesn't fully occlude them — so author an explicit Parietal-adjacent coordinate for the ear root (I.5) rather than eyeballing placement per view.

- Neck: two curves dropping from the jaw origin points, width ≈ 0.4 of head width at rest (XVI.7's anime-default slider value) with a very slight widen toward the shoulders. Draw the Neck Outline as its own closed shape rather than an extension of the jaw curve, so it can be independently reordered in the Z-stack (II.2). At back view (Part IV, Zone 5) the neck narrows as it rises to meet the skull; at profile (Part IV, Zone 4) it meets the skull behind the ear.

- Teeth: not drawn for the Closed, Rounded-Small (U), or Neutral-Rest visemes (VII.2). For the open-mouth visemes (Wide-Open A, Narrow-Wide I) only, draw a simple two-line upper/lower ridge inside the mouth shape, at the same uniform stroke width as everything else. This is the sole teeth construction rule in the manual — the "shark teeth" of XVII.2 are a separate, stylized emotion-effect asset and don't share this geometry.

### I.7 Appeal & Silhouette Principles

- Silhouette Read Test: fill the whole construction flat black and check it in isolation. A classically appealing design stays identifiable from silhouette alone — hairstyle shape, ahoge, and any projections should read clearly without a single interior line. Run this test at every hard-swap and sub-threshold asset independently (front, 22.5°, 3Q, 67.5°, profile, back-3Q, back, plus the Top and Under-Plane pitch assets) — not only at the front-view construction. A silhouette that reads cleanly at 0° can still collapse into an ambiguous blob at profile if the far-side hair ribbon and the near-side hair ribbon overlap into one shape with no separating gap; check each threshold's silhouette independently before considering that asset finished.

- Shape Contrast: the construction is already mostly round (cranium, cheek curve, iris) with a minority of sharp geometric accents (hair-tip V-terminations, brow point, ear tip). Keep that ratio lopsided toward round — roughly four rounded shapes for every one sharp accent. An even split between round and sharp starts reading as busy or aggressive rather than appealing.

- Curve Continuity: draw each silhouette edge (jaw curve, hair ribbon edge, ear outline) as one continuous sweep wherever possible, rather than several short segments stitched together. This is a path-smoothness concern, not a line-weight one — a jittery outline looks uncertain at any uniform width, while one confident unbroken curve reads as clean regardless of scale.

- Gap Rhythm Consistency: the spacing rules set elsewhere in this manual — the eye gap (I.4), the brow-to-eye gap (I.6), and the nose-to-mouth gap (I.4) — should feel like one consistent unit of measure repeating down the face, not three unrelated numbers. Consistent rhythm between features is what makes a face look designed rather than assembled.

- Asymmetry for Life: the Symmetry Axis (Part 0) is a production shortcut for the rig, not an aesthetic target for the character. Reserve true bilateral symmetry for hard geometry (cranium, jaw) and keep the small, deliberate asymmetries already introduced above — hair (I.6) and brow tilt (I.6) — intact per pose; don't let a mirrored yaw asset accidentally re-symmetrize a feature that was asymmetric in the source art.

- Cross-Zone Asymmetry Continuity: because each hard-swap asset is a separate hand-drawn piece, it's possible to accidentally redraw an asymmetric element (ahoge, uneven part, brow tilt) as symmetric on one asset while it stays asymmetric on its neighbors — this reads as a pop even though nothing about pin placement or crossfade timing is wrong. Check each new asset's asymmetric elements against the *same* asset's mirrored or adjacent-zone counterpart, not only against the front view, before signing it off.

---

## PART II — Layer Architecture & Pin Hierarchy

### II.0 Layer order

| View       | Yaw°  | Nose | Bangs | EyeR | EyeL | BrowR | BrowL | Mouth | Teeth | Head | ChkR | ChkL | Chin | EarR | EarL | Hair | BkHair | Neck |
|------------|-------|------|-------|------|------|-------|-------|-------|-------|------|------|------|------|------|------|------|--------|------|
| Front      | 0     | 5    | 5     | 4    | 4    | 4     | 4     | 4     | 4     | 3    | 3    | 3    | 3    | 2    | 2    | 2    | 1      | 1    |
| Narrow     | 22.5  | 5    | 5     | 4    | 4    | 4     | 4     | 4     | 4     | 3    | 3    | 3    | 3    | 2    | 2    | 2    | 1      | 1    |
| 3Q         | 45    | 5    | 5     | 4    | 4    | 4     | 4     | 4     | 4     | 3    | 3    | 3    | 3    | 2    | 2    | 2    | 1      | 1    |
| Sliver     | 67.5  | 5    | 5     | 4    | 4    | 4     | 4     | 4     | 4     | 3    | 3    | 3    | 3    | 2    | 2    | 2    | 1      | 1    |
| Profile    | 90    | –    | 5     | 4    | –    | 4     | –     | –     | –     | 3    | 3    | –    | 3    | 2    | –    | 2    | 1      | 1    |
| Back3Q     | 135   | –    | 5     | –    | –    | –     | –     | –     | –     | 3    | –    | –    | –    | 2    | 2    | 2    | 1      | –    |
| Back       | 180   | –    | 5     | –    | –    | –     | –     | –     | –     | 3    | –    | –    | –    | 2    | 2    | 2    | 5      | –    |
| Back3Q_L   | -135  | –    | 5     | –    | –    | –     | –     | –     | –     | 3    | –    | –    | –    | 2    | 2    | 2    | 1      | –    |
| Sliver_L   | -90   | –    | 5     | –    | 4    | –     | 4     | –     | –     | 3    | –    | 3    | 3    | –    | 2    | 2    | 1      | 1    |
| Profile_L  | -67.5 | 5    | 5     | 4    | 4    | 4     | 4     | 4     | 4     | 3    | 3    | 3    | 3    | 2    | 2    | 2    | 1      | 1    |
| 3Q_L       | -45   | 5    | 5     | 4    | 4    | 4     | 4     | 4     | 4     | 3    | 3    | 3    | 3    | 2    | 2    | 2    | 1      | 1    |
| Narrow_L   | -22.5 | 5    | 5     | 4    | 4    | 4     | 4     | 4     | 4     | 3    | 3    | 3    | 3    | 2    | 2    | 2    | 1      | 1    |
| Top        | 0/+90 | –    | 5     | –    | –    | –     | –     | –     | –     | 3    | –    | –    | –    | 2    | 2    | 2    | 1      | –    |
| UnderPlane | 0/-90 | 5    | 5     | 4    | 4    | 4     | 4     | 4     | 4     | 3    | 3    | 3    | 3    | 2    | 2    | 2    | 1      | 1    |

### II.1 Baseline Z-Depth Stack (0° front-facing default)

1. Extended Projections (snouts, horns, long ears)

2. Front Bangs

3. Hair Shadows (solid shape layer)

4. Primary Features (eyes, brows, mouth, nose)

5. Side Hair (near side)

6. Face Base (cranium + jaw contour)

7. Base-Anchored Projections (ears, secondary attachments)

8. Side Hair (far side)

9. Neck Patch Layer (skin fill behind jaw)

10. Neck Outline

11. Back Hair

### II.2 Dynamic Depth Reordering

The stack above is the 0° default only — it is not fixed for the whole rotation. Any layer whose role flips from occluding to occluded (or vice versa) as yaw or pitch changes must have its depth index swapped in the same keyframe as its asset swap, never on an adjacent frame, or you'll get a frame where the art and the depth order disagree.

Known reorder points (yaw):

- 45.1° (3/4 hard swap): near-side projection may need to move above or below Face_Base_3/4 depending on how far it protrudes — check per design.

- 90.1° (profile hard swap): Side Hair (near) and Side Hair (far) may need to reorder if bangs cross the centerline.

- 180° (back turn): Back Hair moves to the front of the stack (position 1); Face Base swaps to the featureless sphere asset and moves to the back. The emotion effects tier (XVII.6) is defined as sitting "above Primary Features" — pin it above whichever layer currently occupies position 1, not at a fixed absolute index, or a temple vein / sweat drop authored for a front-view cell will render underneath Back Hair the moment this reorder fires.

Known reorder points (pitch):

- +45.1° (Top View hard swap): Front Bangs and Hair Shadows reorder around the crown asset so the hair-whorl pattern becomes the dominant visible layer; Primary Features drop out of the stack entirely (0% visibility per Part V).

- −45.1° (Under-Plane hard swap): Neck Patch and Neck Outline move forward in the stack as the jaw view opens up; the Nose projection reorders relative to the jaw plane as it comes into view from below. No further reorder occurs between −45.1° and −90° — the stack holds while parallax (not a swap) carries the rest of the motion.

Reorder Precedence at Diagonal Crossings: when a single rotation change crosses a yaw reorder point and a pitch reorder point in the same update (e.g., a fast diagonal drag through both 45.1° yaw and 45.1° pitch at once), resolve yaw's reorder first, then apply pitch's reorder on top of the result, in the same keyframe as both asset swaps (see Part IV.0's Diagonal Cohort Crossing for the matching asset-swap rule). Always resolving yaw before pitch, in that fixed order, keeps the final stack deterministic regardless of which axis was crossed a moment earlier in the drag — resolving whichever axis "finished crossing first" in wall-clock time would make the same diagonal pose produce two different stacks depending on drag speed, which is its own source of popping.

### II.3 Pin Taxonomy

Not every feature should use the same pin behavior. Use three classes:

1. Positional Pins — flat 2D features (eyes, brows, mouth, nose dash). A single pin, parented directly to the Head Pin, no independent lag. These translate and scale only per their Parallax Displacement rate (Part III).

2. Root/Tip/Lag Pins — projecting/relief features (ears, horns, snouts, long bangs). Root Pin is rigidly parented to its skull coordinate (I.5); Tip Pin is free to lag behind the head's X/Y velocity for secondary motion. As a starting default, Tip Pin lag should carry 15–25% of the Root Pin's instantaneous angular velocity as a bounded positional offset (not a rotation of the art itself, per the Zero-Morphing Guarantee in III.4) — bound it to a maximum radius of roughly one seam-extension margin (II.4) so an aggressive whip-turn can't drag the tip far enough to expose the seam behind it. Ease the lag offset back to zero whenever the head holds still for more than a few frames, rather than leaving it parked off-anchor.

3. Chain Pins — ribbon hair locks. 3–5 sequential pins from root to tip, each inheriting a decreasing fraction of the previous pin's velocity, producing a natural cascade rather than a single rigid swing. As a starting default, each successive pin should inherit roughly 65–75% of the previous pin's velocity (a geometric decay, not linear) — a shallower ratio produces a stiff, rod-like swing, while a much steeper one produces excessive whip on the last link. Keep the decay ratio consistent within a single ribbon lock; mixing ratios mid-chain reads as a kink rather than a cascade.

### II.4 Seam & Gap Prevention

Monoline flat-fill art has no soft edge to hide a seam. Any layer whose edge neighbors another independently-moving layer (jaw/neck, ear/hair, bangs/face) must have its fill shape extended 8–12% past its visible line-art silhouette on that edge. This extension stays hidden under the neighboring layer at rest and prevents gaps from opening during parallax slide or pin rotation.

This 8–12% figure is a mid-shot default, calibrated against the standard-proximity Peak Displacement values in III.1. Because the Proximity Factor (III.5) scales displacement magnitude up at close range and down at long range, the extension margin should scale with it too: multiply the base 8–12% by the same Proximity Factor before locking the fill shape, so a close-up's larger parallax slide is still fully covered. Below a small floor, percentage-based scaling breaks down the same way stroke-width scaling does at extreme close-up (III.5) — so also set a fixed minimum extension margin in absolute canvas units (not percentage), sized to the largest Peak Displacement value the layer will ever carry at the closest supported camera distance, and use whichever of the two (percentage or floor) is larger.

---

## PART III — Parallax Displacement System

### III.1 Displacement Hierarchy (peak values)

| Layer | Peak Displacement |

|---|---|

| High-Projection Features (snouts, etc.) | +150% |

| Standard Nose / Bangs | +100% |

| Primary Features (eyes/mouth) | +60% |

| Face Base | 0% (anchor) |

| Base-Anchored Projections (ears) | −50% |

| Back Hair | −100% |

### III.2 Easing Logic

Displacement must ease in from 0% at each zone's start and build toward its peak as yaw approaches 90° from either zone-local edge — never applied as a flat constant across the whole turn.

To be precise about the *shape* of that build, refining the general description: true sphere-surface apparent lateral velocity is fastest at the front-facing pole and decelerates continuously toward the profile limb — it does not slow down, speed up, then slow down again like a generic animation ease. The correct reference is the derivative of the Sine Rule itself (III.4): velocity ∝ cos(θ), which is highest at θ = 0° and falls to zero at θ = 90°. Build every easing curve directly from this relationship rather than from a symmetric ease-in/ease-out curve — a generic symmetric ease will look subtly wrong (too slow at the very front, where a real turn is actually fastest) even though it feels intuitively "smooth." The peak *displacement value* (position, not speed) is still reached late in the turn, since sin(θ) keeps climbing all the way to 90° even as its rate of climb falls off — position and velocity peak at different points, and both matter.

Per-Zone Rebased Formula: because Local Delta Reset (III.6) zeroes each layer's translation at every swap, the easing curve above can't be a single unbroken sine function running from 0° to 180° — it has to be rebuilt fresh at the start of every zone while still feeling like one continuous turn. Use this exact construction: for a zone spanning [θ_a, θ_b],

```
offset(θ) = Peak × [ sin(θ) − sin(θ_a) ],  for θ ∈ [θ_a, θ_b]
```

This is the same global sine curve, sampled and shifted down so it reads 0 at the zone's own start — and that shift is what makes it automatically compatible with Trajectory Matching (III.6): the derivative of offset(θ) is Peak × cos(θ), completely unaffected by the vertical shift, so the velocity the outgoing asset was carrying at θ_b (Peak × cos(θ_b)) is mathematically identical to the velocity the incoming asset starts with at the same angle. Built this way, boundary velocity continuity isn't something to hand-tune per swap — it falls out of the formula for free, for any zone boundary, as long as every zone uses the same Peak value and the same underlying sine function. If two neighboring zones legitimately need different Peak values (e.g., a layer's displacement table changes because a different feature enters the swap cohort), see III.6's note on ramping a Peak change across the crossfade window instead of letting it jump.

One consequence worth flagging: because velocity is highest at each zone's start and lowest at its end (per the cos(θ) relationship above), the earliest zone in the 0°→90° sweep (0°→22.5°) carries the steepest on-screen motion of the whole turn, and the last zone before profile (67.5°→90°) carries the shallowest — motion should visibly decelerate zone-over-zone as the turn approaches profile, not stay uniform. Applying the peak value as a flat constant across the whole rotation will read as mechanical sliding rather than a turning head.

### III.3 Mirrored Asset Policy

Author full swap sets for 0° → +180° yaw only. For 0° → −180°, mirror every asset horizontally and invert all displacement signs. Exceptions are any asymmetric design elements flagged during construction (Part 0) — those need a dedicated, separately-drawn asset for the mirrored direction rather than a flipped copy.

In formula terms, mirroring negates the authored azimuth (θ0 → −θ0) and, downstream, the sign of every X-axis term in III.4's projection — pitch terms (φ0, and every Y-axis term) are untouched by the mirror. This is why an asymmetric element can't simply ride the mirrored copy: its θ0 was never symmetric around the centerline to begin with, so negating it lands the feature at the wrong azimuth entirely rather than at its correct mirrored one.

### III.4 2D Camera Alignment & Rotation Multipliers

To maintain the illusion of 3D volume while keeping all 2D assets perfectly flat to the camera (billboarding), displacement must rely on precise trigonometric multipliers, not linear sliding.

- The Sine/Cosine Rule: Calculate a layer's X-axis displacement using the sine of the yaw angle, and Z-depth compression using the cosine. For example, at 30° yaw, a layer moves laterally by `sin(30°) × Peak Displacement`. This naturally produces the easing described in III.2 without manual keyframing.

- Spherical Projection Formula: every anchor's authored position is a pair of angles on the cranium sphere — azimuth θ0 (I.3/I.5) and elevation φ0. Given the live Master Rotation Parameter (yaw θ, pitch φ), the anchor's total orientation is (Θ, Φ) = (θ0 + θ, φ0 + φ). Project this to a 2D screen offset and a depth value with:

  ```
  x = R × cos(Φ) × sin(Θ)
  y = R × sin(Φ)
  z = R × cos(Φ) × cos(Θ)
  ```

  where R is the cranium radius (I.2) used as the sphere's authoring radius. (x, y) — after subtracting the anchor's own front-view baseline position — is the 2D translation applied to the pin; z is not drawn, but feeds two things only: the Z-depth compression term used for the foreshortening percentages in I.4, and the sort key referenced by the Layer Z-Depth Stack (Part II). This is the one fixed, checkable formula that replaces any generic "multiply by a rotation matrix" instruction — the same three lines apply to every positional pin in the rig, with only θ0, φ0, and R varying per feature.

- Rotation Order Convention: yaw is always applied before pitch — rotate around the sphere's vertical axis first (producing Θ), then treat the result as the new reference for the pitch rotation (producing Φ), matching the formula above and matching a standard pan-then-tilt camera gimbal. This order is not a free choice: swapping it changes where a diagonal pose (e.g., 45° yaw combined with 30° pitch) lands on the sphere, because the two rotations don't commute. Use this exact order everywhere the formula is used — continuous parallax (this section), the Reference Cross (I.3), and the corner-pose blend space (Part VI) — so a diagonal pose computed by live parallax and the same diagonal pose looked up in the corner grid agree with each other.

- Zero-Morphing Guarantee: The line art itself is never rotated in 3D space, skewed, or morphed. The rotation multiplier applies strictly to the (x, y) translation coordinates produced by the formula above, and to nothing else. The asset attached to that pin remains 100% flat to the camera surface, guaranteeing the strict uniform line width constraint is never broken by digital distortion.

### III.5 Camera Proximity (FOV & Z-Depth Shifts)

Camera distance changes how the rigging layers interact visually, driven by perspective projection, without ever altering the source art.

- Proximity Displacement Scaling (The Z-Divide): As the virtual camera moves closer to the head (wide FOV / close-up), the visual distance between Z-depth layers exaggerates. The peak displacement values (III.1) must be multiplied by a proximity factor. Because of this, high-projection features (snouts, bangs) will appear to slide much further and faster across the face during a head turn in a close-up than they do in a wide shot.

- Proximity Factor Formula (Clamped): use

  ```
  ProximityFactor = clamp( K / max(Z_camera, Z_min), F_min, F_max )
  ```

  where K is a calibration constant tuned so ProximityFactor = 1.0 at the rig's reference mid-shot distance, Z_camera is the current virtual camera distance, and Z_min is a small positive floor (not zero). A raw, unclamped `1 / Camera Z-Distance` term is a real gap in a naive implementation of this rule: as Z_camera approaches zero (an extreme, lens-touching close-up), the term diverges toward infinity, which would send every displacement value toward infinite slide and tear every seam open at once. The Z_min floor and the F_max ceiling exist specifically to prevent that — pick F_max as the largest proximity multiplier the seam-extension margins (II.4) can still cover, and treat any camera distance closer than the value that produces F_max as equivalent to F_max rather than continuing to scale past it.

- Threshold Invariance: hard-swap threshold angles (22.5°, 45.1°, 67.5°, 90.1°, 135°, 180° yaw; 45.1°, −45.1° pitch) are defined purely in rotation-parameter degrees and never move with camera proximity. Proximity changes how far a layer slides once it's mid-parallax and how dramatic that slide looks, but it never changes the angle at which a swap fires. Don't couple the two systems — a common implementation mistake is to let a close-up framing "trigger" an early swap because the parallax looks maxed out; the swap must still wait for the actual rotation-parameter value.

- Per-Layer Proximity Behavior:

  - Face Base (the 0% anchor layer): never slides regardless of proximity — it's the frame every other layer is measured against — but its share of total canvas area grows or shrinks with ordinary camera framing (dolly), independent of the parallax system.

  - Primary Features (eyes, brows, mouth, nose — ±60% base peak): the least dramatically affected layer by design — even at F_max, a ±60% base peak stays subtle enough that the face doesn't read as swimming even in an extreme close-up. This is the layer most sensitive to the seam-and-gap standard (II.4) at close range, since primary-feature edges sit close to Face Base with little natural cover.

  - Standard Nose / Bangs (±100% base peak): moderate proximity sensitivity — a wide shot barely shows bang sway during a turn; a close-up makes it a clearly readable secondary motion. Usually the first layer where proximity scaling becomes visually obvious to a viewer.

  - High-Projection Features (snouts, horns, long ears — ±150% base peak): the most proximity-sensitive layer in the rig, and the one most likely to expose an under-covered seam margin at F_max — verify II.4's proximity-scaled extension margin against this layer specifically before signing off a close-up-heavy shot.

  - Base-Anchored Projections (ears — −50% base peak): moves opposite the high-projection layer and at roughly a third of its magnitude; proximity makes this more noticeable mainly as increased separation from Face Base rather than as fast motion.

  - Back Hair (−100% base peak): symmetric in magnitude to Standard Nose/Bangs but opposite in sign and depth; at high proximity, watch for Back Hair separating far enough from Face Base at the silhouette edge to reveal Neck Patch underneath prematurely — that's a seam case, not a displacement-value case.

  - Neck Patch / Neck Outline: not part of the peak-displacement table at all, but proximity still affects it indirectly — closer framing crops more of the neck out of frame, which changes how much of the Neck Patch's proximity-scaled seam margin (II.4) is ever actually visible; don't over-invest extension-margin art in areas a close shot will crop away, but don't under-invest it for the wide shots that will show the whole neck.

- Telephoto Flattening: As the camera moves further away (narrow FOV / distant shot), ProximityFactor approaches its calibration floor and the parallax effect dampens toward a near-flat slide; the head reads increasingly like a single rigid plane rather than a volume. This is expected and correct — it's the same flattening a real long lens produces — and it should never be compensated for by artificially boosting peak values at long range, or every zone's Local Delta Reset math (III.2/III.6) stops matching what was tuned at reference distance.

- Screen-Space Monoline Protection: Crucial Rule: When the camera zooms in, the digital rig must not simply scale the entire composition up, as this breaks the monoline constraint by thickening the stroke. The camera proximity dictates the pin placement, boundaries, and displacement magnitude, but the stroke weight of the rendering must remain fixed in screen-space pixels. A line drawn at a uniform width must output at that exact uniform width whether the camera is at Z=100 or Z=10. Practically, this means camera dolly (moving the virtual camera) and canvas zoom (scaling the rendered composite) are two different operations feeding two different systems — dolly feeds the ProximityFactor above; zoom, if used at all, must apply after line rendering, as a display-level scale on the final raster/composite, never on the vector art before its stroke is rasterized.

### III.6 Parallax-to-Swap Registration (Zero-Jump Handoff)

To guarantee seamless transitions with no visual popping or jumping when a layer crosses a hard-swap threshold (e.g., crossing from continuous parallax at 45.0° to a new 3/4 asset at 45.1°), the mathematical handoff between math-driven sliding and asset-driven art must be perfectly calibrated:

- Pivot Anchor Uniformity: Every asset in a swap cohort (e.g., `Eye_Near_Front` and `Eye_Near_3Q`) must share the exact same rigid anchor coordinate on the skeleton (defined in I.5). Never center a pin on the bounding box of the line art, because bounding boxes change size and shape when the art flips from front to 3/4. Pin strictly to the underlying Reference Cross, so the origin point remains absolutely static. In formula terms, this origin is exactly the anchor's front-view baseline position referenced in III.4 — the point the (x, y) translation offset is added to, not computed from.

- Local Delta Reset: The parallax displacement multiplier does not run linearly from 0° to 180° for the art itself. When an asset swaps in at 45.1°, its translation delta must reset to zero relative to its new local origin. The incoming 3/4 asset is already hand-drawn to look correct at exactly 45°; if it inherited the cumulative 45° parallax displacement from the front asset, it would double-translate and physically jump completely out of alignment. This is the same behavior formalized in III.2's rebased per-zone formula — "reset to zero relative to its new local origin" and `offset(θ) = Peak × [sin(θ) − sin(θ_a)]` are two descriptions of the same rule.

- Trajectory Matching (Velocity Handoff): The velocity of the parallax (the sine/cosine easing rate from III.2) immediately prior to the 45.1° swap must perfectly match the starting velocity of the incoming asset's parallax curve at 45.1°. If the front asset is slowing down into the swap, but the 3/4 asset accelerates abruptly right after, the animation will visibly stutter, even if the assets align perfectly on the exact frame of the swap. As shown in III.2, this falls out automatically when every zone uses the rebased sine formula with a consistent Peak value. The one case that still needs manual attention is a Peak *value* change at a boundary (e.g., a feature entering or leaving a swap cohort with a different displacement percentage than its neighbor) — when that happens, don't let the Peak jump instantaneously at the threshold; ramp it linearly across the crossfade window (see Parameter-Space Crossfade Width, below) so the velocity discontinuity that Peak change would otherwise cause is spread across a few visible frames instead of landing on one.

- Worked Example: take a Primary Feature (Peak = 60%) crossing the 45.1° threshold, arriving from the 22.5° sub-threshold. Immediately before the swap, the outgoing Zone 1b asset's offset is 0.60 × [sin(45.1°) − sin(22.5°)] ≈ 0.60 × [0.708 − 0.383] ≈ 0.195 (in units of the Peak's reference distance) — this is where the outgoing asset's pin sits at the moment of the swap. Immediately after, the incoming Zone 2 (3/4) asset starts its own local formula at offset(45.1°) = 0.60 × [sin(45.1°) − sin(45.1°)] = 0, per Local Delta Reset — but because the incoming asset was hand-drawn to look correct with its feature already sitting in the right place *at* 45.1°, that reset-to-zero point is defined relative to the incoming asset's own baseline pin position, not the outgoing asset's. The two must be pinned to the identical Reference Cross coordinate (Pivot Anchor Uniformity) for "outgoing offset 0.195 from its baseline" and "incoming offset 0 from its baseline" to land on the same screen pixel — that shared baseline, not the offset math alone, is what actually prevents the jump.

- Sub-Pixel Continuity: perform every step of the formulas above (III.2, III.4, and this section) in floating-point space, and round to device pixels only at the final render composite — never round an intermediate pin coordinate, and never round differently on the outgoing vs. incoming asset during a crossfade. Rounding early is a common source of a 1-pixel "shiver" exactly at swap boundaries that's otherwise invisible in the math; it shows up only in motion, which makes it easy to miss in a static QA pass.

- Parameter-Space Crossfade Width: define the crossfade window in rotation-parameter degrees, not frame count — e.g., a window of ±0.75° around each threshold's hysteresis-adjusted trigger point (IV.0), rather than "the next 2–3 frames." A frame-count window is silently speed-dependent: the same 2–3 frames covers a much wider angular sweep during a fast drag than during a slow animated turn, so a frame-based crossfade that looks clean at animation speed will visibly pop under a fast manual drag, and one tuned for fast drags will linger too long during a slow turn. A parameter-space window crossfades the same angular sweep — and therefore looks the same — at any interaction speed.

---

## PART IV — Yaw Rotation Zones (0° → 180°; mirror for opposite turn)

### IV.0 Hard-Swap Transition Rule (applies globally)

At every hard-swap threshold below — 22.5°, 45.1°, 67.5°, 90.1°, 135°, 180° yaw, and 45.1° / −45.1° pitch — apply a ±1.5° hysteresis buffer so the swap only fires once fully crossed, in either direction. Formalized as a directional Schmitt trigger: for a threshold at value V, define a forward trigger at V + 1.5° and a reverse trigger at V − 1.5°. Increasing rotation swaps to the new asset only on crossing the forward trigger; decreasing rotation swaps back only on crossing the reverse trigger. Anywhere between the two triggers, whichever asset is currently active stays active regardless of small back-and-forth motion — this dead zone, not the buffer width alone, is what prevents flicker. Cross-fade opacity between the outgoing and incoming asset across the Parameter-Space Crossfade Width (III.6) centered on whichever trigger just fired, rather than cutting instantly or tying the fade to a fixed frame count.

Swap Cohort: every layer scheduled to change at a given threshold (Face Base, Mouth, Brow, Eyes, Projections — whichever are listed under that zone) swaps together, driven by the same rotation-parameter crossing, in the same crossfade window. A slider drag from front toward 3/4 should read as one coherent transition at 45.1° — every piece switching to its 3/4 art in the same beat — not a stagger of parts arriving at slightly different times. A layer with no defined state change at a given threshold simply holds its current asset; it isn't part of that cohort, it's just not due for a swap yet.

Crossfade Placement Alignment: During the 2–3 frame crossfade window, both the outgoing and incoming assets will be visible simultaneously. Because of the Local Delta Reset (III.6), the outgoing asset is maxed out at its parallax limit, while the incoming asset is at its baseline zero — forcing them to occupy the exact same spatial pixel coordinate. This prevents a sudden spatial pop of the anchor point during the transition — it does **not** by itself prevent a "double-vision" ghosting effect. Anchor alignment only guarantees the pin origin matches; the outgoing and incoming assets are still two independently hand-drawn silhouettes, and on monoline line art (no soft edges to blend a seam through) their outlines will only read as a clean blend, rather than two overlapping lines fading through each other, if the two designs are close enough in the first place. See III.6's Sub-Pixel Continuity note — anchor alignment only holds if both assets' coordinates were computed in the same unrounded floating-point space.

Swoosh vs. Ordinary Crossfade: this is why Swoosh exists at all (X.7 #17) — a plain crossfade between two silhouettes that differ too much produces the "ugly hybrid" double-image the Swoosh was built to bypass. The manual only ever applies that judgment at the single 180° front↔back boundary; it needs a general test, not a one-off exception. Before accepting a plain crossfade at any hard-swap threshold, check the non-overlapping outline area between the outgoing and incoming assets within one seam-extension margin (II.4) against a set tolerance; above it, use Swoosh instead. Don't assume the 45.1° (front→3/4) and 90.1° (3/4→profile) boundaries pass this test by default — both introduce new silhouette geometry (an eye socket contour, merged nose/mouth/projection edges) that didn't exist in the outgoing asset, and either can fail the same test the 180° boundary fails.

Diagonal Cohort Crossing: when a single rotation change crosses a yaw threshold and a pitch threshold in the same update — a fast diagonal drag through, say, 45.1° yaw and 45.1° pitch simultaneously — treat every layer due to change on *either* axis as one combined cohort, all swapping and crossfading together in the same window, rather than as two separate sequential swaps. Resolve in the fixed order established in II.2 (yaw's asset and depth changes first, then pitch's, within that same keyframe) so the combined result is deterministic regardless of which axis the drag happened to cross a few pixels earlier. A staggered resolution — yaw's cohort swapping visibly before pitch's — reads as two small pops instead of one clean diagonal transition, which is exactly the kind of stagger the ordinary Swap Cohort rule above already prohibits within a single axis.

### Zone 1: Front to 3/4 Transition (0° → 45°)

- Zone 1a (0° → 22.5°): pure continuous parallax, no asset swaps anywhere in the cohort. High-projection features begin their rapid horizontal travel per the Displacement Hierarchy (III.1–III.2); all eyes, brows, nose, and mouth remain on their front-view assets, sliding per the rebased Zone 1a formula (III.2), Peak values taken straight from III.1.

- Zone 1b sub-threshold (22.5°, hysteresis ±1.5° per IV.0): Far Eye swaps to `Eye_Far_Narrow`; near-side and far-side projections swap to their compressed intermediate assets (near-side shifting slightly toward center, far-side translating backward). This sub-threshold exists precisely so "swap to an intermediate asset once projection length exceeds standard flat-face occlusion" has a fixed, checkable trigger value instead of a floating judgment call — verify against the specific character's projection length at build time, but treat 22.5° as the production default.

- Zone 1b (22.5° → 45°): continuous parallax resumes on the newly-swapped assets, per the rebased Zone 1b formula, until the 45.1° hard swap.

- Visual Reference: at 0°, both eyes are fully visible and near-symmetric within the intentional micro-asymmetries of I.6; the silhouette is at its widest, both cheek contours visible. By 22.5°, the far cheek has begun to recede very slightly and the far eye visually narrows via the `Eye_Far_Narrow` swap (a genuine asset change, not a squeeze of the front-view art, per Zero-Morphing); high-projection features have started a visible lateral slide but haven't yet revealed a side plane. By 45°, the face reads as clearly turning — near-side features have slid toward the viewer-facing edge, the far eye is compressed to a sliver of its front-view width, and the silhouette's far edge has pulled in noticeably from its 0° extent, setting up the Zone 2 hard swap.

### Zone 2: The 3/4 Hard Swap (45.1°)

- Face Base swaps to `Face_Base_3Q`, incorporating the indented eye socket and cheekbone contour on the far side.

- Projections swap to an asset showing bridge, top plane, and side plane.

- Mouth swaps to `Mouth_3Q` — a compressed, off-center curve.

- Far Eyebrow swaps to `Brow_Far_3Q` — shortened to match the compressed browline.

- Visual Reference: this is the character's canonical 3/4 pose — the far cheek's indented socket and cheekbone contour (from `Face_Base_3Q`) are now visible as real geometry, not implied by a sliding eye; the mouth sits off-center toward the near side on its compressed curve; the far eyebrow is visibly shortened relative to the near one. This is also the first pose where the character's projection (snout/horn/ear) shows its side plane rather than only its front — treat this as the single most load-bearing reference pose in the whole yaw sweep, since Zones 1 and 3 are both built to ease smoothly into and out of it.

### Zone 3: 3/4 to Profile Transition (45.1° → 90°)

- Zone 3a (45.1° → 67.5°): continuous parallax on the 3/4 cohort's assets. Near Eye swaps to `Eye_Near_3Q` — mildly compressed to reflect foreshortening; it isn't exempt from perspective change just because it isn't occluded. Projections continue translating outward toward the profile contour.

- Zone 3b sub-threshold (67.5°, hysteresis ±1.5°): Far Eye swaps to `Eye_Far_Sliver`, matching the point where it's about to translate fully behind the projection bridge. Treat 67.5° as the production default trigger, same caveat as Zone 1b's sub-threshold — verify against the specific character's bridge width.

- Zone 3b (67.5° → 90°): continuous parallax resumes on the Sliver cohort until the profile hard swap; the far eye's remaining visible sliver eases toward full occlusion behind the projection bridge across this span, reaching 0% visibility no later than 90.1°.

- Visual Reference: at 67.5°, the character reads as most of the way into a profile turn but still recognizably 3/4 — the far eye is a thin, mostly-hidden sliver, the near eye has taken on a slight compression, and the nose/projection bridge now occupies the position the far eye used to be visible past. By 90°, everything should be poised to hand off cleanly to the true profile silhouette without any leftover far-side geometry still trying to peek through.

### Zone 4: The Profile Swap (90.1°)

- `Face_Base_3Q` swaps to `Face_Base_Profile`.

- Nose/Mouth/Projections drop to 0% visibility once fully merged into the profile contour line.

- Near Eye swaps to `Eye_Profile` (single lash line, minimal or no visible sclera) or to 0% visibility if fully occluded by the projection bridge — check per design.

- The rig's bounding box shifts to the profile extreme.

- Visual Reference: this is a true profile — one continuous outer contour from crown to chin to (if visible) jaw, a single visible eye (or none, per the per-design occlusion check), and the nose/mouth/projections either fully merged into the silhouette line or dropped to 0% visibility. This pose has the narrowest canvas footprint of the whole yaw sweep — confirm the rig's bounding-box shift doesn't clip anything the layout depends on, particularly any Root/Tip lag offset (II.3) on a long projection, which can push slightly past the profile's own silhouette during a fast turn.

### Zone 5: The Back Turns (90.1° → 180°)

- 135°: all facial features at 0% visibility. Projections rotate to flat back-fuzz planes.

- 180°: Face Base swaps to a featureless sphere. Back Hair moves to the front of the Z-stack (see II.2).

- Visual Reference (135°): a back-3/4 pose — no facial features visible at all, the character reads primarily through hairstyle silhouette and the back-fuzz plane the projections have rotated into; this is the pose where the Silhouette Read Test (I.7) matters most, since it's carrying the entire read with zero interior facial detail.

- Visual Reference (180°): full back view — a featureless cranium sphere plus Back Hair now in front of the stack; confirm the featureless sphere asset still honors the same cranium-circle proportions from I.2, so the silhouette's crown curvature doesn't visibly mismatch the front-view asset it's opposite to.

---

## PART V — Pitch (Vertical) Rotation (0° → ±90°)

Pitch behaves asymmetrically on purpose: the top of a head converges toward a simple, near-featureless circular silhouette, which is well served by one dedicated asset. The underside of a jaw keeps meaningful, changing geometry (nostrils, jaw contour, neck) all the way to true nadir, which is better served by continuing to deform one asset than by introducing a second flat one. The Hard-Swap Transition Rule and Swap Cohort behavior (IV.0) apply to both pitch thresholds below exactly as they do to yaw. Unlike yaw, pitch has no sub-thresholds analogous to 22.5°/67.5° — its swap structure is deliberately simpler (see V.2).

### V.1 Looking Down — Parallax Phase (0° → 45°)

- Zone P1 (0° → +20°): parallax only — eyes, nose, mouth slide downward per the Displacement table (III.1–III.2), reusing the same Peak percentages defined for yaw and applying them to vertical (y) offset via the pitch term of the Spherical Projection Formula (III.4: y = R sin(Φ)) rather than a separately-tuned pitch table. No asset swap.

- Zone P2 (+20.1° → +45°): parallax continues — projection pivot, translating upward and expanding the rig's vertical footprint; eye assets compress vertically as a deformation, not a swap. No intermediate asset is introduced anywhere in this range — everything from 0° to 45° is achieved by continuing to deform the same front-facing asset set that Part III already defines. There is no "3/4 pitch" tier the way yaw has intermediate 3/4 swaps.

- Visual Reference: at +20°, the crown begins to dominate the upper silhouette as the face plane tilts away from camera; eyes, nose, and mouth have all shifted downward together as one group, still fully legible. At +45°, the face is nearly foreshortened to its floor — features are compressed toward the lower half of the head, the crown occupies most of the upper silhouette, and the pose is visibly one hard swap away from losing facial detail entirely.

### V.2 Top View — Single Hard Swap (45.1° → 90°)

- +45.1° (hard swap): one discrete swap, straight from the fully-parallaxed front asset to a single `FaceBase_Top` asset (crown / hair-whorl silhouette). Facial features (eyes, nose, mouth) drop to 0% visibility in the same keyframe (Swap Cohort, IV.0). See II.2 for the accompanying depth reorder.

- 45.1° → 90°: this entire span is covered by that one Top asset — no further swap tiers exist between the hard-swap point and true zenith. If the crown needs any motion at all across this span (e.g., a hair whorl catching a slight parallax drift), it's handled as a deformation of the single Top asset, not a new swap.

- This is deliberately simpler than the yaw system (Part IV), which chains multiple intermediate swaps (narrow eye → 3/4 hard swap → sliver eye → profile swap) before reaching its endpoint. Pitch-to-top skips straight from continuous parallax to one endpoint asset.

- Visual Reference: a near-circular crown silhouette dominated by the hair-whorl pattern (I.6's crown highlight patch, reused here as the primary readable shape); no facial features anywhere in frame. Confirm this asset's outer silhouette circularity matches the cranium-circle proportion from I.2 at the same radius R used everywhere else in the formula (III.4) — a Top asset drawn to a different implied radius will visibly mismatch the moment a viewer imagines rotating back down to front view.

### V.3 Looking Up — Parallax Phase (0° → 45°)

- Zone P1' (0° → −20°): parallax only — eyes, nose, mouth slide upward, same table-reuse rule as V.1's Zone P1.

- Zone P2' (−20.1° → −45°): the under-plane asset (nostrils/under-jaw) cross-fades in against the standard under-jaw fill, using the same Parameter-Space Crossfade Width convention (III.6) as a hard swap even though the full swap hasn't triggered yet — this is a preview cross-fade, not the committed one. Full swap not yet triggered.

- Visual Reference: at −20°, the jaw's underside begins to open up into view and the neck patch begins expanding to compensate; by −45°, the under-jaw fill is most of the way toward the full Under-Plane asset's geometry via the preview cross-fade, so the −45.1° hard swap (V.4) lands as a near-imperceptible continuation rather than a visible pop.

### V.4 Bottom View — Parallax-Driven Range (−45.1° → −90°)

- −45.1° (hard swap): full swap to the Under-Plane asset. Chin translates downward; the Neck Patch layer expands to show the jaw underside, increasing the lower bounding height. See II.2 for the accompanying depth reorder.

- −45.1° → −90°: unlike the Top View, there is no second flat asset waiting at true nadir. This entire span continues on the same Under-Plane asset, carried purely by parallax — the Neck Patch keeps expanding, the jaw/under-jaw plane keeps translating, and the nose projection keeps repositioning, all per the Displacement system (Part III), right up to −90°. Treat −90° as the point where that parallax reaches its maximum extension, not as a trigger for a new asset.

- Because there's no dedicated "true bottom" asset, the Under-Plane asset must be drawn with enough headroom in its fill shapes (see II.4, Seam & Gap Prevention) to survive being stretched all the way to its −90° extreme without a seam opening.

- Visual Reference: nostrils, jaw underside contour, and an expanded Neck Patch dominate the frame; there is no separate "true bottom" pose to draw toward — as pitch continues past −45.1° toward −90°, this same asset's fill shapes keep stretching per the Displacement system, so its Visual Reference at −90° is simply this same geometry taken to its maximum parallax extension, with the Neck Patch's proximity-scaled seam margin (II.4, III.5) doing the most work of any layer in the rig to keep that stretch from opening a gap.

---

## PART VI — Combined Diagonal Angles (Blend Space)

For any pose that's off-axis in both yaw and pitch at once (e.g., 30° yaw / 25° pitch) and that doesn't cross a hard-swap threshold on either axis, don't hand-author a unique asset and don't treat this as an interpolation between two separate pieces of art — compute it directly with the Spherical Projection Formula (III.4), applying the fixed yaw-then-pitch rotation order to the same base asset cohort that's currently active on each axis independently. This is the normal case, not a special one: most of the working range of the rig is diagonal poses handled exactly this way.

Blend space, properly speaking, only comes into play at a hard-swap threshold: when the live pose crosses a threshold on one axis while sitting at a non-zero, non-corner value on the other (e.g., yaw crossing 45.1° while pitch sits at 25°), the incoming asset swaps per the ordinary Hard-Swap Transition Rule (IV.0), and the *other* axis's parallax (pitch, in this example) continues to apply on top of the newly-swapped asset exactly as it did on the outgoing one — the swap and the cross-axis parallax are independent operations happening in the same keyframe, not blended into each other.

Minimum required corner-pose grid: every intersection of the yaw hard-swap thresholds (0°, 45°, 90°, 135°, 180°) with the pitch corner values 0°, +45°, and −45°. Note the asymmetry: +90° (Top View) is a corner in its own right since it's a dedicated asset, but −90° (Bottom View) is not — it's reached by continuing to parallax the −45° corner pose rather than blending toward a separate asset, so no −90° corner needs to be authored. Everything off the authored grid is interpolated (yaw) or parallaxed (pitch beyond −45°), not drawn.

Sub-Threshold Grid Note: the yaw sub-thresholds (22.5°, 67.5° — Part IV, Zones 1 and 3) are not part of this minimum corner grid on their own, but any pitch corner that's active when a sub-threshold fires still needs that sub-threshold's assets authored for it — an `Eye_Far_Narrow` drawn only at P00 will be missing the moment someone combines it with P45 or Pn45. Budget for sub-threshold assets at all three pitch corners, not only at neutral pitch.

Diagonal Cohort Crossing: see Part IV.0 for the rule governing a rotation change that crosses a yaw threshold and a pitch threshold in the same update — it applies here without modification; the corner grid is what that rule's simultaneous swap resolves into.

Residual Correction (Hand-Art vs. Formula): because every corner-pose asset is hand-drawn rather than generated, its exact pin anchor position can end up a few units off from wherever the pure Spherical Projection Formula would have placed it — a hand-drawn 3/4 eye rarely lands on the mathematically "perfect" pixel. Don't silently accept a formula/art mismatch and don't force the hand-drawn art to match the math exactly either. Instead, record the difference once, per corner, as a fixed Residual Correction offset (formula-predicted position minus actual hand-drawn anchor position), and add that correction into the live pin position, scaled by normalized distance to the corner — full correction exactly at the corner, fading to zero at the midpoint toward the next corner. This keeps the formula's output nudged smoothly onto each hand-authored asset's true anchor without ever popping, and without requiring the art to be redrawn to match the formula.

---

## PART VII — Non-Destructive Masking & Expression States

### VII.1 Dynamic Masking

When projections or hair swing over the eyes, the eye's solid sclera fill acts as an alpha matte, hiding the overlapping lines beneath it rather than requiring a separate cutout layer.

### VII.2 Lip Sync — Standard Viseme Set

Use a minimum 5-shape reduced set for production efficiency: Closed, Wide-Open (A), Narrow-Wide (I), Rounded-Small (U), Neutral-Rest. Each shape needs its own asset at every yaw hard-swap zone (front, 3/4, profile) — mouth shape is a function of both phoneme *and* head angle, so a viseme drawn only for front view will look wrong once the head turns. Cross-reference Part IV for which mouth asset slot each zone expects. Teeth are only ever drawn for the A and I visemes, per I.6's Teeth rule — Closed, U, and Neutral-Rest show none. Viseme swaps are triggered by phoneme changes, not by the rotation parameter, so they don't need the hysteresis buffer defined in IV.0 — there's no equivalent of "hovering near a threshold" for a discrete phoneme choice. They do still benefit from a short, fixed cross-fade purely for visual smoothness between mouth shapes — specify this in milliseconds (roughly 40–80ms), not frames, since "1–2 frames" is a different duration at every runtime frame rate; keep this cross-fade mechanism distinct from the Parameter-Space Crossfade Width (III.6), which is specifically for rotation-driven swaps and has no phoneme-timeline equivalent to measure a "parameter width" against.

### VII.3 Blink States

Pre-draw at minimum: Open, Half, Closed, at the same yaw zones as VII.2. All states share the same construction as the primary eye asset — only the lid position changes. Like visemes, blink states cross-fade on their own short fixed timer rather than the rotation-driven crossfade system — a blink is timed by performance, not by where the head happens to be pointed.

### VII.4 Eyebrow Expression (optional extension)

If expression range beyond blink/lip sync is needed, author Brow states (Neutral, Raised, Furrowed) at minimum for front, 3/4, and profile, using the same zone-based swap logic as the mouth.

---

## PART VIII — Asset Naming & Production Pipeline

Use a consistent token schema so swap states are unambiguous as the library grows:

```

$$Feature$$

_

$$State$$

_Y

$$YawZone$$

_P

$$PitchZone$$

.ext

```

- Feature: `Eye_Near`, `Eye_Far`, `Brow_Near`, `Brow_Far`, `Mouth`, `Nose`, `FaceBase`, `Proj` (projection), `HairFront`, `HairBack`, etc.

- State: the specific swap state — `3Q`, `Sliver`, `Profile`, `A`, `I`, `U`, `Closed`, `Raised`, etc.

- YawZone: `Y00`, `Y22` (sub-threshold, Zone 1b), `Y45`, `Y67` (sub-threshold, Zone 3b), `Y90`, `Y135`, `Y180`.

- PitchZone: `P00`, `P20`, `P45`, `P90` (Top View), `Pn20`, `Pn45` (`n` = negative/looking-up). There is intentionally no `Pn90` token — the Bottom View has no dedicated asset; anything past `Pn45` is a parallax state of the `Pn45` asset, not a separate file.

Sub-threshold tokens (`Y22`, `Y67`) follow the identical folder-per-Feature organization as the five primary tokens — they are not a separate, lower-priority tier of the pipeline, just a lower-magnitude swap within it.

Examples: `Eye_Far_Sliver_Y90_P00.svg`, `Mouth_A_Y45_P00.svg`, `FaceBase_Top_Y00_P90.svg`, `FaceBase_UnderPlane_Y00_Pn45.svg`, `Eye_Far_Narrow_Y22_P00.svg`.

Organize the source library one folder per Feature token, with State/Zone combinations as files inside — this lets an animator locate any swap state by feature first, angle second, without searching the whole set.

---

## PART IX — Template & Target Art Parity System

### IX.1 Purpose of the Template

The template anime girl exists to prove out the rig — pin placement, depth order, parallax curves, swap thresholds, hysteresis, blend space — before any final character art exists. Every rule in Parts I–VIII should be fully testable end to end using the template alone, driven by the slider/rotation control described in Part 0.

### IX.2 The Geometry Contract

Because the template and the eventual target art share one rig, they must share one skeleton. Any target art intended to replace a template part must be constructed against the exact same coordinates defined in Part I:

- Same cranium-circle-to-canvas ratio and the same 0.5-radius chin drop.

- Same Hairline Arc inset (10% from silhouette).

- Same Reference Cross behavior across yaw (I.3) — the target's centerline must bow at the same rate the template's does.

- Same 5-part / Compressed Grid feature placement (I.4).

- Same Parietal and Center-Face anchor coordinates (I.5).

- Same authored spherical azimuth/elevation (θ0, φ0) per anchor (I.5), recorded explicitly rather than inferred — this is what lets target art plug into the Spherical Projection Formula (III.4) without a separate re-tuning pass.

If target art is drawn freehand without matching this contract, it will not sit correctly on the template's pins, and its Root/Tip/Chain pin behavior (II.3) will not match what was tuned against the template.

### IX.3 Full-Matrix Checklist

The corner-pose grid from Part VI is the literal production checklist for both art tracks — every yaw threshold (0°, 45°, 90°, 135°, 180°, mirrored) crossed with every pitch corner (0°, +45°, +90° Top View, −45°) needs a finished asset per feature before that feature can be swapped from template to target. −90° needs no dedicated asset in either art track — it's covered by parallax on the −45° corner (Part V.4). The sub-threshold tokens (Y22, Y67) belong on this checklist too, for every feature they touch (Eye_Far, Eye_Near, Proj) — they're easy to miss because they don't appear in the primary five-threshold list, but a target-art pass that skips them will fall back to template art specifically at those two yaw values, which is exactly the kind of partial-swap seam IX.5's validation pass is meant to catch. Partial matrices are acceptable mid-production (see IX.4), but the rig falls back to the template asset at any coordinate the target set hasn't filled in yet.

### IX.4 Per-Part Replacement Workflow

Target art replaces template art one asset at a time, keyed by the naming schema in Part VIII — `Eye_Far_Sliver_Y90_P00.psd` in target art replaces the identical token in template art, nothing else. This means:

- Layers are independently replaceable. The rig can run with, say, final target eyes and mouth while hair, face base, and projections still use template art — there's no requirement to swap a whole character at once.

- Replacing one asset never requires touching pin data, depth order, or swap-threshold values — those live at the rig level (Parts II–V), not the art level, provided IX.2's contract was followed. Because Residual Correction offsets (Part VI) are anchored per hand-drawn asset, replacing an asset means recomputing its correction offset as part of the swap — this is an art-level value despite living next to rig-level data, and it's the one exception to "replacing art never requires touching rig-level values."

- Recommended replacement order: Face Base first (it's the anchor every other coordinate is measured against), then Primary Features, then Hair, then Projections — this surfaces any contract mismatches early, while the fix is cheapest.

### IX.5 Validation Pass

After any part is replaced, re-check it against the Swap Cohort behavior (IV.0) at its nearest thresholds and confirm the Reference Cross (I.3) still lines up. A target asset that's off-contract usually shows up as a seam opening during parallax (II.4) or a visible pop at a threshold that used to crossfade cleanly. Also re-verify the Residual Correction offsets (Part VI) for any corner pose the replaced asset touches — a new hand-drawn anchor position won't automatically match the old correction value computed against the template's anchor.

### IX.6 Production Scale

This checklist is not a small, fixed count — it cross-multiplies, and nowhere else in this manual is that total ever added up. As a floor estimate: 16 hand-authored primary rotation cells + 8 mirrored, at roughly 8 feature layers per cell before sub-thresholds, is already 150–200 assets for a silent, neutral-expression rig alone. Layer in Part XVII's emotion system (7 emotions × the feature variants and view cells XVII.4 requires) and Part VII's viseme set (5 shapes × the zones VII.2 requires), and the count runs into the several hundreds before the two systems are even made to compose with each other (XVII.7). Treat this as a required production-budgeting step: decide up front which emotion×view and emotion×viseme cells are actually in scope for a given character tier, and rely explicitly on the stated fallback behavior (template art for unauthored rotation cells, IX.3; neutral for unauthored emotion cells, XVII.6) for everything else, rather than discovering the gap mid-production.

---

## PART X — Cutout Rigging Principles

This rig belongs to the cutout-animation family: flat articulated art pieces, jointed at pins, moved by transforms on the pins rather than by mesh deformation. The principles below are the load-bearing rules the no-deformation contract depends on, stated as auditable rules so a build can be checked against them. Every rule maps onto a section of Parts I–IX; this part is the consolidated reference.

### X.1 The Multiplane Parallax Principle

Depth in a flat-art rig comes from **multiple independently-translatable layers separated by an optical gap**. Two rules govern the parallax that gap produces:

1. **The further from the camera, the slower the slide.** This is the entire justification for the signed Peak Displacement table in III.1 — the Nose/Bangs slide fastest (+100%), the Face Base anchors (0%), and the Back Hair slides opposite (−100%). A depth-ordered monotonic slide-rate table *is* the multiplane rule.
2. **Foreground and background sliding in opposite directions produces the read of rotation.** This is why the near-side features slide one way and the far-side pair members (and Back Hair) slide the other during a yaw turn — opposing slides are the rotational cue.

The gap between layers is not empty space — it is what gives each layer its own focus falloff and what makes the depth read volumetrically even when nothing is moving. This is the principle behind the seam margin (II.4) and, when the atmospheric veil of Part XV is enabled, behind the visible depth haze between planes.

### X.2 The Art-Swap Principle (Angular Discretization)

The hard-swap system (Parts IV–V) rests on the principle that **a flat 2D image cannot be rotated in 3D without foreshortening incorrectly**, so the turn is faked by **discrete per-view art swaps**: the same character is represented by different hand-authored art depending on its rotation relative to the viewer, with an **angular crossfade at cell boundaries** to prevent popping. That crossfade is exactly the Parameter-Space Crossfade Width of III.6.

The view sphere is partitioned into a discrete set of angular cells (Part XI); each cell owns one authored asset, selected by nearest angular cell. The principle holds whether the cell count is small (a few canonical angles) or large: the art is *authored once per angle*, never deformed per-frame. This is the entire justification for the Full-Matrix Pre-Build of Part 0 — the rig is a pre-built asset selector, not a generative one. A 3D-modeled anime face "breaks" when viewed off-axis (eyes go asymmetric, nose distorts, mouth contour collapses); a 2D card authored at each canonical angle preserves the intentional drawing at every angle. Same defect class, same solution.

### X.3 The Swap-Set & Crossfade Principles

Three rules govern how discrete art swaps are softened into seamless transitions:

1. **Fade-then-hide.** A layer leaving the cohort fades its opacity to zero *before* the boundary, rather than holding at alpha zero (which wastes fill-rate and risks a double-render). A layer with no incoming art hides at transition start. *(IV.0, V.)*
2. **Divorce swap resolution from crossfade weights.** The visible part is computed from the discrete state alone; opacity is a separate continuous channel. If both the swap resolution and the crossfade drive rendering, two poses render simultaneously through the boundary and produce a double-image. *(IV.0.)*
3. **Crossfade in parameter space, not frame count.** The blend window is a function of the rotation parameter (±0.75°), so a fast drag and a slow drag fade over the same angular sweep. A frame-count window is silently speed-dependent. *(III.6, XIV.4.)*

### X.4 The Articulated-Cutout Principle

Every body part is a **fixed outline shape**; only its position, rotation, scale, opacity, and Z-order change between frames. There is **no in-plane squash, stretch, or vertex deformation** — the medium of cutout animation does not allow it without re-cutting the piece. This is the physical enforcement of the Zero-Morphing Guarantee (III.4): the contract is not an arbitrary digital restriction, it is the same one a pair of scissors enforces on paper. The silhouette line is the source of truth; the fills are secondary. The source art is authored once, against fixed construction coordinates, and the rig never redraws it.

### X.5 The Atmospheric-Veil Principle

A separately-animated translucent overlay, whose opacity ramps with depth, carries the atmospheric read that parallax alone cannot. The haze is a **translucent plane whose distance from the artwork controls the diffusion falloff** — a receding feature dissolves through the veil rather than cutting out. This is the principle formalized in Part XV: it generalizes the fade-then-hide rule (X.3) from swap boundaries to *any* receding depth, and it is what makes the "air between planes" read volumetrically rather than as a flat collage.

### X.6 The "Hand-Moved" Jitter Principle

Perfectly deterministic motion can read as mechanical. A tolerated, sub-pixel position noise — the residual of hand-placement — reads as "breathing" and is, in some aesthetics, considered essential to the medium. The principle is offered as an **optional** overlay: a controllable noise term on the per-frame parallax offset, off by default (the rig's math is exact by design), available as an aesthetic choice when a shot calls for the hand-moved quality. It applies to the pin's translation only, never to the source-art vertices (the art stays immutable, X.4). See XV.6.

### X.7 The 18 Commandments of No-Deformation Cutout Rigging

Every commandment below is enforced somewhere in Parts I–IX; this is the consolidated audit checklist.

1. **The art is immutable.** Never move a vertex of the source art at runtime. Legal per-frame transforms are uniform translation, rotation, scale, opacity, and Z-order of whole pieces only. *(Zero-Morphing Guarantee, III.4.)*
2. **Rotation = a rotation node, not a warp.** If a piece must turn rigidly, dedicate a rotation transform; never bend a mesh to fake a turn. *(III.4.)*
3. **The turn IS the swap.** Between authored pose keys, swap the visible attachment — never morph pose A into pose B vertex-by-vertex. *(IV.0 Swap Cohort.)*
4. **Depth = parallax slide.** Z differences are expressed as different translation magnitudes per layer. **Closest slides most; backdrop never slides.** *(III.1, X.1.)*
5. **Slide peaks are signed per layer.** Foreground and background slide in **opposite directions** to fake rotation. *(III.1, X.1.)*
6. **The velocity hierarchy is monotonic in Z.** The per-tag rate (−100% Back Hair … +100% Nose … +150% High-Proj) stays strictly ordered by depth — never invert two layers' rates or the depth reads inside-out. *(III.1.)*
7. **Author extremes at canonical angles; never interpolate a vertex path.** Pose-to-pose. The in-between is a parameter-space blend of swap + ramp of slide, not a vertex morph. *(IV, VI.)*
8. **Crossfade in parameter space, not in frame count.** The blend window is a function of the rotation parameter (±0.75° around a boundary), so a fast drag and a slow drag fade over the same angular sweep. *(III.6, XIV.4.)*
9. **Fade-then-hide; never alpha-zero-hold.** A layer leaving art behind fades *before* the boundary; a layer with no incoming art hides at transition start. *(IV.0.)*
10. **Divorce swap-set resolution from crossfade weights.** Compute the visible part from the discrete state alone; let opacity be a separate continuous channel. *(IV.0.)*
11. **Use Schmitt-trigger hysteresis on every rotation-driven state boundary.** Two thresholds per boundary (enter/exit), with a deadband (±1.5°) so typical jitter can't toggle the state. Viseme and blink swaps (VII.2, VII.3) are exempt by design, not oversight — they're phoneme/performance-driven, with no "hovering near a threshold" equivalent to guard against. *(IV.0, XIV.3.)*
12. **Ease everything with smoothstep.** Linear interpolation produces the "shrunken shape" / "swim" artifact. Use `3t²−2t³` minimum; `6t⁵−15t⁴+10t³` for C² continuity. *(III.2, XIV.2.)*
13. **Animate Z-order; don't only rely on occlusion.** A layer moving to the back must demote in the sort, or it paints over the front silhouettes. *(II.2 Dynamic Depth Reordering.)*
14. **Break mirror symmetry per pose.** The 3/4 card is not the mirror of the front; insert one controlled asymmetry (ahoge, iris arc, off-center mouth) or the pose reads lifeless ("twins"). *(I.6, I.7, XIII.4.)*
15. **Keep vertex/attachment counts minimal.** Every vertex costs a transform; every extra swap slot costs a draw-call decision. Densify only where deformation actually happens — and here, deformation *doesn't* happen, so stay sparse. *(III.4.)*
16. **Respect Solid Drawing.** Even though art can't squash, *pose* selection must preserve volume: a profile silhouette stays as wide-at-the-eye-line as the front, or the turn loses weight. *(XIII.3.)*
17. **One focal point per instant.** During a swoosh transition, damp secondary layers (hair/cloth) so the *swap event* reads, then re-enable follow-through after. *(Staging principle.)*
18. **Arcs come from compositing axes, not from one.** Pure yaw slide is intentionally "mechanical" (straight) — that's fine. To get an arced path, composite yaw + pitch (or yaw + head-bob); don't try to arc a single axis. *(Arcs principle, adapted.)*

---

## PART XI — The Full View Matrix

Parts IV–VI describe the view system piecemeal — yaw zones in IV, pitch bands in V, the corner grid in VI. This part is the single consolidated enumeration of every view the rig must author, with the asset cohort, visibility contract, depth contract, and the authority (parallax vs. swap) that produces it. **This matrix is the production checklist; a rig with any cell unauthored has a hole in its 360° coverage.**

### XI.1 The Yaw Axis — 8 Primary Zones

The yaw circle is partitioned into 8 zones by 7 hard-swap boundaries. Zone indices follow `EFaceAngleState` order and read in **camera-orbit order** (left → 3/4L → front → 3/4R → right → backR → back → backL, with the backL segment wrapping to the left edge):

| Zone | Yaw center | Yaw range | Primary asset cohort | Authority across the zone |
|---|---|---|---|---|
| Z0 Front | 0° | −22.5° … +22.5° | `FaceBase_Front`, `Eye_Near_Front`/`Eye_Far_Front`, `Mouth_Front`, `Brow_*_Front`, all Projections_Front | Parallax slide only; no swap |
| Z1 3/4L (mirror) | −45° | −67.5° … −22.5° | Mirror of Z2 | Parallax + 1 sub-threshold swap at −22.5° (`Eye_Far_Narrow`) |
| Z2 3/4R | +45° | +22.5° … +67.5° | `FaceBase_3Q`, `Eye_Near_3Q`/`Eye_Far_3Q`, `Mouth_3Q`, `Brow_Far_3Q`, Projections_3Q (side plane visible) | Parallax + 1 sub-threshold swap at +22.5° (`Eye_Far_Narrow`) and +67.5° (`Eye_Far_Sliver`, `Eye_Near_3Q`) |
| Z3 ProfileL (mirror) | −90° | −112.5° … −67.5° | Mirror of Z4 | Parallax + sub-threshold at −67.5° |
| Z4 ProfileR | +90° | +67.5° … +112.5° | `FaceBase_Profile`, `Eye_Profile` (or 0% if occluded), Nose/Mouth/Projections merged into contour or 0% | Parallax; far-side pair members fold (visibility 0%) |
| Z5 Back3QL (mirror) | −135° | −157.5° … −112.5° | Mirror of Z6 | All features 0%; back-fuzz planes |
| Z6 Back3QR | +135° | +112.5° … +157.5° | All features 0%, Projections = flat back-fuzz planes, silhouette reads via hairstyle alone | Parallax |
| Z7 Back | 180° | +157.5° … −157.5° (wrap) | `FaceBase_Back` (featureless sphere), Back Hair → stack position 1, Face Base → stack back | Parallax; **backdrop never slides** (multiplane rule X.1) |

**Boundary set (Schmitt ±1.5° each, IV.0):** ±22.5° (sub-threshold, `Eye_Far_Narrow`), ±45.1° (primary, full 3/4 cohort), ±67.5° (sub-threshold, `Eye_Far_Sliver` + `Eye_Near_3Q`), ±90.1° (primary, profile cohort), ±135° (primary, back-3/4 cohort), ±180° (primary, featureless back).

### XI.2 The Pitch Axis — 3 Bands

Pitch is deliberately simpler than yaw (V.2): it has **two** hard-swap boundaries, not seven, because the top of a head converges to a near-featureless circle well served by one asset, and the jaw underside keeps meaningful geometry all the way to nadir.

| Band | Pitch center | Pitch range | Asset | Authority |
|---|---|---|---|---|
| P0 Neutral | 0° | −45° … +45° | The active yaw cohort's `P00` assets | Parallax (V.1/V.3) for the full ±45° |
| P+ Top | +90° | +45.1° … +90° | `FaceBase_Top` (crown/hair-whorl silhouette); all Primary Features 0% | One hard swap at +45.1° (V.2); no further tiers to +90° |
| P− Bottom | −90° | −90° … −45.1° | `FaceBase_UnderPlane`; Neck Patch expands to carry the jaw underside | One hard swap at −45.1° (V.4); **no second asset at −90°** — parallax carries the rest |

**Asymmetry note (V.4):** Top is a *swap-and-stop* band; Bottom is a *swap-and-continue-parallaxing* band. There is intentionally no `Pn90` corner in the matrix (Part VIII) because the under-plane asset stretches to nadir, it doesn't get replaced.

### XI.3 The Complete Corner Grid

Every cell the rig must author is the cross-product of the 8 yaw zones with the 3 pitch bands — **24 primary cells** — plus the 2 yaw sub-thresholds (`Y22`, `Y67`) which each need their own assets at every pitch band they can co-occur with:

```
        P−          P0          P+
Z0      [parallax]  [FRONT]     [parallax]
Z1m     mirror      mirror      mirror
Z2      [3Q]        [3Q]        [3Q]      + sub-threshold Y22, Y67 at each pitch
Z3m     mirror      mirror      mirror    + sub-thresholds mirrored
Z4      [PROFILE]   [PROFILE]   [PROFILE]
Z5m     mirror      mirror      mirror
Z6      [BACK3Q]    [BACK3Q]    [BACK3Q]
Z7      [BACK]      [BACK]      [BACK]
        +P− swap    (P0)        +P+ swap at 45.1°
```

**Cell count math:** 8 yaw × 3 pitch = 24 primary cells, of which 8 are produced by mirroring (Z1, Z3, Z5) — so **16 hand-authored primary cells** + 8 mirrored. The 2 sub-thresholds add 2 more yaw rows at every pitch band they touch (Eye_Far, Eye_Near, Projections only) = up to 6 more sub-cells per affected feature. The Top and Bottom rows exist only at the +45.1°/−45.1° swap, not at intermediate pitch values — everything between P0 and the swap is parallax on the P0 cohort (V.1, V.3).

### XI.4 Per-Cell Contract

Every authored cell carries four contracts simultaneously:

1. **Asset cohort** — the set of feature files (FaceBase, Eye_Near, Eye_Far, Brow_Near, Brow_Far, Mouth, Nose, Projections, Hair_*) active in that cell. Named per Part VIII.
2. **Visibility per feature** — which features are at 1.0, which have folded to 0.0 (far-side pair members ≥ Z4; all non-silhouette features ≥ Z5/Z6; all Primary Features in P+ Top). See XII.3 for the read contract.
3. **Depth order** — the Z-stack permutation for that cell (II.2). Most-notably: Back Hair promotes to position 1 at Z7; Face Base demotes to back; Primary Features drop out entirely at P+.
4. **Authority** — parallax (continuous slide within the cell) vs. swap (discrete cohort replacement at the cell boundary). The boundary itself always uses the Parameter-Space Crossfade (III.6, XIV.4); inside the cell, parallax is the sole motion authority.

### XI.5 The Mirror Shortcut and Its Exceptions

Cells Z1/Z3/Z5 (the left-half yaw states) are produced by **horizontally mirroring** their right-half partners (Z2/Z4/Z6) per III.3 — `θ0 → −θ0`, X-axis signs flip, pitch terms untouched. **Three categories of element cannot ride the mirror** and must be re-authored separately for the mirrored direction:

- **Asymmetric design elements flagged at construction** (Part 0): an off-center part, a single earring, asymmetric bang length, a deliberate ahoge. Their `θ0` was never symmetric, so negating it lands them at the wrong azimuth.
- **Anchor-critical silhouettes whose back-fuzz plane is hand-drawn** (XII.3): if the back-fuzz art for +135° has a hand-placed ribbon curl that isn't the geometric mirror of the −135° curl, the mirror shortcut silently overwrites the hand placement.
- **Bridge-safe features with a hand-drawn Residual Correction** (VI): the correction offset is per-asset; the mirrored asset gets its own correction, computed against the mirrored anchor, not copied from the partner.

---

## PART XII — Cross-View Consistency Contract

The character must read as *the same character* at every cell of the Part XI matrix. This part consolidates the rules that guarantee that, scattered across I–IX, into one auditable contract.

### XII.1 The Invariant: The Construction Cross

The single thing that must not change across views is the **Reference Cross** (I.3): the centerline (crown-to-chin) and the browline (through the eye baseline), both bowed per the spherical projection formula as yaw/pitch move. Every feature in every cell is placed *against the cross of that cell*, never against the flat page and never against the previous cell's silhouette. If the cross is consistent across the matrix, the character reads as the same character at every angle; if any cell's cross drifts, the read breaks at that boundary no matter how clean the cohort swap is.

The cross is rebuilt at every hard-swap threshold and every sub-threshold by plotting azimuth samples through the III.4 formula and connecting them with the Curve Continuity standard (I.7) — one smooth sweep, never a stitched polyline. The cross is what makes a 3/4 eye land in the same relative socket position as the front eye, even though the eye asset itself has swapped.

### XII.2 The Five Anchor Registrations

Five anchor sets must agree across every cell that shows them. These are the registration marks of the rig — the things an art director checks first when a view "feels off."

| Anchor | Position | Visible in zones | Consistency rule |
|---|---|---|---|
| Pupil centers | Brow equator, ±(5-part eye offset) | Z0–Z4 (folded Z5+) | x-positions trace the browline arc; inter-ocular gap projects as `gap₀ · cos(θ)` |
| Nose tip | Centerline, sub-nasal y | Z0–Z4 (merged into contour Z4+) | stays on the bowed centerline; never drifts to one side |
| Mouth center | Centerline, mouth y | Z0–Z4 (0% Z5+) | same centerline rule; the dead-center gap (I.6) stays dead-center |
| Chin apex | Centerline, chin y | Z0–Z4, Z6 (hidden Z7 by featureless sphere) | the lowest point of the V; never moves off-centerline |
| Ear tops | Brow equator, side planes | Z0–Z6 (folded to back-fuzz Z5+) | span eye-top to nose-bottom; rotate to back-fuzz past profile |

If any anchor set disagrees across two cells (e.g., the 3/4 mouth sits 3 px off where the front mouth would have slid to), the swap pops at that boundary even with a perfect crossfade.

### XII.3 The Foreshortening Math (the math of the turn)

When the head rotates yaw θ°, every front-facing feature's projected width scales as `cos(Θ)` where `Θ = θ0 + θ` is the feature's *own* total azimuth (III.4), not the raw yaw value. Concretely:

- **Inter-ocular gap** projects as `gap₀ · cos(Θ_eye)`. At Z4 profile, the two eyes merge to one (the near eye); the far eye folds behind.
- **Far-side feature widths** compress by `cos(Θ_far)` clamped at 0 past 90° (the "fold, don't squash" rule, XIV.6). This is why Z2 swaps to a *separately authored* `Eye_Far_Narrow` card rather than mathematically squeezing `Eye_Far_Front` — the foreshortening is **baked into the asset**, never computed per-frame.
- **Near-side features** compress modestly (≈7% at θ0 ≈ 22°) but compound with the segment-boundary slide and the browline arc compression to the visible ~20% (I.4).
- **Vertical (pitch)** foreshortening uses the same formula on the y term: `y = R · sin(Φ)` where `Φ = φ0 + φ` (III.4).

**The Compressed Grid is not a linear scale of the front grid.** A common defect is to take the 5-part front grid and multiply by `cos(θ)` uniformly — this overcompresses far-side features and undercompresses near-side ones. The correct procedure (I.4) is to place each feature against the *bowed Reference Cross of that cell*, using its own `θ0`, not the raw yaw.

### XII.4 Anchor-Critical vs. Bridge-Safe (the read contract)

A character is readable across 360° if and only if the **anchor-critical** silhouettes — the parts that carry the identity alone — never fully disappear. Every other feature is **bridge-safe** and may hide in the walk-behind states (|yaw| ≥ 135°) without breaking the read.

| Class | Members | Visible states | Hidden states |
|---|---|---|---|
| **Anchor-critical** | Head, Bangs, Hair (front mass), Back Hair, Ears | Z0–Z7 (Ears fold to back-fuzz Z5+) | (never fully hidden) |
| **Bridge-safe** | Eyes (both), Brows (both), Mouth, Nose, Teeth, Cheeks | Z0–Z4 | Z5, Z6, Z7 (walk-behind: all features hide) |

The contract: **a swap that hides an anchor-critical part is always a defect.** A swap that hides a bridge-safe part in a walk-behind state is correct and expected. This is why the silhouette read (I.7) is tested per-cell independently — a walk-behind cell that *should* read via silhouette alone must still pass the one-component connected-component test.

### XII.5 The Mirror-vs-Reauthor Decision Tree

For each element in each mirrored cell (Z1, Z3, Z5):

1. Is the element geometrically symmetric about the centerline (cranium circle, jaw curve, neck)? → **Mirror.** No action.
2. Is the element a paired feature with a known partner (left eye ↔ right eye)? → **Resolve to the partner's ring, mirrored.** (The P45 role split is the only slot that's role-split; every other slot is slot-for-slot L == mirror(R).)
3. Is the element an asymmetric design flag from Part 0 (ahoge, single earring, off-center part)? → **Re-author separately.** Do not mirror.
4. Is the element a hand-drawn silhouette whose back-fuzz art has a deliberate non-geometric curl or detail? → **Re-author separately.**
5. Is the element a bridge-safe feature with a Residual Correction offset (VI)? → **Mirror the geometry, recompute the correction** against the mirrored anchor.

### XII.6 Consistency Audit (per cell sign-off)

Before any cell is considered finished, run this six-point check:

1. **Reference Cross** rebuilt for that cell's threshold (I.3) — drawn, not implied.
2. **Five anchor registrations** (XII.2) land within the Residual Correction tolerance of their formula-predicted positions.
3. **Silhouette Read Test** (I.7) — one connected component, no ambiguous blob, passes for that cell specifically.
4. **Shape-contrast ratio** (XIII.3) — ~4 rounded : 1 sharp, holds for that cell.
5. **Mirror-vs-reauthor** (XII.5) — every asymmetric element correctly categorized.
6. **Depth stack** (II.2) — the cell's Z-order permutation matches the state flags the Schmitt trigger will set when the cell goes live.

---

## PART XIII — Attractiveness Engineering

Part I.7 gives the appeal rules as qualitative craft. This part grounds them in their empirical and mathematical basis, so the "why" is as checkable as the "what."

### XIII.1 Neoteny & the Baby Schema — the biological basis of cute

The **baby schema** defines the feature set that releases the caregiving response in humans: large eyes, large cranium/forehead, small nose, small mouth, short thick limbs, chubby cheeks, rounded body. Anime cute design is a literal illustration of this list — which is why the I.4 placement rules (eyes at the vertical center, tiny nose, tiny mouth clustered near the chin, large cranium) are not stylistic arbitrary choices but a direct mapping of the schema onto the canvas.

### XIII.2 The Cardioidal Strain Transformation (the math of cute)

The specific mathematical operation that turns any face realistic → cute is a **negative cardioidal strain**: features at the top of the head **expand outward and upward** (cranium enlarges, eyes enlarge); features at the bottom **contract inward and upward** (jaw narrows to a V, nose shrinks, mouth shrinks and rises). Applying the inverse (positive cardioidal strain) makes a face read as older/more threatening. This is the exact operation that separates the classical realistic canon from the anime canon:

| Feature | Realistic (classical) | Anime (cardioidal-strained) | Delta |
|---|---|---|---|
| Eye baseline y (of face height) | ~0.50 | ~0.40–0.46 | eyes drop to vertical center |
| Nose baseline y | ~0.70 | ~0.62–0.68 | shrinks, rides higher |
| Mouth baseline y | ~0.85 | ~0.74–0.82 | clusters near chin |
| Eye width : face width | 1 : 5 | 1 : 3.5–4 | eyes balloon 1.5–2× |
| Cranium : chin (vertical) | ~1.4 : 1 | ~1.6 : 1 to 2 : 1 | cranium enlarges, jaw shrinks |
| Jaw taper | soft square | pointy V | mandible narrows to apex |
| Nose size | full wedge | minuscule triangle | 80–95% reduction |
| Mouth size | full lip width | tiny shallow curve | 50–70% reduction |

The I.4 placement values (`y_eye_baseline = −0.25R`, `y_nose = −1.00R`, `y_mouth ≈ −1.28R` in the tech guide; eyes ≈ 0.44 of head height in the design guide) are the cardioidal-strained targets, not the classical realistic ones. Treat the table above as the *diagnostic* separating anime from semi-realistic — if target art drifts back toward the classical column, the character loses the cute read before any rotation logic runs.

### XIII.3 Shape Contrast (the ~4:1 round-to-sharp rule)

The appeal principle of "variety of shape" explicitly warns against "twins" (mirrored sides that read lifeless). The empirical ratio that holds across cartoon construction, cel-shaded character design, and curve-smoothing attractiveness engines is **approximately four rounded/curved forms for every one sharp/pointed form.** Too many sharp corners reads as menacing; too many rounds reads as a blob.

Applied to the head: the **rounded** set (cranium, cheek contours, iris, ear curves, hair-mass outer boundary, jaw curve) outnumbers the **sharp** set (chin V-apex, nose tip triangle, hair-tip V-terminations, brow point, ear tip) by roughly four to one. The I.7 checkable form tags each path segment by curvature variance and flags any asset that falls below 4:1.

### XIII.4 Deliberate Asymmetry (the anti-"twins" rule)

Pure bilateral symmetry falls into the "twins" anti-pattern and reads as doll-like or dead. Anime injects **controlled asymmetry** — typically one or two cues per face, no more:

- An **ahoge** (cowlick) breaking the centerline — the canonical case, a single vertical hair spike that breaks the mirror.
- Hair parted to one side; one longer lock.
- One eyelid slightly heavier than the other.
- An off-center mouth (the `Mouth_3Q` compressed-off-center shift is the in-zone version of this).
- A few degrees of brow tilt difference between left and right (I.6).

**Rule: exactly one or two asymmetry cues per face.** Enough to read as alive; not enough to read as deformed. Cross-zone consistency (I.7) requires every cell that touches the asymmetric element preserve its asymmetry — re-symmetrizing the ahoge on the 3/4 card while it stays asymmetric on the front is the classic "pop" defect.

### XIII.5 Eye Highlight Conventions (the strongest appeal lever under monoline)

Under the monoline constraint (I.1), the eye highlight is the single most powerful appeal tool, because it depends entirely on shape and placement — not on line weight or gradient. The universal anime/manga eye carries **1–3 solid white highlights** on the iris:

- **Key light:** one large highlight, upper-outer quadrant of the iris (the light source reading).
- **Rim/bounce:** one smaller highlight, lower-inner quadrant (the reflected bounce).
- **Optional tertiary:** a third micro-highlight for the "wet jewel" look (the high-sparkle portrait convention).

**Eyelash hierarchy:** every lash and outline is drawn at the single locked stroke width of I.1 — there is no second stroke weight anywhere in this rig, including here. The "anime eye" hierarchy is built from shape and coverage, not weight: the upper lash is a closed wedge shape occupying most of the lid arc (the dominant shape by area); the lower lash is a short, disconnected segment covering a fraction of that arc; the iris outline is a thin closed loop with no wedge fill at all. Contrast comes from how much shape each line encloses, never from a heavier or lighter stroke.

**Hair gloss band:** a single soft elliptical gloss patch 60–70% up the hair mass, built with the same solid-fill technique as I.1's specular highlights. Two-tier gloss (a dark base + a lighter crown band) is the cel-shaded convention. Both highlights and gloss are **solid-fill shapes**, never line modulation — this is what keeps them compatible with the camera-proximity scaling rule (III.5).

### XIII.6 The Uncanny Valley and Why No-Deformation Is Mandatory

The **uncanny valley**: as a figure approaches human likeness, empathy rises, then **crashes into revulsion** just short of full realism, then recovers. Five design rules to escape the valley, all directly governing this rig:

1. **Match realism levels.** A photoreal texture on a stylized mesh (or vice-versa) is the #1 trigger. Anime escapes by keeping *every* element at the same stylization tier — flat cel shading, uniform line, schematic features. Don't mix photographic shading into an anime head.
2. **Don't mix human proportions with stylized ones.** Cardioidal-strained proportions (large eyes, small nose/mouth) read as eerie when paired with photoreal human texture. Keep the texture flat when the proportions are stylized.
3. **Add neotenous features** to climb back out of the valley. Even "realistic" anime faces retain the large-eye/small-mouth schema — this is the *escape mechanism*, not a stylistic leftover.
4. **Appearance and motion must agree.** When an animated character looks more human than its movement, it gives a negative impression. **A 2D anime head must move like a 2D card (parallax + hard swap), not like a 3D deforming mesh.** Deforming the source art is the single most reliable way to throw an anime head into the uncanny valley — this is the core justification for the Zero-Morphing Guarantee (III.4).
5. **The eyes are the strongest uncanny trigger** — removing eyes from a realistic face is eerier than removing the nose. The eye must be flawless; everything else can be loose. This is why the eye has the most-authored variant set in the matrix (Eye_Front, Eye_Far_Narrow, Eye_Near_3Q, Eye_Far_Sliver, Eye_Profile — five variants across the yaw sweep, more than any other feature).

**The contract:** the no-deformation rule is not an aesthetic preference — it is the *empirically validated* motion model that keeps a stylized character out of the valley. Any "optimization" that deforms the art to avoid authoring a swap state trades a small asset-budget saving for a large uncanny-valley regression.

### XIII.7 Color & Tonal Stack (the limited-palette cel convention)

The cel-shaded palette uses **2–3 tones per color region**: a base, a shadow (1–2 steps darker, slightly cooler), and an optional highlight (1 step lighter, slightly warmer). This limited tonal stack is what keeps anime readable at small sprite sizes (portrait scale) and follows the outlined-form-with-flat-fill convention.

- **Skin:** warm yellow-orange base; slightly cooler shadow; optional warm highlight on cheekbone, nose bridge, chin.
- **Hair:** the *identity* color (most saturated region on the character); base + 1 shadow tone + 1 gloss band (XIII.5).
- **Eyes:** iris hue usually shared with hair or complementary; iris is a flat solid fill (I.6) — pupil darker, highlights solid white (XIII.5).
- **Line color:** not pure `#000000`. A very dark version of the fill hue (e.g. `#16181d` for neutral, `#2a1a1a` for warm skin) reads softer and more "anime." Outer silhouettes may be pure black for a graphic poster look.
- **Background:** desaturated relative to the character so the character pops (staging principle).

### XIII.8 Applicability to Emotion Assets

None of the checks above (XIII.1–XIII.6) mention emotion, which leaves it silent whether they apply to Part XVII's emotion swap assets or only to the Part XVI neutral construction. They don't apply uniformly. Non-comedic emotion assets — Sadness, Anger, Pride, Serious, and Joy/Relaxation/Defeat at Mild through High intensity — are still meant to read as the same appealing character and should pass XIII.2 (cardioidal strain), XIII.3 (shape contrast), and XIII.5 (eye highlight) the same as the neutral face. The comedic-break assets that XVII.5's Extreme tier produces (SD/chibi mode, spiral eyes, void-white rage eyes, flat "><" eyes) are deliberately breaking the model for graphic/comedic effect and are exempt by design, not oversight. Use the `appeal_checked` tag introduced in XVII.5 to record which of the two buckets each authored emotion asset falls into, so the checklist in XIII isn't silently skipped for assets that were supposed to pass it.

---

## PART XIV — Context-Aware Math Foundation

This part is the single citable reference for every formula the rig depends on. Each formula is given in canonical form, derived or justified, and paired with the **context that selects it** — which view zone, which swap state, which camera distance — because the same rig runs the formula in different configurations depending on where the character is in the rotation space.

### XIV.1 Spherical→Screen Projection (the master formula)

**Canonical form:**

```
Θ = θ0 + θ           (total azimuth: authored + live yaw)
Φ = φ0 + φ           (total elevation: authored + live pitch)

x = R · cos(Φ) · sin(Θ)
y = R · sin(Φ)
z = R · cos(Φ) · cos(Θ)
```

where `R` is the authoring radius (`R_cranium` for eyes/brows/ears/upper-projections; `R_jaw` for chin/nose/mouth — see tech guide I.6 two-radius domain model), `(θ0, φ0)` is the anchor's authored angular position on the sphere (I.5), and `(θ, φ)` is the live Master Rotation Parameter (Part 0).

**Rotation order:** yaw θ applied first (around the sphere's vertical axis), pitch φ second (around the resulting horizontal axis). This is the intrinsic Tait–Bryan `R = R_y(yaw) · R_x(pitch)` order. **The order is not a free choice** — 3D rotations don't commute, so swapping the order moves a diagonal pose (45° yaw + 30° pitch) to a different sphere point. Use this exact order everywhere: parallax (III.4), the Reference Cross (I.3), the blend space (VI).

**Context selector:** this formula is the *only* positional-displacement authority in the rig. It runs in every cell of the Part XI matrix; the only thing that changes cell-to-cell is which `(θ0, φ0)` set is active (the current cell's authored cohort) and what `(θ, φ)` is (the live rotation). It is *never* replaced by a "rotation matrix times vertex list" — that would deform the art (X.6 commandment 1).

**Pole singularity (gimbal lock):** at φ = ±90°, `cos(Φ) → 0` and all horizontal yaw parallax collapses to 0 — this is the formal justification for the Top/Bottom hard swaps (V.2, V.4). At yaw poles (Θ = 0 or 180°) the azimuth is well-defined but the feature is either at the front pole (maximum parallax velocity) or the back pole (folded/hidden).

### XIV.2 The Smoothstep Family (the easing curves)

**Canonical forms** (from the Wikipedia article, derived by solving polynomial systems with endpoint + derivative constraints):

```
S₁(t) = 3t² − 2t³                              (cubic,     C¹ continuous — slopes 0 at both ends)
S₂(t) = 6t⁵ − 15t⁴ + 10t³                      (quintic,   C² continuous — Perlin "smootherstep")
S₃(t) = −20t⁷ + 70t⁶ − 84t⁵ + 35t⁴             (septic,    C³ continuous)

general:  S_N(t) = t^(N+1) · Σ_{k=0..N} C(N+k,k)·C(2N+1, N−k)·(−t)^k
derivative: d/dt S_N(t) = (2N+1)·C(2N,N)·(t − t²)^N
```

- **C¹ vs C²:** S₁ pins zero slope at both ends (no velocity discontinuity); S₂ additionally pins zero acceleration (no jerk discontinuity). C² is visibly smoother during fast drags.
- **Frequency-domain payoff:** S₁'s Laplace transform rolls off at **60 dB/decade** vs 20 dB/decade for a Heaviside step and 40 dB/decade for linear ramp. Higher continuity ⇒ more band-limited ⇒ fewer visible "pops" or shimmering harmonics during the swap window (Nyquist–Shannon).
- **Clamping form (HLSL/GLSL):** `smoothstep(edge0, edge1, x)` = S₁(clamp((x−edge0)/(edge1−edge0), 0, 1)).
- **Inverse (cubic only):** `InvS₁(x) = ½ − sin(asin(1−2x)/3)`.

**Context selector:**
- *Crossfade α across a swap boundary (IV.0, III.6):* use S₁ at minimum; S₂ if fast drags show harmonic shimmer.
- *Per-zone parallax easing (III.2):* NOT smoothstep — the easing there is the sine itself (XIV.5). Smoothstep is for the *blend between two states*, not for the *slide within one state*.
- *Pin lag/chain decay (II.3):* S₁ on the lag offset's return-to-zero.

### XIV.3 The Directional Schmitt Trigger (state-boundary hysteresis)

**Canonical form** (generalized from the op-amp dual-threshold comparator):

```
For a state boundary at value V with half-hysteresis H:

  Switch state A → B  only when  input crosses  V + Sign·H  in the forward direction
  Switch state B → A  only when  input crosses  V − Sign·H  in the reverse direction

  where Sign = +1 for an increasing parameter, −1 for decreasing.
```

The dead zone `[V−H, V+V]` (width 2H) is the **single most important defense against state chatter** when the input jitter is comparable to the angular discretization. Inside the dead zone, the output **retains its previous value** — this is the "memory" / "bistable" property that makes a Schmitt trigger a one-bit quantizer with state, not a stateless comparator.

**This rig's parameters:** V ∈ {22.5°, 45.1°, 67.5°, 90.1°, 135°, 180°} (yaw), {45.1°, −45.1°} (pitch); H = 1.5°. So the 45.1° yaw boundary commits at 46.6° rising, de-commits at 43.6° falling — a 3°-wide band where the rig holds its current state regardless of small back-and-forth motion.

**Context selector:** every hard-swap boundary in Part XI uses this trigger. The trigger sets two things on fire: (a) the `CurrentAsset` state flag (read by the cohort swap, IV.0), and (b) the `theta_fired` runtime variable (read by the Local Delta Reset, XIV.5). Both must read from the same trigger event — never re-derive from the raw angle (tech guide II.2 fix).

### XIV.4 Parameter-Space Crossfade (speed-independent blending)

**Canonical form:**

```
α(θ) = S₁( clamp[ (θ − (B − W)) / (2W) ] )       // eased, 0..1 across ±W of boundary B

or linear:
α(θ) = clamp[ (θ − (B − W)) / (2W) ]
```

where `B` is the (Schmitt-adjusted, XIV.3) boundary angle and `W` is the half-window width (`CrossfadeHalfWindowDeg = 0.75°` in this rig). At θ = B, α = 0.5 (the equal-mix instant).

**Why it is speed-independent:** α has **no `dt` term**. The fade completes when the *angle* has swept `2W` degrees, regardless of how fast the user swept it. A flick and a slow drag both fade over the same angular sweep; retracing the path replays the exact same α curve (no lingering residue). Compare a frame-count fade `FInterpTo(α, 1, dt, speed)`: same N frames for fast and slow ⇒ fast drag snaps, slow drag lingers ⇒ visibly inconsistent.

**Wrap handling:** across the ±180° Back pair, the signed sweep must be measured the short way around (Z6 ↔ Z7) so the crossfade stays monotonic.

**Context selector:** every swap boundary in Part XI, for every layer in the cohort, uses this α. The α feeds the opacity channel of both the outgoing and incoming assets during the window: `displayed_opacity_outgoing = 1 − α`, `displayed_opacity_incoming = α`. The swap-set resolution (which asset is "incoming") is computed discretely by the Schmitt trigger (XIV.3) and **divorced from α** — otherwise two poses render simultaneously through the boundary (the "double-image" defect, X.3).

### XIV.5 Local Delta Reset (per-zone rebased sine)

**Canonical form:**

```
T(θ) = Peak · [ sin(θ) − sin(θ_a) ]      for θ ∈ [θ_a, θ_b]
```

where `θ_a` is the **zone-anchor key the Schmitt trigger actually fired at** (not the nominal threshold — `theta_fired` from XIV.3), and `Peak` is the layer's signed displacement coefficient from III.1.

**Velocity-continuity proof** (the key claim): differentiate,

```
dT/dθ = Peak · cos(θ)
```

At the zone boundary θ = θ_a, the outgoing zone's velocity is `Peak_out · cos(θ_a)`; the incoming zone's velocity is `Peak_in · cos(θ_a)`. They share the same `cos(θ_a)` factor; only `Peak` differs. Therefore:

- If `Peak_out = Peak_in` (same depth class on both sides): **exact velocity continuity**, for free.
- If `Peak` steps (e.g. a feature enters/leaves a cohort with a different coefficient): the velocity step is proportional to the Peak ratio, and because the Parameter-Space Crossfade (XIV.4) ramps the *asset* across the same boundary, the Peak change is interpolated across the window — boundary velocity inherited smoothly.

**Context selector:** every parallax layer, in every cell of Part XI. With `θ = key + fraction × HalfZoneWidth` (fraction −1..+1 from the authored pose key), this is the `RampOffset` contract: same global sine, shifted down per zone, automatically velocity-matched at every boundary.

### XIV.6 Cosine Foreshortening & the "Fold, Don't Squash" Rule

**Canonical form:**

```
w_visible(θ) = w_0 · cos(Θ)          for cos(Θ) ≥ 0
              = 0  (feature hidden)   for cos(Θ) < 0   (folded past the profile limb)

where Θ = θ0 + θ  (the feature's own total azimuth, III.4)
```

At Θ = 90° the feature is edge-on (zero projected width); past 90° it would go negative — geometrically a *fold* onto the back hemisphere. **Real 2D art cannot invert through zero**, so the rig **hides** the far-side member (visibility 0%) rather than letting it squash through — this is the "fold, don't squash" rule and the reason Z4/Z5/Z6 hide far-side pair members (XII.3, XII.4).

**Context selector:** the foreshortening is *baked into the authored asset* (the `Eye_Far_Narrow`, `Eye_Far_Sliver`, `Eye_Profile` cards of Part IV are pre-foreshortened art), never computed per-frame on a single sprite. The formula above is the *validation* that the authored widths match the geometric prediction, not a runtime deformation.

### XIV.7 Inverse Camera Proximity (clamped 1/Z)

**Canonical form:**

```
F_prox = clamp( K / max(Z_cam, Z_min),  F_min,  F_max )
S    = F_prox · [δx, δy]                    // scales the parallax output, never the source art
```

where `K` calibrates `F_prox = 1.0` at the rig's reference mid-shot distance, `Z_cam` is the current camera distance, `Z_min` is a small positive floor (not zero), and `F_max` is the largest multiplier the seam margins (II.4) can still cover.

**Why the clamps are mandatory:** `1/Z` diverges as `Z → 0⁺`. An unclamped term sends every displacement toward infinite slide and tears every seam at once on a lens-touching close-up. The `Z_min` floor (near-clip analogue) and `F_max` ceiling (seam-margin ceiling) exist specifically to prevent that. At the far end, `1/Z → 0`, so distant layers effectively don't slide — this is the mathematical reason the Back Hair/skybox layer stays stationary (the "everything in a skybox appears infinitely distant" property).

**Threshold invariance:** `F_prox` scales the *magnitude* of the parallax output only. It never changes which Schmitt boundary fires (XIV.3), because thresholds are defined in rotation-parameter degrees, never in screen distance. A close-up does not "trigger an early swap" no matter how maxed-out the parallax looks.

**Monoline protection:** `F_prox` scales `[δx, δy]` — the pin translation — *after* the formula. It must never scale the source art's stroke width; the stroke stays fixed in screen-space pixels (III.5 Screen-Space Monoline Protection). Camera dolly (feeding `F_prox`) and canvas zoom (a display-level scale on the final composite) are two different operations feeding two different systems.

### XIV.8 Context-Aware Composition (the full per-frame pipeline)

For any live `(θ, φ)` and camera `Z_cam`, the rig evaluates, in this fixed order, every visible layer:

```
1. Schmitt triggers (XIV.3) resolve → current cell of Part XI matrix
   → which asset cohort is active, which features visible, depth order

2. For each visible layer L:
   a. Look up L's (θ0, φ0) and R from the cohort data            (I.5, I.6)
   b. Spherical projection (XIV.1) → (x_L, y_L, z_L)
   c. Subtract L's front-view baseline → (δx_L, δy_L)
   d. Local Delta Reset (XIV.5) → T_L(θ) = Peak_L · [sin(θ) − sin(θ_fired)]
   e. Camera Proximity (XIV.7) → scale by F_prox
   f. Round to device pixels ONCE (Sub-Pixel Continuity, III.6)

3. For each layer crossing a boundary this frame:
   a. Parameter-Space Crossfade α (XIV.4) → opacity split outgoing/incoming
   b. Smoothstep (XIV.2) the α

4. Composite in depth order (II.1/II.2) with opacity
```

The "context-aware" part is that **step 1's resolution changes which `(θ0, φ0)` set feeds step 2**, and **step 2e's `F_prox` changes with camera**, and **step 3 only fires near boundaries**. The math is fixed; the context selects the inputs.

---

## PART XV — Atmospheric Perspective & Depth Haze

Parts II–III give the rig **parallax depth** (layers translate at different rates) but no **atmospheric depth** — the rig's Z-layers separate by motion and by Z-order alone, never by the aerial-perspective cue (distant things go lighter, cooler, lower-contrast) that every naturalistic 2D image uses to read as volumetric. This part formalizes that missing layer, on the atmospheric-veil principle of X.5: a translucent overlay whose opacity is a function of its distance from the artwork, animated per frame.

The digital analog is a **per-layer translucent haze overlay** whose opacity is a function of (a) the layer's own Z-depth and (b) a separately-animated `mist` parameter. It is **additive** to the parallax system — it composites after parallax displacement and never replaces it — and it is **optional per shot** (the monoline cel look of Part I deliberately has no atmospheric perspective; this system is for shots that need a volumetric read).

### XV.1 The Aerial-Perspective Law

The canonical model for atmospheric attenuation (from landscape painting and observational physics) is **exponential decay of contrast with distance**:

```
haze(Z) = 1 − e^(−k · Z)        // in [0, 1); k > 0 is the scattering coefficient
```

- At `Z = 0` (the layer touching the camera), `haze = 0` — no veiling, the art reads at full contrast.
- As `Z → ∞`, `haze → 1` — the layer is fully the haze color, no original contrast survives.
- `k` is the **scattering coefficient** (units: inverse depth). Small `k` = clear air (only the back layer is hazed); large `k` = thick fog (even mid layers veil). Tune `k` against the seam-margin depth (II.4) so the haze becomes visible only at depths where parallax alone starts to read as flat.

This is the formula a lifted translucent veil approximates physically: as the veil's gap grows, more of the artwork's contrast is scattered into the veil color, and the layer "melts away." The exponential is the correct mathematical form because light scattering through a uniform medium is exponential (Beer–Lambert law), and any linear or smoothstep approximation produces a haze that *looks* synthetic rather than atmospheric.

### XV.2 The Veil as a Per-Layer Compositing Operation

For each layer at Z-depth `Z_L`, the atmospheric composite is:

```
displayed_color(L) = lerp( L.source_color,  haze_color,  haze(Z_L) · mist_intensity )

// where:
//   haze_color       = the atmospheric tint (cool light grey for air;
//                       white for dense fog; warm amber for golden-hour)
//   mist_intensity   = a shot-level multiplier in [0, 1], animatable
//                       (the per-frame veil lift)
//   haze(Z_L)        = 1 − e^(−k · Z_L)    from XV.1
```

**Rules of the composite:**

1. **The veil never moves a vertex.** Like the parallax offset (III.4) and the camera-proximity scale (III.5), the haze is a per-pixel color operation on already-rasterized art. It composites *after* the line is rendered in screen-space pixels (III.5 Screen-Space Monoline Protection). It cannot thicken the stroke; it can only tint the visible pixels toward the haze color.

2. **The veil is depth-ordered, not Z-stack-reordered.** The haze composite runs in back-to-front order over the existing Z-stack (II.1) — it never promotes or demotes a layer. A layer that has promoted to stack position 1 (e.g. Back Hair at Z7, II.2) is still composited last (front-most) but still receives its own `haze(Z_L)` contribution based on its *logical* Z-depth, not its stack index. This separation is what lets a back-promoted layer read as "in front but still far" rather than snapping to full clarity.

3. **The haze color is shot-level, not layer-level.** One `haze_color` per shot, shared by every layer — only `haze(Z_L)` varies. Per-layer haze colors break the aerial-perspective illusion (the eye reads the gradient as a stack of unrelated tints, not as one medium).

### XV.3 The `mist` Parameter (the Veil Lift)

The veil's effective opacity is animated per frame by a **`mist` parameter**, normalized `[0, 1]`, that scales `mist_intensity` and may be:

- **Static** (a shot-wide constant) — e.g. `mist = 0.6` for a foggy scene, `mist = 0` for the default monoline cel look.
- **Bound to a view state** — `mist` ramps up as the head approaches a walk-behind state (Z5/Z6/Z7, XII.4), so the features "melt away" into the haze as they hide, rather than cutting to 0% visibility. This is the **"melt-away crossfade"** — the atmospheric-veil principle (X.5) applied to the rig's visibility contract.
- **Bound to camera proximity** — `mist` ramps up with `F_prox` (III.5), so a close-up in fog reads denser than a wide shot in the same fog. This is physically correct: closer camera = more fog volume in the cone = more scattering.

**The melt-away crossfade (the visibility-contract enhancement):** in Part XII.4, walk-behind states hide bridge-safe features at visibility 0%. With the veil enabled, that hide becomes a *haze-mediated dissolve*:

```
visibility(L, state) = 1.0                                           // normal state
                     = 1.0, but haze ramps to 1.0 across the swap     // walk-behind with veil
```

The feature stays at full source opacity but is fully veiled by `haze_color`, so it "melts away" rather than cutting. The Parameter-Space Crossfade (III.6, XIV.4) drives the haze ramp across the same ±0.75° window, so the dissolve is speed-independent just like a swap.

### XV.4 The "Air" Between Planes (Seam-Margin Interaction)

The optical gap between depth planes (X.1) is the "air" the rig's seam margin (II.4) leaves between Z-layers. With the veil enabled, that gap reads visibly: a layer sliding behind another during a turn trails a faint haze edge (the back layer's `haze(Z_L)` is higher than the front layer's), which is what makes the depth read volumetrically rather than as a flat collage.

**Seam-margin rule with the veil:** the 8–12% extension margin (II.4) is sized to cover the parallax displacement, *not* the haze gradient. A veiled edge can be slightly *smaller* than an unveiled edge — the haze itself softens the seam — but never zero; the stroke still needs its solid-fill backing (I.1) so the haze composites against a clean edge, not against a half-transparent line.

### XV.5 When to Use the Veil (and When Not)

The veil is a **shot-level aesthetic choice**, not a rig-level default. The default monoline cel look (Part I) deliberately has **no** atmospheric perspective — anime cel shading is high-contrast, flat, and depth is expressed entirely through parallax + cohort swap. Adding the veil to a default shot will make it read as a *different genre* (watercolor storybook, fog cutscene, memory flashback) rather than as a better anime shot.

Use the veil when:

- The shot is a non-combat cutscene where volumetric depth is wanted.
- The shot is a "memory" or "dream" sequence (the "melt away" read is the genre convention).
- The shot needs the receding walk-behind features to dissolve rather than cut (XV.3).
- The shot is a wide establishing frame where parallax alone reads as flat at distance.

Do **not** use the veil when:

- The shot is the default anime cel look — flat high-contrast is the aesthetic contract (Part I, XIII.6 uncanny-valley rule 1: match realism levels).
- The shot is at telephoto distance (III.5) — the parallax is already flattened and the veil adds nothing but mud.
- The shot is a close-up on a single feature — there is no depth to haze.

### XV.6 Sub-Pixel Jitter (the "Hand-Moved" Option)

Perfectly deterministic motion can read as mechanical; a tolerated sub-pixel position noise reads as "breathing" and is, in some aesthetics, essential to the medium (X.6). The hand-moved quality leaves a residual position jitter (sub-pixel, ~0.1–0.3 px at production resolution). The rig's math is exact by design — every formula in Part XIV is deterministic — so the jitter is offered as an **optional aesthetic overlay**, not a default:

```
displayed_offset(L) = parallax_offset(L) + jitter(L, t)

jitter(L, t) = J_amplitude · noise2D(L.seed, t · J_frequency)   // in [-J_amplitude, +J_amplitude]^2

// defaults: J_amplitude = 0 (OFF), J_frequency = 0.5 Hz
// aesthetic range when ON: J_amplitude ∈ [0.1, 0.3] px, J_frequency ∈ [0.3, 1.0] Hz
```

- **`J_amplitude = 0` by default.** The rig is exact; jitter is a creative choice.
- The jitter is a **per-layer Perlin/value noise** seeded per layer (so two layers don't move in lockstep — that reads as a bug, not as breathing). It applies to the parallax offset *after* all the Part XIV math, never to the source-art vertices (X.7 commandment 1 still holds — the art is immutable; only the pin's translation jitters).
- The jitter is **monotone per layer depth** — closer layers jitter slightly more (their parallax offset is bigger, so their absolute jitter reads at the same *relative* scale). Tie `J_amplitude` to the layer's `|Peak|` (III.1) so the jitter scales with displacement magnitude.
- If a reviewer asks why a layer is "trembling," the answer is: it's the hand-moved jitter, named, bounded, and off by default.

---

## PART XVI — Anime Girl Proportions & Personality (Owner's Preference)

This part is the **practical art reference** for an attractive anime girl character (shōjo/moe/bishōjo style). It is deliberately simple: line-art construction only, no meshing/blending, no new math. The numbers below are the consensus anime construction canon, grounded in the neoteny theory of Part XIII.

All vertical fractions are of **total head height** (0.0 = skull top, 1.0 = chin bottom) unless noted. The front view is the baseline; per-view deltas are in XVI.3.

### XVI.1 Front-View Canonical Proportions

The defining anime departure is **eyes at the vertical center** of the head (y ≈ 0.50), not the realistic upper-third (y ≈ 0.43). Every other proportion cascades from that one shift.

| Measurement | Realistic adult | **Anime default** | Moe/child extreme | Mature extreme |
|---|---|---|---|---|
| Head width : head height | 2 : 3 | **3 : 4** | 4 : 5 to 1 : 1 | 2 : 3 |
| Cranium : lower-face (neoteny ratio) | 1 : 1.6 | **1 : 1** | 1 : 0.8 | 1 : 1.4 |
| Eye baseline (vertical center of eye) | y ≈ 0.43 | **y ≈ 0.50** | y ≈ 0.55 | y ≈ 0.45 |
| Eye width : face width (5-part rule) | 1 : 5 | **1 : 4** | 1 : 3.5 | 1 : 6 |
| Eye width : eye height (aspect) | 3 : 1 | **2 : 1** | 1.4 : 1 | 3 : 1 |
| Inter-ocular gap (eye-to-eye) | 1.0 eye | **0.7–1.0 eye** | 0.7 eye | 1.0 eye |
| Nose baseline | y ≈ 0.55 | **y ≈ 0.65–0.70** | y ≈ 0.68 | y ≈ 0.60 |
| Mouth baseline | y ≈ 0.80 | **y ≈ 0.80–0.85** | y ≈ 0.85 | y ≈ 0.78 |
| Mouth width : one eye width | 0.7 | **0.3–0.5** | 0.3 | 0.6 |
| Brow-to-eye gap (head-height fraction) | 0.06 | **0.02–0.04** | 0.02 | 0.05 |
| Neck width : head width | 0.6 | **0.4** | 0.3 | 0.5 |

**Construction grid (front view):** this is I.4's 5-Part Width Rule at its anime-default proportions — five segments, not equal — `| 0.5 margin | EYE | 0.8 gap | EYE | 0.5 margin |`. It is not a different or "4-part" grid; equal-segment division of the same five parts gives the realistic 1:5 column above, not this one. Place the eye *centers* at y ≈ 0.50. The nose is a dot/triangle ≈ 0.05–0.10 of an eye-width in size at y ≈ 0.68, frequently omitted. The mouth is a short dead-center line at y ≈ 0.82, width ≈ 0.4 eye-widths.

### XVI.2 Eye Construction & Personality Signals

The eye is the single most load-bearing feature for both attractiveness (Part XIII) and personality. The **outer canthus angle** is the personality switch — and unlike anchor position, nothing in Part I, IV, or XI enforces that this angle actually carries into the independently-drawn `Eye_Near_3Q`, `Eye_Far_Sliver`, and `Eye_Profile` assets (I.6's per-zone note only requires them to obey the socketed-iris and wedge-lash rules, not the chosen canthus angle). Treat the canthus tilt chosen here as a locked value that every yaw-zone eye variant must reproduce at its own foreshortened angle — a tsurime front view drawn without a matching tilt at 3/4 and profile will read as a different, more neutral character the moment the head turns, and no other check in this manual catches that drift.

**Eye-shape variants (the personality switch):**

- **Tsurime (釣り目)** — outer corner slants **up ~10–20°** above the inner corner. Reads as **tsundere, confident, fierce, energetic.** The default for fire/lead-heroine characters.
- **Tareme (垂れ目)** — outer corner slants **down ~10–15°** below the inner corner (drooping). Reads as **gentle, shy, deredere, classic moe.** The default for sweet/soft characters.
- **Jitome (ジト目)** — half-lidded, flat top lid, narrow vertical. Reads as **bored, annoyed, kuudere, aloof.**
- **Round / mixed** — innocent, young.

**Eye anatomy ratios:**

- **Iris : eye opening** — anime default ≈ 70–85% (the colored disc nearly fills the eye; white shows only as thin crescents). Moe extreme ≈ 90–100% (no sclera visible). Realistic ≈ 50–60%.
- **Pupil : iris** — diameter ≈ 1/3 to 1/2 of iris.
- **Upper lash : lower lash coverage** — both drawn at the single monoline stroke width (I.1, XIII.5); the upper lash is a thick closed wedge shape, the lower lash is a thin open line or 2–3 small ticks at the outer corner only. The contrast is shape and fill area, not stroke weight.
- **Highlights** — **1–2 standard, 3 for maximum sparkle.** Canonical placement: one large highlight **upper-left** of the pupil, one small highlight **lower-right** (opposite corners = spherical "wet" read). Optional 3rd = a tiny secondary sparkle near the first. This is the *dekame* (star-highlight) convention of shōjo manga.

### XVI.3 Per-View Proportion Changes (the turn)

The proportions migrate as the head turns. These are the **art-directed deltas**, not the cosine-foreshortening math of XII.3 (that math governs the slide; these govern how each hand-authored card is drawn).

| View | Yaw / Pitch | What changes proportionally |
|---|---|---|
| **Front** | 0° | Baseline. Symmetric. 5-part grid (I.4, anime-default proportions) at full width, eyes at y ≈ 0.50. |
| **3/4** | ±45° | Far eye compresses to **~0.6–0.7 of near-eye width**. Inter-ocular gap narrows on the far side. Nose shifts toward the near cheek, drawn as a short wedge with a visible bridge. Far jaw shows as a contour curve. Near ear appears at center. Far brow shortens to match the compressed browline. |
| **Profile** | ±90° | **One eye** visible, drawn narrow (a slit, ~0.5 of front-view eye width). Nose protrudes as a small triangle from forehead to lip. Forehead→nose→lip→chin forms one continuous contour. Ear sits at vertical center. Neck meets the skull behind the ear. Mouth width compresses to ~0.3 eye-width. |
| **Back 3/4** | ±135° | Mostly hair silhouette. A sliver of far cheek and the far ear tip may show. Jaw is a hint only. Nose/mouth/eyes hidden (bridge-safe, XII.4). Read carried by hairstyle silhouette + back-fuzz planes. |
| **Back** | ±180° | Pure hair + neck. No facial features. Hairline visible at the nape; neck narrows up into the skull. The featureless cranium sphere of Part IV Zone 5. |
| **Top** (looking down) | pitch +90° | **Crown of hair dominates** (~60–70% of visible head). Face foreshortens to a downward wedge; you see the parting/crown and the tops of the bangs. Eyes/nose/mouth at 0% visibility (V.2). |
| **Bottom** (looking up) | pitch −90° | **Jaw, chin, nostrils dominate.** Under-plane of the nose and chin read strongly; eyes recede up and away; neck disappears behind the chin. Carried by parallax on the Under-Plane asset (V.4). |

### XVI.4 Hair Design (the silhouette carrier)

Hair is the **identity layer** — on back/back-3/4 views it carries the whole read. It is drawn as **two layers**: an outer mass (the silhouette) and a face cutout (the hole the face shows through). This is the *hair-as-annulus* rule.

**Volume:** hair makes the head **~1.10–1.25× taller** (the "hair helmet" extends 10–25% above the bare cranium). Back/back-3/4 silhouettes are almost entirely this outer mass.

**Bang types & signals:**

- **Straight across** — neat, classic, young.
- **Center-parted** — mature, balanced.
- **Side-swept** — stylish, modern, slightly mysterious.
- **Hime cut (姫カット)** — straight blunt bangs + straight side locks framing the jaw + long back. Reads as **elegant, ojou/refined, traditional.**
- **Baby bangs** — short, above the brow. Reads as **bold, quirky, retro.**

**Side locks** frame the jaw from temple to chin/shoulder, ~1 face-length long, width ≈ 0.25–0.35 of face width each.

**Ahoge (アホ毛, "cowlick / idiot hair"):** a single (or 2–3) sprout(s) rising from the crown, **~0.15–0.25 of head-height tall**, breaking the silhouette's centerline. Reads as **airheaded, clumsy, energetic, moe.** This is the canonical controlled asymmetry of Part XIII.4.

**Hair length categories (silhouette ratio = hair-length : head-height):**

- **Short** < 1.0 (above chin).
- **Bob** ≈ 1.0–1.5 (chin to shoulder).
- **Medium** ≈ 1.5–2.5 (shoulder to mid-back).
- **Long** ≈ 2.5–4.0+ (waist and beyond).
- **Twin-tail / ponytail** — styled; the silhouette sprouts a tail mass equal to ~1–2 head-heights.

### XVI.5 Mouth, Nose, Brow Micro-Features

**Nose:** front view = a dot, tiny triangle, or short shadow ≈ **0.05–0.10 of one eye-width** in size. Frequently omitted entirely. Profile = a small triangular wedge protruding ~0.05–0.08 of head-width.

**Mouth:** default width ≈ **0.3–0.5 of one eye width**. Drawn as a short dead-center line/gap with a tiny lower-lip tick. Smile = the line curves up at the ends; **corners must not exceed the inner edge of the eyes.** Open mouth = a small rounded shape (the "cat mouth" ω or 3).

**Brow:** thin — a single tapered stroke. Sits just above the upper lash (gap ≈ 0.02–0.04 head-height). Gentle peak over the outer third of the eye. **Tilt-asymmetry rule:** raising the inner end = sadness/worry; raising the outer end = confidence/anger; lowering the inner end = anger.

### XVI.6 Personality Expression Through Proportions (the dere archetypes)

The construction is a personality delivery system. Each visual choice signals a trope the audience reads instantly.

| Feature | Variant | Reads as |
|---|---|---|
| Eyes | **Tsurime** (outer up) | tsundere, confident, fierce |
| Eyes | **Tareme** (outer down) | gentle, shy, deredere, moe |
| Eyes | **Jitome** (half-lidded) | bored, kuudere, aloof |
| Chin | **Sharp / pointed V** | mature, elegant, older |
| Chin | **Round / soft** | young, cute, childlike |
| Brows | **Thick** | energetic, boyish, tomboy |
| Brows | **Thin** | delicate, refined, feminine |
| Mouth | **Small** | demure, shy |
| Mouth | **Wider** | expressive, energetic |
| Neoteny | **Big eyes + big cranium** | child, moe |
| Neoteny | **Smaller eyes + longer face** | mature, adult |

**The "dere" archetypes (visual shorthand):**

- **Tsundere** — tsurime eyes, sharp-ish chin, sometimes twin-tails, slight scowl default.
- **Yandere** — often tareme or flat eyes, long dark hair, unnaturally calm/flat mouth.
- **Kuudere** — jitome/half-lidded, blue/silver hair, minimal expression.
- **Dandere** — tareme, soft bangs covering eyes, small mouth.
- **Deredere** — round tareme, open bright smile, warm colors.
- **Himedere / ojou** — hime cut, sharp chin, tsurime, refined.

**Anime color theory (hair : eye : personality):**

- **Red / orange hair** → fiery, hot-tempered, passionate (tsundere).
- **Blue hair** → calm, cool, intelligent, aloof (kuudere). The pale-skin + blue-hair + quiet composition is the canonical kuudere template.
- **Pink hair** → cute, innocent, sweet.
- **Silver / white hair** → mysterious, otherworldly, powerful.
- **Green hair** → natural, serene, healing.
- **Blonde** → foreign, *gyaru*, or regal/noble.
- **Black** → ordinary protagonist, traditional, or mysterious.

### XVI.7 The Owner's Preference Sliders

The construction is best treated as **7 proportion sliders** the owner dials to express a specific character. Each slider has a realistic anchor, an anime default (the mid), a moe/cute extreme, and a mature extreme. Pick the value per slider; the rig's Geometry Contract (IX.2) holds at every combination.

| Slider | Realistic anchor | **Anime default** | Moe/cute extreme | Mature extreme |
|---|---|---|---|---|
| **Eye size** (eye ÷ face width) | 1/5 | **1/4** | 1/3.5 | 1/6 |
| **Chin sharpness** | rounded adult | **soft V** | round | pointed V |
| **Hair volume** (head+hair ÷ head) | 1.05 | **1.15** | 1.25 | 1.05 (flat) |
| **Brow thickness** | medium | **thin** | hair-thin | medium-thick |
| **Mouth size** (÷ eye width) | 0.7 | **0.4** | 0.3 | 0.6 |
| **Neoteny level** (cranium : lower-face) | 1 : 1.6 | **1 : 1** | 1 : 0.8 | 1 : 1.4 |
| **Neck width** (÷ head width) | 0.6 | **0.4** | 0.3 | 0.5 |

**How to use the sliders:** pick a target personality from XVI.6, then push the sliders toward the column that personality favors. A tsundere skews toward tsurime + smaller eyes + sharp chin + thicker brows + medium mouth; a moe mascot skews toward tareme + huge eyes + round chin + hair-thin brows + tiny mouth + max neoteny + thin neck. The owner's preference is literally which column each slider sits in.

### XVI.8 Construction Order (simple line-art, no meshing)

1. **Draw the cranium circle** (radius R). Mark the equator and the centerline.
2. **Mark the chin** at y = −1.5R (0.5R below the cranium bottom) — soft V for default, sharp/round per the chin slider.
3. **Draw the jaw curves** from each equator edge to the chin (Bezier, I.2).
4. **Place the eye baseline** at the vertical center (y ≈ 0.50 of total head height).
5. **Lay the 5-part grid (I.4)** at the eye line, anime-default proportions — `| 0.5 margin | EYE | 0.8 gap | EYE | 0.5 margin |`.
6. **Draw the eyes** per XVI.2 — pick the shape (tsurime/tareme/jitome) per the personality, add iris + pupil + 1–2 highlights.
7. **Add the nose dot** at y ≈ 0.68, the mouth line at y ≈ 0.82.
8. **Add the brows** just above the upper lash, thin, with the personality tilt.
9. **Draw the hair annulus** — outer mass + face cutout, volume per the slider, bang type per the personality.
10. **Add the ahoge** (if the personality calls for it).
11. **Add the neck** (width ≈ 0.4 of head width).
12. Repeat the construction at every view in XVI.3, against the Reference Cross (I.3, XII.1).

This is the entire front-view construction. No meshing, no blending, no deformation — just circles, Beziers, dots, and lines at fixed proportions.

---

## PART XVII — Expressions & Emotions

Parts VII.2–VII.4 covered visemes, blinks, and brows as the baseline expression layer driven by the phoneme/performance timeline. This part covers the **discrete emotions** — joy, anger, pride, sadness, defeat, relaxation, serious — and the **emotion effect symbols** (manpu) that overlay the face. It follows the same no-meshing, hard-swap contract as the rotation system: an emotion is a discrete feature-state the rig swaps to, and transitions between emotions are parameter-driven, exactly as the yaw/pitch swaps of Part IV.

Feature deltas below are expressed relative to the **neutral front-view construction** of Part XVI, and use the standardized facial-movement **Action Unit (AU)** shorthand where a muscle-group movement is the cleanest description. Angles are degrees-from-neutral unless noted. All vertical fractions are of head height; horizontal offsets are in eye-widths (EW).

### XVII.1 The Seven Emotions — Feature-by-Feature

#### Joy

The genuine ("Duchenne") smile: the cheek raiser (AU6) + the lip-corner puller (AU12), plus the lower-lid tightener (AU7) that closes the eye into the upward arc at high intensity.

- **Eye:** open at low intensity. At mid+, the **lower lid raises and straightens**, pushing the lower lash line up ~15–25° into a shallow upward arc. At high intensity, the eye **closes into an upward bow** (the "⌒⌒" smile-arc — a smooth arc construction, not a slit): the upper lash curves down to meet the raised lower lid, iris hidden. The fox-eye "> <" variant uses two thick half-circles.
- **Brow:** slightly raised or neutral; the outer ends bow down to follow the eye arc. Inner brow neutral, not pinched.
- **Mouth:** corners pulled **upward and outward** ~15–30° at low intensity; opens to an upward-curved horseshoe at high intensity. The cat-mouth ω (XVII.2) is the closed-mouth cute variant.
- **Nose:** no change.
- **Cheeks:** raised; may show blush at high intensity (a joy+embarrassment blend).
- **Head tilt:** often a slight ~5–10° tilt to one side for the warm variant.

#### Anger

Brows down-and-together + lid tension + mouth compression. The "hard stare."

- **Eye:** open wide (upper lid raised, sclera shows above iris) **and** narrowed simultaneously by the lower-lid tightener — the squinting stare. Iris often drawn smaller; sclera may be shadowed for the vengeful reading.
- **Brow:** **inner corners pulled down and together** ~10–20° below neutral at the inner end, creating the vertical anger crease between the brows. Outer ends angle sharply inward — the classic ">\ <" brow.
- **Mouth:** closed press (a thin line) OR the open shout (a wide downward-open shape with shark teeth at high intensity, XVII.2). Corners pulled slightly down in seething anger.
- **Nose:** nostrils flare at high intensity.
- **Cheeks:** tense, sometimes hollowed.
- **Head tilt:** chin tipped **down** ~5° to brow-stare; or tipped up for the dominant/defiant variant (the anger→pride blend).
- **Effect:** the cross-popping anger vein (XVII.2) appears at moderate+ intensity.

#### Pride

Head tilted back + small smile + an expanded, "looking down on you" posture. An asymmetric, knowing expression.

- **Eye:** open, narrow, or slightly lidded; gaze directed **upward and outward** (avoiding eye contact). The half-closed smug lid covers the top ~20% of iris.
- **Brow:** **outer ends raised** ~10°, inner ends neutral — the asymmetric "knowing" arch. Often only **one** brow raised (the sardonic single-brow raise).
- **Mouth:** asymmetric smirk — the lip-corner puller stronger on one side — OR a closed confident line with corners slightly up. Closed-mouth most common.
- **Nose:** no change; occasional slight chin-lift so the nose points up.
- **Cheeks:** slight raise, no blush.
- **Head tilt:** **~10–20° backward pitch** (the defining feature), often combined with slight yaw so the character looks over one shoulder.

#### Sadness

The signature is the **oblique inner-brow raise** (the inner brow high while the outer end slopes down) — the "puppy-dog" brow that is hard to fake and is the single most diagnostic sad cue.

- **Eye:** open, slightly droopy; lower lid may sag. Iris large/watery at high intensity; tear accumulation at the inner canthus.
- **Brow:** **inner corner raised** ~10–15° **and** pulled together/down — the entire inner brow is high while the outer end slopes down. Without this, the read is ambiguous.
- **Eyelid:** upper lid droops over the top ~30–40% of iris (the tired lid).
- **Mouth:** corners **pulled down** ~15–25°, often with a slight lip stretch → a small inverted-U, or a quivering open oval at high intensity (the "wobbly mouth").
- **Nose:** no change; a long blush-stripe down the nose bridge at intense weeping.
- **Cheeks:** drawn up slightly; no blush.
- **Head tilt:** **forward/down** ~10–20° pitch (the drooping head).

#### Defeat

The "voided" face — de-focused eyes, wavy mouth, the aura of depression. (Japanese *gakkari*, disappointment/defeat.)

- **Eye:** **de-focused / voided** — large round eyes with **no iris/highlight** (the white-eye), OR eyes replaced with flat circles, "> <" squiggles, or "= =" flat-lines. Pupils shrink to dots or vanish.
- **Brow:** flat or slightly raised in the inner corners (~5–8°), sometimes a slight \__/ shape.
- **Eyelid:** heavy droop, upper lid covers the top ~40–50%.
- **Mouth:** small open oval, **wavy/zigzag line** (the trembling mouth), or a tiny cat-mouth ω.
- **Nose:** no change.
- **Cheeks:** sunken; **dark bags under eyes** common (XVII.2).
- **Head tilt:** forward, chin near chest (~20–30° pitch); the ghost-wisp drifting from the mouth is the canonical "dead inside" symbol (XVII.2).

#### Relaxation

The signature is the **absence of tension** + a slight upward mouth corner. Closest to "contentment."

- **Eye:** **half-lidded** — upper lid covers the top ~40–50% of iris, lower lid neutral. The sleepy/satisfied eye. Often drawn as a flat horizontal line with a small upward curve at the outer edge.
- **Brow:** neutral or very slightly lowered (a soft arch), positioned slightly closer to the eye than alert-neutral.
- **Eyelid:** the lid IS the expression — the heavy droop is the defining feature.
- **Mouth:** closed, slightly parted, corners slightly up (~5–10°). The soft smile. May show a cat-mouth ω for the peaceful/cute variant.
- **Nose:** no change.
- **Cheeks:** relaxed, sometimes a faint warm blush.
- **Head tilt:** any relaxed pose — chin propped on hand (~15° yaw), or head tipped slightly back (~5–10°).

#### Serious

An **affective restraint** display — the absence of joy/sadness/anger with tightened control. The "cold/heartless" look.

- **Eye:** **narrowed, sharp** — drawn as a narrow horizontal lens (almond), iris fully visible but small, **hard outline, reduced highlight** (the "dead" eye). The dead-fish-eye variant removes the highlight entirely.
- **Brow:** **lowered and flattened** (~5–10° inner-down), straightened rather than angled. Often a single thick straight line very close to the eye.
- **Eyelid:** upper lid covers the top ~20–30%, lower lid flat.
- **Mouth:** closed thin line, corners neutral. The "dash mouth" — a single short horizontal line.
- **Nose:** no change; occasionally emphasized/sharpened in profile.
- **Cheeks:** tense, hollowed; a **shadow band under the eye** common.
- **Head tilt:** none — head dead-level, **direct forward gaze** into camera.
- **Effect:** dark shading over the upper face, "shadowed eyes" regardless of room lighting.

#### Surprise (transient — not a held emotion state)

Both the Anticipation beat and the blink-punctuation catalog (XVII.3) require this pose, but it was never actually defined as one of the seven emotions above, had no feature breakdown, and wasn't in the Emotion × View Matrix (XVII.4). It doesn't get its own intensity axis or effect-symbol set — it exists only as a brief transitional pose, never a resting state, so it needs one asset per feature at front view only (near-symmetric enough at 3/4 to reuse the front asset with the standard per-view compression, unlike the held emotions).

- **Eye:** upper lid raised well above neutral (sclera visible above iris), lower lid neutral — the "wide eye," not the anger version (no lower-lid tightening).
- **Brow:** raised toward the hairline, ~15–25° above neutral, both together (no asymmetry).
- **Mouth:** small open oval or dropped-open shape, no shout-width shark-teeth variant.
- **Duration:** held for 2–4 frames (~80–165ms at the 24fps reference, XVII.3) — this is the only emotion-adjacent pose in the manual defined by duration rather than by an intensity tier, because it is never a resting state.

### Cross-Emotion Distinguishability

Nothing above guarantees the seven held emotions read as distinct from each other. Anger and Serious are both built from a narrowed, hard-stare eye — the difference is brow angle and head-level vs. head-forward, not eye construction. Sadness and Defeat share the downcast-head, drooping-lid family, differentiated mainly by degree. Before locking a character's emotion set, hold the front-view neutral-pitch asset for every pair of emotions side by side and confirm at least two of {eye construction, brow angle, mouth shape, head tilt} differ enough to read at a glance — the Silhouette Read Test (I.7) checks that one asset reads on its own; it does not check that it reads as a *different* asset from its siblings, which is the actual job an expression set has to do.

### XVII.2 Emotion-Specific Art Elements (the Effect Symbols)

These are overlay iconography — not part of the neutral face. They are **separate art pieces** layered on top of the expression, exactly like the depth layers of Part II. All are placed in eye-width (EW) units relative to the face — but "relative to the face" needs a domain the way every other feature has one (I.5, I.6). Anchor each symbol to the nearest feature's own `(theta_0, phi_0)` coordinate in the `R_cranium` domain — sweat drop and anger vein to the near Brow's anchor, tear streams to the near Eye's — rather than a raw screen-space offset. Anchored this way they inherit the same spherical projection (III.4) as everything else, so they parallax and rotate with the head instead of visibly detaching from the face the moment the character turns.

The "viewer-facing side" swap (sweat drop and anger vein flip sides as the character turns past center, XVII.4) is the same kind of job II.2 already does for Side Hair near/far reordering with a Schmitt-trigger state flag — but effects aren't part of any declared Swap Cohort (IV.0's cohort list is Face Base/Mouth/Brow/Eyes/Projections) and aren't in the Part XI/XII matrix where thresholds live. Give the side-flip its own trigger, reusing the existing yaw-crosses-center boolean rather than inventing a new one, and fade it across a short crossfade window rather than a hard positional pop.

**Sweat drop (ase):** a single oversized teardrop shape (point up, bulb down), blue/cyan, often with a small highlight dot. Floats **above and to the side of the head** (~1.5–2 EW from the eye center), on the viewer-facing side. On a back-turned character it sits above the hair. Size ~0.5–1.0 EW tall at low intensity, up to 1.5+ EW at extreme. Meaning: embarrassment, exasperation, confusion, dismay, speechless discomfort.

**Tear drops / tear streams (namida):** smaller and more numerous than the sweat drop — small ovals or droplet shapes streaming from the inner or outer eye corner. The "single tear" originates at the **inner canthus** (sad); laughter tears originate at the **outer canthus + lower lid**. At high intensity becomes the **twin fountain** — two vertical jets arcing up from each eye, ~1–2 EW tall, sometimes pooling into a puddle below. Blue/cyan.

**Blush (sekimen):** two distinct styles.
1. **Parallel-line blush** — 2–4 short diagonal slash lines (／) under each eye, slanted ~30–45°, ~0.3–0.5 EW long. Romantic embarrassment, drunk, or fever.
2. **Solid blush patch** — a red/pink oval (or two parallel ovals) centered on the cheek ~0.5 EW below the outer eye corner. Rosy cute cheeks.

A **long-nose blush** — a red vertical stripe down the bridge of the nose connecting both cheeks — is used for intense weeping or overwhelming emotion. Red = embarrassment/shame; pink = cuteness/warmth.

**Anger vein / cross-popping vein (ikari māku):** a cruciform / quatrefoil shape — four elongated diamond/leaf shapes radiating from a center, like a stylized "#" or four-pointed starburst. Floats **above the temple/forehead**, ~1–1.5 EW from the eye on the visible side. On back-turned characters it appears above the hair. Multiple veins = greater anger; they stack. Size ~0.5–1.0 EW, bright red. Meaning: anger, irritation, frustration (rage itself uses eye changes + speed lines, not the vein).

**Fear lines / shudder lines:** 3–6 thin **vertical parallel lines** drawn over/around the head and face, often with dark shading — overlaying the hair/face like a screen-tone column. A band under the eye is the "blue/pale face" mortification marker. Meaning: fear, horror, being drained of color, shock. If the lines are **wavy** instead of straight → disgust.

**Happiness lines / laugh lines (warai-sen):** 2–3 short upward-curving lines radiating from the **outer eye corner** when the eye is drawn closed into the smile-arc (the "⌒⌒" happy closed eye). They fan outward at ~15°, ~30°, ~45° from horizontal, ~0.3–0.5 EW long. Meaning: genuine/strong happiness; they reinforce that the closed-eye is a *smile*, not a squint.

**Bags under eyes:** 2–3 short **horizontal parallel lines** directly under the lower lash line, ~0.2–0.3 EW below the eye, span ~0.5–0.8 EW wide, centered under the iris. Meaning: fatigue, defeat, illness, world-weariness — the brooding serious character's permanent marker.

**Speed lines / focus lines (shūchūsen):** radiating lines converging on a focal point (the face, a hand, an object). Straight, evenly spaced, radiating from off-panel. Variants: converging focus lines = emphasis/realization; speed lines on a character = motion, determination, combat-readiness.

**Cat mouth (neko-guchi):** a sideways "3" or Greek omega "ω" — two small humps replacing the normal mouth, slightly smaller than a neutral mouth (~0.5–0.8 of neutral width), positioned at the same vertical. Meaning: mischief, feistiness, cuteness, playful smugness; occasionally contentment. Puffed cheeks + an elongated "3" = huffy/pouting frustration (the cute-female variant).

**Shark teeth:** a row of **triangular serrated teeth** filling the mouth when open — sharp alternating triangles, like a saw blade. 5–10 equilateral triangles per row, pure white with a black outline. Meaning: mischievous/trickster grin, manic energy, comedic rage, or smug superiority.

**Spiral eyes (uzumaki-me):** each eye replaced with a spiral — Archimedean, 1.5–2 turns, ~0.5 EW diameter. Meaning: dizziness, confusion, disorientation, KO, "brain broke."

**Half-closed flat eyes (the "> <" / "= =" face):** eyes replaced with flat squiggles or bent "> <" shapes — two mirrored V-bends or two flat underscores. Meaning: nervousness, embarrassment, exasperation, suppressed frustration.

**Other notable symbols:** cross/X eyes (× ×) → dead, unconscious, KO; ellipsis (...) above the head → silence, something unsaid; a mushroom-shaped sigh exhale → awkward relief or depression; a ghost-wisp drifting from the mouth → comedic depression / figurative death.

### XVII.3 Emotion Transitions (How Features Move, When They Swap)

Emotion transitions follow the **same parameter-driven contract as rotation** (Part IV): a discrete state swap, softened by a crossfade, never a vertex morph. The timing is what differs from rotation — emotion beats are dramatic, not continuous. Part IV's crossfade widths are deliberately specified in rotation-parameter space rather than frame counts so they don't drift with playback speed (III.6); the frame counts below don't have a parameter-space equivalent to fall back on, so treat them as **milliseconds at a stated 24fps reference**, not raw engine-frame counts, and convert accordingly for any runtime that isn't locked to 24fps — otherwise the same speed-dependent popping III.6 eliminated from rotation reappears in every emotional beat.

**Snap vs. ease.** Anime **snaps** to the extreme for comedic/emotional beats (a 1-frame / ~42ms hold at the peak) and uses **eases for emotional drift** (a sadness welling-up is a 12–24 frame / ~500–1000ms ease). A neutral→rage beat is often a 1-frame snap; the "reset" snap back to neutral is also 1 frame. Decide per-transition whether the beat is comedic (snap) or emotional (ease).

**Feature lead order.** **The eyes lead** — brows + upper lids move first, ~2–4 frames (~80–165ms) ahead — then **the mouth follows**. Tear/vein/sweat effect symbols appear **after** the face has settled into the expression; they are punctuation, not anticipation.

**Anticipation (the opposite-then-emotion beat).** The eyes widen into the Surprise pose (defined above, XVII.1) for 2–4 frames (~80–165ms) **before** snapping to anger. This sells the "boiling point" beat — the face briefly visits the opposite pole before committing. This is the same anticipation principle that governs physical animation, applied to the face.

**Hold frames.** The held extreme — typically **3–8 frames (~125–330ms) on twos/threes** at the peak — is where the effect symbols appear. The hold is the punctuation.

**Blinks as emotion punctuation.** The **passing blink** (eyes close → re-open in a different expression) transitions between emotions without morphing — the closed-eye frame masks the swap, exactly like the rotation system's crossfade. A fast double-blink = surprise. A long slow blink = weariness / processing grief. A single blink after a beat = "the realization landed." This is the cleanest crossfade mechanism for emotion: the closed frame is the natural fade-to-neutral-and-back.

**Smear frames.** High-speed emotion changes may use **1 hand-drawn smear asset** depicting the face stretched toward the target expression (motion-blur style), shown for one frame before the snap-to-extreme — this is a separate authored drawing, like the effect symbols, not a runtime stretch transform applied to the existing asset (a runtime non-uniform stretch would be a vertex morph and is prohibited by X.4/X.7 #1 same as everywhere else in the rig). Use sparingly — it is a stylistic choice, not a default.

**Which emotion pairs may crossfade directly, and which need a bypass.** With seven held emotions there are 21 unordered pairs, and nothing above says which are legal direct transitions versus which must route through Neutral. Apply the same test already used for rotation's Swoosh boundary (IV.0): measure the non-overlapping outline area between the outgoing and incoming eye/mouth assets; above a set tolerance, the pair can't use a plain crossfade. Defeat's blank void-eyes into Anger's narrow hard-stare, or Joy's closed upward-bow arc into Serious's flat dead-fish-eye, are exactly the kind of pair that fails this test — bigger silhouette jumps than anything in the 8-zone rotation matrix. For any pair that fails, require either Blink-Punctuation (the closed-eye frame masks the swap the same way a rotation crossfade's overlap window does) or a two-step transition through Neutral, not a raw crossfade between the two extremes.

**The transition contract (mirrors Part IV):** each feature swap (eye shape, brow position, mouth shape) is a discrete asset swap gated by the emotion-state change, softened by one of four modes: a snap (1-frame, no crossfade), a parameter-space crossfade (the same ±window of XV.3/III.6, for pairs that pass the silhouette-delta test above), a blink-punctuation mask (mandatory for pairs that fail it), or a smear frame. All four are legal `transition.mode` values — a validator that only recognizes snap and crossfade will incorrectly reject a correctly-built blink-masked transition. The effect symbols (sweat, vein, tears) are separate overlay layers with their own fade-in timing, composited after the face settles.

**A note on the AU degree deltas in XVII.1.** Every feature delta above ("brow down 10–20°," "corners up 15–30°") is author-time drawing guidance for hand-building each emotion's swap asset — it is not a runtime transform applied to the neutral asset. This matters because rotation's legal per-frame transforms (X.4) do include uniform rotation, and reading these deltas as *runtime* rotations would look like a shortcut past hand-drawing a full separate asset per emotion. It isn't one: the swap-cohort naming (`Eye_Joy`, `Eye_Anger`, XVII.6) assumes each is its own drawing, and Emotion Pivot Anchor Uniformity (III.6) only holds if each is authored independently to share the neutral asset's anchor point — not derived from it by transform. If a future revision wants to spend the transform budget instead of the asset budget for a specific low-drama feature (brow tilt is the most plausible candidate), state that exception explicitly per-feature; don't assume it from the degree values alone.

### XVII.4 Emotion × Rotation (How Expressions Work Across Views)

An emotion must read at every view angle in the Part XI matrix. The interaction rules:

**Far eye/brow at 3/4.** The far-side brow/eye mirrors the near side but compresses. For asymmetric expressions (the pride smirk, the single raised brow), the far side is partially occluded and the asymmetric read is preserved on the near half. The far eye in a Duchenne smile still shows the up-arc lower lid, foreshortened.

**Mouth compression.**
- **Front:** mouth drawn at full symmetric width.
- **3/4:** mouth drawn at ~70–80% width; the far corner tucks under the nose shadow. The cat-mouth ω and shark teeth compress to a single visible hump/triangle row.
- **Profile:** mouth compresses to a single curve or angle — the upper lip becomes a short forward-projecting shape, the lower lip a receding line. Open-shout mouths become a single vertical open shape.

**View-dependent effect placement.** The sweat drop and anger vein are placed on the **viewer-facing side** — they swap sides when the character turns; they are not bilaterally symmetric. Blush renders on both cheeks at front, only the near cheek at 3/4/profile (the far cheek is occluded — anime rarely paints blush "wrapping around"). Tear streams originate primarily from the near eye.

**Per-view asset variants required.** Any emotion whose mouth geometry breaks at profile (open shout, shark teeth, tongue-out) needs a **dedicated P90 asset** — the front mouth shape cannot be uniformly scaled. The cat-mouth ω and most closed mouths compress acceptably and reuse one asset with a horizontal scale. Spiral/flat-eye symbols are 2D and need a per-view variant only at extreme angles.

**The emotion×view matrix:** each emotion at each of the 8 yaw zones × 3 pitch bands needs the feature variants + the effect symbols placed correctly. This is the same Full-Matrix Pre-Build of Part 0, extended by one dimension — emotion adds a third axis to the existing yaw×pitch grid.

### XVII.5 The Emotion Intensity Slider

Each emotion has an **intensity axis** — mild joy → ecstatic joy; mild annoyance → raging anger. Intensity drives which features change and when the effect symbols appear. Unlike rotation, this axis had no numeric range anywhere in either document; fix that here so it can actually use the same Schmitt-trigger contract the Glossary already claims for it.

**Define intensity as a continuous parameter `I ∈ [0.0, 1.0]`**, with tier boundaries and a hysteresis deadband exactly analogous to the rotation thresholds of IV.0:

| Tier | Range | Enter threshold | Exit threshold (±0.05 deadband) |
|---|---|---|---|
| Mild (trace) | 0.00 – 0.25 | — (default) | — |
| Moderate (marked) | 0.25 – 0.55 | 0.25 | 0.20 |
| High (severe) | 0.55 – 0.85 | 0.55 | 0.50 |
| Extreme (SD) | 0.85 – 1.00 | 0.85 | 0.80 |

Use the same Schmitt-trigger logic as IV.0: crossing into a tier fires at the enter threshold; falling back out only fires at the lower exit threshold, so a value hovering at, say, 0.55 doesn't chatter between Moderate and High. This is the numeric backbone the effect-symbol table below and the tech guide's `THRESHOLD_TABLE` lookup were referencing without ever defining.

1. **Mild (trace):** feature-only — mouth corners up 5–10°, brow down 5°. No effect symbols.
2. **Moderate (marked):** the full AU combo; **manpu appear** (sweat drop, anger vein, blush).
3. **High (severe):** the face deforms — eyes change shape (the closed-eye smile, the void-white rage eye), mouth geometry breaks (open shout, cat-mouth ω).
4. **Extreme:** **super-deformed (SD) mode** — the entire character collapses to chibi proportions; the effect symbols take over the panel (background goes solid red for rage, speed lines radiate, the character becomes a stylized icon). SD triggers at `I ≥ 0.85`, same enter/exit rule as the table above.

**Threshold table (when effect symbols appear):**

| Symbol | Appears at intensity | Emotion |
|---|---|---|
| Sweat drop | I ≥ 0.25 (mild+) | any awkwardness |
| Anger vein | I ≥ 0.25 (moderate) | anger (past annoyance) |
| Parallel-line blush | I ≥ 0.25 (moderate) | embarrassment/fever |
| Tear streams | I ≥ 0.55 (high) | sadness/joy (past watery eye) |
| Shark teeth | I ≥ 0.55 (high) | anger (manic) / mischief |
| Spiral eyes | I ≥ 0.55 (high) | confusion/dizzy |
| Nosebleed | I ≥ 0.85 (extreme) | attraction only |
| SD mode | I ≥ 0.85 (extreme) | any (comedic ceiling) |

**What still isn't covered by this fix.** Whether SD/void-eye/spiral-eye assets at the Extreme tier are exempt from the Part XIII appeal validators (Cardioidal Strain Ratio, Shape Contrast Counter, Baby-Schema Test) because they're deliberate comedic style breaks, or whether Mild/Moderate/High assets for a non-comedic emotion like Sadness or Anger are still required to pass them, is a production decision this manual doesn't make — Part XIII never mentions emotion at all. Tag each emotion asset explicitly as `appeal_checked: true/false` at authoring time rather than leaving it implicit.

### XVII.6 Construction Notes

- Each emotion is a **discrete feature-state**, authored as a separate asset variant per feature (Eye_Joy, Eye_Anger, Mouth_Joy, Mouth_Anger, Brow_*) at every yaw zone it touches. This is the same naming schema as Part VIII extended with an emotion token.
- The effect symbols are **separate overlay layers** with their own Z-depth (sweat drop above the hair, blush under the eyes, vein above the temple), added to the Part II stack as a new "effects" tier above the Primary Features. See II.2's 180° reorder note — this tier must track whichever layer currently sits at stack position 1, not a fixed index.
- The emotion state is driven by a **single emotion parameter** (analogous to the Master Rotation Parameter of Part 0) that selects the active emotion and its intensity. The swap uses the same Schmitt/hysteresis + parameter-space crossfade contract as rotation — no special-case code.
- Authoring order: neutral first (Part XVI), then the 7 emotions at front view, then per-view variants for each, then the effect symbols. Partial matrices are acceptable mid-production (the rig falls back to neutral at any emotion×view cell not yet authored).

### XVII.7 Cross-System Precedence (Rotation × Emotion × Viseme)

None of Parts IV, VII, and XVII actually run alone — a real performance turns, emotes, and talks in the same beat, and nothing elsewhere in this manual says how the three systems combine when they fire together. Resolve each frame in a fixed order, the same way II.2 fixes yaw-before-pitch for diagonal crossings:

1. **Rotation cohort** (yaw, then pitch — IV.0, II.2) resolves first, fixing which view-zone's assets and depth order are active.
2. **Emotion cohort** (XVII.3) resolves next, against that zone's assets, per the Emotion × View Matrix (XVII.4).
3. **Viseme** (VII.2) resolves last, as an override on the mouth feature only.

**Mouth-shape ownership.** Viseme and emotion both claim the mouth, and nothing before this section says who wins. Default rule: at moderate+ emotion intensity (XVII.5), the emotion's mouth shape takes precedence and the viseme is expressed as a secondary open/close modulation on top of it, rather than swapping in the full-width neutral viseme shape (an angry closed-press mouth still cycles narrower/wider on phoneme timing without becoming the plain "A" shape). At mild intensity, the ordinary viseme set governs and the emotion is carried by eyes/brows only. Either way this requires a dedicated per-emotion mouth-open/mouth-closed asset pair at minimum — not the full 5-shape viseme set crossed with all 7 emotions — so budget for it explicitly (IX.6) rather than discovering the gap mid-production.

**Head-orientation ownership.** Five of the seven emotions specify a head tilt as part of their definition (XVII.1) — Pride ~10–20° backward pitch, Sadness ~10–20° forward, Defeat ~20–30° forward — and these are pitch-rotation values, not baked-in asset tilts (per XVII.6's swap-cohort model, each emotion's Face Base is drawn at neutral pitch and the tilt is a live rotation applied on top, the same as any other pitch input). That makes pitch a second value, after mouth, that both Rotation and Emotion claim. Default rule: while a held emotion with a defined head-tilt is active, the emotion's tilt is added to (not replacing) any live pitch input, clamped to stay below Part V's 45.1° pitch hard-swap threshold regardless of how far the live input alone would otherwise push it — a High-intensity Defeat (near the top of its 20–30° range) stacked with a legitimate look-down input is exactly the case that could otherwise cross into hard-swap territory with no defined behavior. If the combined value would exceed the threshold, clamp it and let the emotion's own asset-level exaggeration (drawing the Face Base tilted further than the rotation parameter alone would justify) carry the rest of the intensity, rather than letting pitch cross a rotation boundary as a side effect of an emotion change.

**Emotion Pivot Anchor Uniformity.** Pivot Anchor Uniformity (III.6) is defined for rotation swap cohorts (`Eye_Near_Front` / `Eye_Near_3Q`) and never explicitly extended to emotion swap cohorts (`Eye_Neutral` / `Eye_Joy` / `Eye_Anger`...), which matters because of *when* each kind of swap can fire. A rotation swap only ever fires at a declared threshold — the one place Local Delta Reset (XIV.5) and Sub-Pixel Continuity actually run. An emotion swap can fire at *any* live `(θ, φ)`, including mid-slide through a parallax zone with a nonzero accumulated offset already applied to the outgoing asset. **Require every emotion variant of a feature to share the exact same rigid anchor coordinate as that feature's neutral asset** — same rule as Pivot Anchor Uniformity, extended to this axis explicitly, not left to be inferred. This is what makes it safe for the composition pipeline (XIV.8) to swap emotion assets at an arbitrary rotation value without re-deriving a Local-Delta-Reset-equivalent for the emotion axis: if the anchor is guaranteed identical, the existing rotation-driven `(delta_x, delta_y)` the pipeline already computed for the neutral asset applies unchanged to the incoming emotion asset. Without this guarantee explicitly stated and authored to, a hand-drawn `Eye_Anger` whose visual center doesn't sit exactly on `Eye_Neutral`'s Reference Cross point will pop on swap — and because `SNAP` transitions (XVII.3) have no crossfade at all, there's no fade to mask the mismatch the way a rotation boundary's minimum ±0.75° window always provides.

---

## Glossary

**Aerial perspective** — the depth cue where distant objects go lighter, cooler, lower-contrast. Modeled exponentially as `haze(Z) = 1 − e^(−k·Z)`. The basis of Part XV. (XV.1)

**Ahoge (アホ毛)** — a single (or 2–3) hair sprout(s) from the crown, ~0.15–0.25 head-heights tall, breaking the centerline. Reads as airheaded/clumsy/moe. The canonical controlled asymmetry. (XIII.4, XVI.4)

**Action Unit (AU)** — a standardized facial-muscle movement (from the Facial Action Coding System). Used in Part XVII to specify emotion feature deltas precisely (e.g. AU12 = lip-corner puller, AU4 = brow lowerer). (XVII)

**Anger vein (cross-popping vein)** — a cruciform/quatrefoil overlay of four diamond shapes above the temple, bright red, ~0.5–1.0 EW. Signals anger/irritation at moderate+ intensity. (XVII.2)

**Anchor-critical** — a part whose silhouette carries the character's identity alone (Head, Bangs, Hair, Back Hair, Ears). Never fully hidden; always rendered. Opposite of **bridge-safe**. (XII.4)

**Authoring radius (`R`)** — the sphere radius used in the spherical projection (III.4). Two domains: `R_cranium` for eyes/brows/ears/upper-projections; `R_jaw` (= 1.5·R) for chin/nose/mouth. (tech guide I.6)

**Billboard / billboarded** — a flat 2D image kept perpendicular to the camera line-of-sight. Real 2D art is *always* billboarded in this rig — the surface normal never turns edge-on. (X.2, X.6 commandment 1)

**Bishōjo (美少女)** — "pretty girl"; the anime/manga style category for attractive female characters. Round bodies, emotive rounded faces, clean circular lines, large expressive eyes. The style category this rig targets. (XVI)

**Blush (sekimen)** — the cheek redness overlay: parallel slash lines (romantic embarrassment) or a solid red/pink patch (cute warmth), ~0.5 EW below the eye. (XVII.2)

**Bridge-safe** — a part that may hide in walk-behind states without breaking the read (Eyes, Brows, Mouth, Nose, Teeth, Cheeks). (XII.4)

**Cardioidal strain** — the mathematical transformation that enlarges top-of-head features and shrinks/raises bottom ones. The operation that turns a realistic head into an anime head. (XIII.2)

**Cat mouth (neko-guchi)** — a sideways "3" or omega "ω" replacing the mouth; mischief/cuteness/playful smugness. (XVII.2)

**Cohort (swap cohort)** — every layer scheduled to change at a given threshold, swapped together in the same crossfade window. (IV.0)

**Construction Cross (Reference Cross)** — the centerline + browline, bowed per the spherical projection. The invariant against which every feature in every cell is placed. (I.3, XII.1)

**Cutout animation** — a 2D animation technique using flat articulated pieces (historically paper/card; digitally, vector paths) jointed at pins/pegs. The tradition this rig belongs to. (X)

**Dere archetypes** — the anime personality-trope shorthand expressed visually: tsundere (tsurime, fierce), yandere (flat, calm), kuudere (jitome, aloof), dandere (tareme, shy), deredere (round, bright), himedere/ojou (hime cut, refined). (XVI.6)

**Deformer** — a rig primitive that transforms a piece. A rotation transform rotates rigidly (legal); a mesh-warp transform bends the art (illegal under Zero-Morphing). (X.4)

**Hand-moved jitter** — an optional, off-by-default sub-pixel position noise on the parallax offset that reads as "breathing." The hand-moved-cutout quality, formalized in XV.6. (X.6, XV.6)

**Effect symbols (manpu)** — the overlay iconography layered on top of the expression (sweat drop, anger vein, blush, tears, etc.). Separate art pieces with their own Z-depth and fade-in timing. (XVII.2)

**Emotion parameter** — a single value selecting the active emotion and its intensity, analogous to the Master Rotation Parameter. Drives the emotion swap via the same Schmitt/crossfade contract as rotation. (XVII.6)

**Fade-then-hide** — the rule that a layer leaving the cohort fades its opacity to 0 *before* the boundary, rather than holding at alpha 0 (which wastes fill-rate and risks double-render). (X.4, X.6 commandment 9)

**Hard swap** — a discrete asset replacement at a rotation-parameter threshold, as opposed to a continuous morph. Always gated by a Schmitt trigger and softened by a parameter-space crossfade. (IV.0)

**Haze (atmospheric)** — the translucent veil compositing each layer's color toward the haze color, opacity `haze(Z) = 1 − e^(−k·Z)`, scaled by a `mist` parameter. The depth-atmosphere treatment of Part XV. (XV)

**Happiness lines (warai-sen)** — 2–3 short upward-curving lines radiating from the outer eye corner when the eye is closed into a smile-arc. Signal genuine/strong happiness. (XVII.2)

**Hime cut (姫カット)** — straight blunt bangs + straight side locks framing the jaw + long back. Reads as elegant/ojou/refined/traditional. (XVI.4)

**Hysteresis** — the property of a system whose output depends on history. In this rig, the Schmitt trigger's dead zone (±1.5°) prevents state chatter near a boundary. (XIV.3)

**Jitome (ジト目)** — half-lidded, flat-top-lid eye shape. Reads as bored/annoyed/kuudere. (XVI.2)

**Baby schema (neoteny)** — the feature set (large eyes/cranium, small nose/mouth, chubby cheeks) that releases the caregiving response. The biological basis of cute. (XIII.1)

**Local Delta Reset** — the per-zone rebasing `T(θ) = Peak·[sin(θ) − sin(θ_a)]` that zeroes each layer's translation at the zone anchor, guaranteeing velocity continuity across boundaries. (III.6, XIV.5)

**Master Rotation Parameter** — the single `(yaw, pitch)` control that drives both parallax and swaps in lockstep. There is no separate "which zone" control. (Part 0)

**Mirrored asset** — an asset produced by horizontally flipping its positive-yaw partner (`θ0 → −θ0`). The shortcut for Z1/Z3/Z5; exceptions must be re-authored. (III.3, XI.5, XII.5)

**Mist parameter (`mist`)** — a shot-level `[0, 1]` multiplier on the atmospheric haze intensity (XV.2). May be static, bound to a view state (the "melt-away"), or bound to camera proximity. Animates the veil's effective opacity per frame. (XV.3)

**Moe (萌え)** — the anime/manga concept of strong affection/cuteness release; tied to neoteny and the feeling of wanting to protect. Expressed through physical traits (large eyes, round face) or personality archetypes. (XIII.1, XVI.6)

**Monoline** — the constraint that every line is drawn at a single uniform width with no tapering. Stroke width is fixed in screen-space pixels. (I.1)

**Multiplane parallax** — the depth technique of stacking multiple independently-translatable art layers separated by an optical gap; further layers slide slower, opposing slides read as rotation. The principle behind the parallax displacement system. (X.1)

**Atmospheric veil** — a translucent overlay whose opacity ramps with depth, dissolving receding features into a haze color rather than cutting them out. The principle formalized in Part XV. (X.5, XV)

**Parameter-Space Crossfade** — a crossfade defined as a function of the rotation parameter (±0.75° window), not frame count. Speed-independent. (III.6, XIV.4)

**Parallax peak (`Peak`, `C_peak`)** — a layer's signed displacement coefficient (III.1): +150% High-Proj, +100% Nose/Bangs, +60% Primary, 0% Face Base, −50% Ears, −100% Back Hair. (III.1)

**Owner's Preference sliders** — the 7-dial construction system (eye size, chin sharpness, hair volume, brow thickness, mouth size, neoteny level, neck width) each owner dials to express a specific character. (XVI.7)

**Pin** — a rigid attachment point on a layer. Three classes: Positional (flat features), Root/Tip/Lag (projecting features with secondary motion), Chain (ribbon hair). (II.3)

**Pin rig / cutout rig** — a 2D character built as a hierarchy of pinned flat pieces, articulated by transforms on the pins (never by mesh deformation). The rig architecture this manual describes. (Part II)

**Pivot Anchor Uniformity** — the rule that every asset in a swap cohort shares the exact same rigid anchor coordinate on the skeleton (the Reference Cross point), never its bounding-box center. (III.6)

**Pose group (swap set)** — a set of parts of which only one is visible at a time; equivalent to this rig's swap cohort. (X.3)

**Residual Correction** — a per-corner offset `E = P_art − P_math` that nudges the formula's output onto a hand-drawn anchor's true position, fading to zero at the midpoint to the next corner. (VI)

**Schmitt trigger** — a dual-threshold comparator with memory. The state-machine primitive that prevents swap chatter. (XIV.3)

**Shark teeth** — triangular serrated teeth filling an open mouth (5–10 triangles per row). Signal manic energy, mischief, comedic rage. (XVII.2)

**Sweat drop (ase)** — a single oversized teardrop overlay above the temple, blue/cyan, ~0.5–1.5 EW. Signals embarrassment/exasperation/confusion. (XVII.2)

**Seam extension margin** — the 8–12% (scaled by `F_prox`) extra fill shape drawn past a layer's visible silhouette, hidden under neighbors at rest, preventing gaps during slide. (II.4)

**Silhouette Read Test** — flood-fill the construction flat black and check it reads as the character from shape alone. Must pass per-cell, not just at front view. (I.7)

**Slot** — a container of which one attachment is visible at a time; the active swap set picks which. (X.3)

**Smoothstep** — the easing family S₁/S₂/Sₙ with C¹/C²/C³ continuity. Replaces linear interpolation everywhere a crossfade is needed. (XIV.2)

**Spherical projection** — the master formula `x=R·cos(Φ)·sin(Θ), y=R·sin(Φ), z=R·cos(Φ)·cos(Θ)` that turns the live `(yaw, pitch)` into a 2D screen offset. (III.4, XIV.1)

**Sprite impostor** — a 2D image (or set of images, one per view angle) substituting for a 3D object, chosen by nearest angular cell. The graphics-engine ancestor of the hard-swap system. (X.2)

**Sub-threshold** — a swap point inside a zone (22.5°, 67.5°) rather than at its boundary. Lower visual impact than a primary threshold, identical mechanism. (Part 0, IV)

**Swoosh** — a faster structural-gap transition (front ↔ back) that bypasses the normal crossfade because a slow blend of two very different silhouettes produces an ugly hybrid. (tech guide)

**Tareme (垂れ目)** — eye shape with the outer corner slanting ~10–15° below the inner. Reads as gentle/shy/deredere/moe. (XVI.2)

**Tsurime (釣り目)** — eye shape with the outer corner slanting ~10–20° above the inner. Reads as tsundere/confident/fierce. (XVI.2)

**Swap cohort** — see Cohort.

**Top View / Bottom View** — the two dedicated pitch-endpoint assets (P+90 / P−90). Top is swap-and-stop; Bottom is swap-and-continue-parallaxing. (V.2, V.4)

**Uncanny valley** — the curve where near-human figures evoke revulsion just short of full realism. The reason no-deformation is mandatory for stylized characters. (XIII.6)

**Viseme** — a mouth shape corresponding to a phoneme (Closed, A, I, U, Neutral). Needs its own asset at every yaw zone. (VII.2)

**Walk-behind states** — Z5, Z6, Z7 (|yaw| ≥ 135°), where all bridge-safe features hide and the read is carried by silhouette alone. (XII.4)

**Yaw / Pitch** — the two rotation axes. Yaw: −180°..+180° around the vertical (0 = front, +180 = back). Pitch: −90°..+90° around the horizontal (0 = eye-level, +90 = top-down, −90 = bottom-up). (Part 0)

**Zero-Morphing Guarantee** — the absolute rule that the line art itself is never rotated in 3D, skewed, or vertex-morphed. The rotation multiplier applies only to the (x,y) translation of the pin. (III.4, X.6 commandment 1)

**Z-depth stack** — the ordered render list of layers (index 0 = top, 11 = bottom). Permutates per cell (II.2). (II.1)