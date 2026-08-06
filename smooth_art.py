#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
smooth_art.py — Art-attractiveness engine for FaceParallax placeholder art.

Converts raw polygon vertices (from FPSchematicAuthoredPoseTable) into
art-guideline-compliant SVG paths following art_guide.md Parts I.1, I.6, I.7:

  I.1 Monoline: single uniform width, no taper; depth via fill patches.
  I.6 Construction: eyes=geometric wedge+disconnected lower lash+iris+highlight;
    mouth=shallow curve with gap; hair=ribbon with S-curve; brows=single arch.
  I.7 Appeal: Curve Continuity (continuous sweeps, not stitched segments),
    Shape Contrast (4 rounded : 1 sharp), Silhouette Read, Gap Rhythm.

Core algorithm: Catmull-Rom → Cubic Bezier spline conversion with curvature-based
sharp-corner detection. Per-part feature construction for eyes, mouth, hair, brows.

Public API:
  ring_to_svg_paths(part_name, ring, canvas=1000.0)
      -> list of SVG path dicts: {d, fill, stroke, stroke_width, opacity}
      Each dict is one <path> element. Fill paths have fill=<color> stroke="none".
      Outline paths have fill="none" stroke=<color>.
"""

import math

# ---------------------------------------------------------------------------
# Catmull-Rom → Cubic Bezier conversion
# ---------------------------------------------------------------------------
def _catmull_rom_to_bezier(p0, p1, p2, p3, tension=0.5):
    """Convert one Catmull-Rom segment (p0→p1→p2→p3) to cubic Bezier control
    points for the p1→p2 segment.  tension=0.5 is the standard centripetal
    Catmull-Rom (best for avoiding self-intersections on tight curves)."""
    d1 = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    d2 = math.hypot(p2[0] - p1[0], p2[1] - p1[1])
    d3 = math.hypot(p3[0] - p2[0], p3[1] - p2[1])
    d1 = max(d1, 1e-6)
    d2 = max(d2, 1e-6)
    d3 = max(d3, 1e-6)

    alpha = tension
    d1a = d1 ** alpha
    d2a = d2 ** alpha
    d3a = d3 ** alpha

    b1x = (d1a * d1a * p0[0] - d2a * d2a * p1[0]
            + (2 * d1a * d1a + 3 * d1a * d2a + d2a * d2a) * p2[0]) \
        / (3 * d1a * (d1a + d2a)) if abs(d1a * (d1a + d2a)) > 1e-12 else p1[0]
    b1y = (d1a * d1a * p0[1] - d2a * d2a * p1[1]
            + (2 * d1a * d1a + 3 * d1a * d2a + d2a * d2a) * p2[1]) \
        / (3 * d1a * (d1a + d2a)) if abs(d1a * (d1a + d2a)) > 1e-12 else p1[1]

    b2x = (d3a * d3a * p2[0] - d2a * d2a * p1[0]
            + (2 * d3a * d3a + 3 * d3a * d2a + d2a * d2a) * p1[0]) \
        / (3 * d3a * (d3a + d2a)) if abs(d3a * (d3a + d2a)) > 1e-12 else p2[0]
    b2y = (d3a * d3a * p2[1] - d2a * d2a * p1[1]
            + (2 * d3a * d3a + 3 * d3a * d2a + d2a * d2a) * p1[1]) \
        / (3 * d3a * (d3a + d2a)) if abs(d3a * (d3a + d2a)) > 1e-12 else p2[1]

    # Simpler, more robust formula (standard centripetal Catmull-Rom):
    b1x = p1[0] + (p2[0] - p0[0]) / (6 * tension) if tension else p1[0]
    b1y = p1[1] + (p2[1] - p0[1]) / (6 * tension) if tension else p1[1]
    b2x = p2[0] - (p3[0] - p1[0]) / (6 * tension) if tension else p2[0]
    b2y = p2[1] - (p3[1] - p1[1]) / (6 * tension) if tension else p2[1]
    return (b1x, b1y), (b2x, b2y)


# ---------------------------------------------------------------------------
# Sharp-corner detection
# ---------------------------------------------------------------------------
def _interior_angle(p0, p1, p2):
    """Interior angle at vertex p1 formed by edges p0→p1 and p1→p2.
    Returns degrees in [0, 180].  Small angle = sharp corner."""
    v0 = (p0[0] - p1[0], p0[1] - p1[1])
    v1 = (p2[0] - p1[0], p2[1] - p1[1])
    dot = v0[0] * v1[0] + v0[1] * v1[1]
    m0 = math.hypot(*v0)
    m1 = math.hypot(*v1)
    if m0 < 1e-12 or m1 < 1e-12:
        return 180.0
    cos_a = max(-1.0, min(1.0, dot / (m0 * m1)))
    return math.degrees(math.acos(cos_a))


def _detect_sharp_corners(ring, threshold_deg=40.0):
    """Return set of indices where the interior angle is below threshold.
    These vertices are kept as sharp L-corners; all others get smooth curves."""
    n = len(ring)
    sharp = set()
    for i in range(n):
        angle = _interior_angle(ring[i - 1], ring[i], ring[(i + 1) % n])
        if angle < threshold_deg:
            sharp.add(i)
    return sharp


# ---------------------------------------------------------------------------
# Silhouette / clip helpers (Feature 1: iris clipped to eye outline; the same
# "virtual filled silhouette" concept Feature 3 uses for cross-layer
# occlusion in art_viewer.py).
# ---------------------------------------------------------------------------
def _is_closed_d(d):
    """True if an SVG d-string represents a closed path (ends with Z / z)."""
    s = d.rstrip()
    return bool(s) and s[-1] in ("Z", "z")


def part_silhouette_d(paths):
    """The d-string of the part's outline silhouette — the "virtual filled
    silhouette" used for cross-layer clipping. Preference order:

      1. An explicit ``silhouette`` marker on a path dict (the Eye cell's
         hidden lens-shape, combining upper + lower lash; never painted).
      2. The first ``fill="none"`` STROKE path whose d-string ends in Z
         (the Head outline, the smoothed hair ring, the upper-lash wedge,
         etc. — whatever closed outline the cell already paints).

    Fill patches and open accent strokes are skipped. Returns None if no
    silhouette can be derived."""
    for p in paths:
        if p.get("silhouette"):
            return p.get("d")
    for p in paths:
        if p.get("fill", "none") == "none" and p.get("stroke", "none") != "none":
            if _is_closed_d(p.get("d", "")):
                return p["d"]
    return None


def _scope_clip_id(clip_id, clip_scope):
    """Namespaced clip id so the same logical clip ("eye_silhouette") is
    globally unique inside a grid SVG that contains many cells. Unscoped
    when no clip_scope is given (single-cell document)."""
    if clip_scope:
        return f"{clip_scope}__{clip_id}"
    return clip_id


def collect_clip_defs(paths, clip_scope=None):
    """Collect unique (namespaced_clip_id, silhouette_d) pairs from a path
    dict list. A path contributes a clip def when it carries both
    ``clip_id`` and ``clip_d``; only the first occurrence per clip_id is
    kept (subsequent paths with the same clip_id reuse the def). Returns
    an ordered list of (id, d) tuples — empty if none."""
    seen = set()
    out = []
    for p in paths:
        cid = p.get("clip_id")
        if not cid or "clip_d" not in p:
            continue
        if cid in seen:
            continue
        seen.add(cid)
        out.append((_scope_clip_id(cid, clip_scope), p["clip_d"]))
    return out


# ---------------------------------------------------------------------------
# Path builders
# ---------------------------------------------------------------------------
def _fmt(v):
    """Format a coordinate for SVG (3 decimal places)."""
    return f"{v:.3f}"


def ring_to_smooth_path(ring, canvas=1000.0, sharp_indices=None, tension=0.5):
    """Convert a ring of (x,y) vertices in [0,1]^2 to a smooth SVG path string.

    Uses Catmull-Rom splines for smooth segments, sharp L-corners at
    identified vertices (hair tips, chin V, etc.).

    Args:
        ring: list of (x, y) in [0,1]^2
        canvas: SVG canvas size (coordinates = ring * canvas)
        sharp_indices: set of vertex indices to keep as sharp corners.
            If None, auto-detected via curvature.
        tension: Catmull-Rom tension (0.5 = standard centripetal).

    Returns:
        SVG path d-string: "M x0 y0 C ... L x1 y1 C ... Z"
    """
    n = len(ring)
    if n < 3:
        return ""

    if sharp_indices is None:
        sharp_indices = _detect_sharp_corners(ring)

    C = canvas  # shorthand

    # Clamp the Catmull-Rom BEZIER CONTROL points to the ring's bbox (+ a
    # 2% pad): when a boundary vertex sits near the card edge with uneven
    # neighbor spacing, the raw CR control points overshoot the ring and
    # the drawn curve bulges OUTSIDE the card (art-escape class). The
    # clamp keeps every curve inside the ring's own bounds — the drawn art
    # never leaves the geometry the ring defines.
    min_x = min(p[0] for p in ring)
    max_x = max(p[0] for p in ring)
    min_y = min(p[1] for p in ring)
    max_y = max(p[1] for p in ring)
    _pad = max(max_x - min_x, max_y - min_y, 1e-9) * 0.02
    _clamp_cx = (min_x - _pad, max_x + _pad)
    _clamp_cy = (min_y - _pad, max_y + _pad)

    def _clamp_pt(x, y):
        x = _clamp_cx[0] if x < _clamp_cx[0] else (_clamp_cx[1] if x > _clamp_cx[1] else x)
        y = _clamp_cy[0] if y < _clamp_cy[0] else (_clamp_cy[1] if y > _clamp_cy[1] else y)
        return x, y

    # Build the path segment-by-segment.
    # Between two sharp corners, we run a smooth Catmull-Rom chain.
    # At a sharp corner, we emit an L (line-to) to land exactly on the vertex.
    segments = []  # list of SVG path fragments

    # Find runs of smooth vertices between sharp corners.
    # We iterate around the ring.  At each sharp vertex, we break the chain.
    ordered = list(range(n))

    # Start at vertex 0 and walk forward.
    i = 0
    started = False

    while i < n:
        v = ring[i]
        sx = v[0] * C
        sy = v[1] * C

        if not started:
            segments.append(f"M {_fmt(sx)} {_fmt(sy)}")
            started = True
            i += 1
            continue

        if i in sharp_indices:
            # Sharp corner: straight line to this vertex.
            segments.append(f"L {_fmt(sx)} {_fmt(sy)}")
            i += 1
            continue

        # Smooth segment: find the run of non-sharp vertices starting at i.
        run_start = i
        while i < n and i not in sharp_indices:
            i += 1
        run_end = i  # exclusive; ring[run_end] is the next sharp (or wrap)

        # Emit Catmull-Rom splines for the run [run_start, run_end).
        # For each interior vertex in the run, we need p0, p1, p2, p3.
        # The run starts from a known vertex (the previous sharp or M start)
        # and ends at the next sharp vertex.
        run_verts = list(range(run_start, run_end))

        if len(run_verts) == 0:
            continue
        elif len(run_verts) == 1:
            # Single vertex in run — just line to it.
            v = ring[run_verts[0]]
            segments.append(f"L {_fmt(v[0]*C)} {_fmt(v[1]*C)}")
        else:
            # Multiple vertices: smooth chain.
            for k, vi in enumerate(run_verts):
                # Neighbors for Catmull-Rom:
                p0 = ring[run_verts[max(0, k - 1)]]
                p1 = ring[vi]
                p2 = ring[run_verts[min(len(run_verts) - 1, k + 1)]]
                if k + 2 < len(run_verts):
                    p3 = ring[run_verts[k + 2]]
                else:
                    # Past the run end — use the next sharp vertex or wrap.
                    next_sharp = run_end if run_end < n else run_end % n
                    p3 = ring[next_sharp]

                if k == 0:
                    # First in run: we're already at p1 via M/L.
                    # Emit the curve FROM p1 TO p2.
                    b1, b2 = _catmull_rom_to_bezier(p0, p1, p2, p3, tension)
                    b1 = _clamp_pt(*b1)
                    b2 = _clamp_pt(*b2)
                    segments.append(
                        f"C {_fmt(b1[0]*C)} {_fmt(b1[1]*C)} "
                        f"{_fmt(b2[0]*C)} {_fmt(b2[1]*C)} "
                        f"{_fmt(p2[0]*C)} {_fmt(p2[1]*C)}")
                elif k < len(run_verts) - 1:
                    # Middle of run: already arrived at p1 via previous C.
                    # Emit curve FROM p1 TO p2.
                    b1, b2 = _catmull_rom_to_bezier(p0, p1, p2, p3, tension)
                    b1 = _clamp_pt(*b1)
                    b2 = _clamp_pt(*b2)
                    segments.append(
                        f"C {_fmt(b1[0]*C)} {_fmt(b1[1]*C)} "
                        f"{_fmt(b2[0]*C)} {_fmt(b2[1]*C)} "
                        f"{_fmt(p2[0]*C)} {_fmt(p2[1]*C)}")
                # Last in run: the curve to the next sharp is handled
                # when we hit it (or at Z close).

    segments.append("Z")
    return " ".join(segments)


# ---------------------------------------------------------------------------
# Per-part feature construction
# ---------------------------------------------------------------------------
# Part classification for special handling.
# "smooth"  — generic: smooth the polygon (Head, CheekL/R, EarL/R, Chin, Neck, BackHair)
# "eye"     — upper lash wedge + disconnected lower lash + iris fill + highlight fill
# "brow"    — single smooth arch (thin uniform stroke)
# "mouth"   — shallow curve with gap (upper lip line + lower lip line, open)
# "nose"    — small triangle (stays angular per I.6)
# "bangs"   — hair ribbons: smooth outer + inner boundary + highlight fill
# "hair"    — hair mass: smooth outer + face cutout inner + highlight fill
# "teeth"   — small shape inside mouth (smooth outline)

_PART_TYPE = {
    "Head":    "smooth",
    "CheekL":  "smooth",
    "CheekR":  "smooth",
    "EarL":    "smooth",
    "EarR":    "smooth",
    "Chin":    "smooth",
    "Neck":    "smooth",
    "BackHair":"smooth",
    "EyeL":    "eye",
    "EyeR":    "eye",
    "BrowL":   "brow",
    "BrowR":   "brow",
    "Mouth":   "mouth",
    "Nose":    "nose",
    "Teeth":   "teeth",
    "Bangs":   "bangs",
    "Hair":    "hair",
}

HIGHLIGHT_COLOR = "#d0d4da"   # light grey for specular highlights (I.1, I.6)
IRIS_FILL      = "#16181d"    # dark fill for iris (solid flat, I.6)
STROKE_COLOR   = "#16181d"    # dark monoline
STROKE_WIDTH   = 5.0          # 0.5% of canvas


def _eye_silhouette_d(outer, inner, cp1_x, cp1_y, cp2_x, cp2_y, lower_pts, C):
    """The closed lens-shape EYE SILHOUETTE (Feature 1: "iris masked by the
    surrounding eye shape"). Traces the upper arch outer->inner via the same
    cubic the upper lash uses, then back inner->outer along the lower-lash
    curve. Result is a closed lens — bigger than the upper-lash wedge
    alone, so an oversized iris that would have poked below the lower lash
    is properly clipped to "only the part inside the eye outline shows"."""
    parts = [f"M {_fmt(outer[0]*C)} {_fmt(outer[1]*C)} "
             f"C {_fmt(cp1_x*C)} {_fmt(cp1_y*C)} "
             f"{_fmt(cp2_x*C)} {_fmt(cp2_y*C)} "
             f"{_fmt(inner[0]*C)} {_fmt(inner[1]*C)}"]
    if lower_pts:
        parts.append(f"L {_fmt(lower_pts[0][0]*C)} {_fmt(lower_pts[0][1]*C)}")
        if len(lower_pts) >= 3:
            p1 = lower_pts[1]
            p2 = lower_pts[2]
            parts.append(f"Q {_fmt(p1[0]*C)} {_fmt(p1[1]*C)} "
                         f"{_fmt(p2[0]*C)} {_fmt(p2[1]*C)}")
        elif len(lower_pts) == 2:
            p1 = lower_pts[1]
            parts.append(f"L {_fmt(p1[0]*C)} {_fmt(p1[1]*C)}")
        parts.append(f"L {_fmt(outer[0]*C)} {_fmt(outer[1]*C)}")
    parts.append("Z")
    return " ".join(parts)


def _build_eye_paths(ring, canvas, blink=None):
    """Eye construction per I.6:
    - Upper lash: closed geometric wedge (smooth curve for the upper arc,
      sharp corners at the outer/inner points).
    - Lower lash: disconnected line segment (smooth curve below the eye).
    - Iris: solid-fill circle tucked under the upper lash, CLIPPED to the
      eye silhouette (Feature 1: an oversized iris never extends past the
      eye outline — only the part inside the eye shows).
    - Highlight: solid light-fill shape in upper-outer quadrant, also
      clipped to the eye silhouette (a highlight outside the eye is
      nonsensical).

    The ring has 13 points: [outer_corner, 8 upper-lash points,
    inner_corner, 2 lower-lash points, (back to outer_corner)].
    Points 0-9 are the upper lash wedge.  Points 10-12 are the lower arc.

    Blink frames (VII.3): the ring is already the per-frame lid squash
    (BLINK_SCALE about the centroid); this builder gates the ART — "Closed"
    drops the iris + highlights entirely (a closed lid is a line), "Half"
    shrinks the iris to 45% and lowers it under the half-closed lid.
    """
    n = len(ring)
    C = canvas
    paths = []

    if n < 13:
        # Fallback: just smooth the whole ring.
        return [{"d": ring_to_smooth_path(ring, canvas),
                 "fill": "none", "stroke": STROKE_COLOR,
                 "stroke_width": STROKE_WIDTH}]

    # --- Upper lash (points 0-9): closed wedge, smooth arch on top ---
    # Per art_guide I.6: two SHARP corner tips (outer/inner canthus) joined
    # across the top by a smooth cubic-Bézier arch.  We build the arch as
    # a SINGLE cubic Bézier from outer to inner, with the control point
    # pushed up to match the arch height of the intermediate points.
    outer = ring[0]   # outer canthus
    inner = ring[9]   # inner canthus
    mid_pts = ring[1:9]  # 8 intermediate points defining the arch

    # Find the highest point (lowest y) in the intermediate points
    peak_y = min(p[1] for p in mid_pts)
    # Find the x-coordinate of the peak point
    peak_idx = min(range(len(mid_pts)), key=lambda i: mid_pts[i][1])
    peak_x = mid_pts[peak_idx][0]

    # Build a smooth arch: outer → Bézier peak → inner
    # The arch follows the natural curve through the peak point
    # using a cubic Bézier with control points that create a smooth arch.
    cp1_x = outer[0] + (peak_x - outer[0]) * 0.4  # control point 1: 40% toward peak
    cp1_y = outer[1] + (peak_y - outer[1]) * 1.2  # pushed up past peak for smoothness
    cp2_x = inner[0] + (peak_x - inner[0]) * 0.4  # control point 2: 40% toward peak
    cp2_y = inner[1] + (peak_y - inner[1]) * 1.2  # pushed up past peak for smoothness

    # Build path: M outer, smooth Bézier arch to inner, close wedge
    d = (f"M {_fmt(outer[0]*C)} {_fmt(outer[1]*C)} "
         f"C {_fmt(cp1_x*C)} {_fmt(cp1_y*C)} "
         f"{_fmt(cp2_x*C)} {_fmt(cp2_y*C)} "
         f"{_fmt(inner[0]*C)} {_fmt(inner[1]*C)} "
         f"Z")
    paths.append({"d": d, "fill": "none", "stroke": STROKE_COLOR,
                  "stroke_width": STROKE_WIDTH})

    # --- Lower lash (points 10-12): disconnected smooth curve ---
    lower_pts = ring[10:13]
    # Smooth curve through these 3 points (open, not closed).
    if len(lower_pts) >= 2:
        # Build a smooth open curve.
        pts = lower_pts
        d_parts = [f"M {_fmt(pts[0][0]*C)} {_fmt(pts[0][1]*C)}"]
        if len(pts) == 2:
            d_parts.append(f"L {_fmt(pts[1][0]*C)} {_fmt(pts[1][1]*C)}")
        elif len(pts) == 3:
            # Quadratic through 3 points.
            p0, p1, p2 = pts
            # Control point: intersection of tangents.
            cx = p1[0]
            cy = p1[1]
            d_parts.append(
                f"Q {_fmt(cx*C)} {_fmt(cy*C)} "
                f"{_fmt(p2[0]*C)} {_fmt(p2[1]*C)}")
        else:
            # Smooth chain.
            for k in range(1, len(pts)):
                d_parts.append(f"L {_fmt(pts[k][0]*C)} {_fmt(pts[k][1]*C)}")
        paths.append({"d": " ".join(d_parts), "fill": "none",
                      "stroke": STROKE_COLOR, "stroke_width": STROKE_WIDTH})

    # Eye silhouette d-string (Feature 1) — the lens shape used to clip the
    # iris + highlights below. Computed once; carried on each clipped path
    # via clip_d so emit_svg / paths_xml can build the <clipPath> def.
    eye_sil_d = _eye_silhouette_d(outer, inner,
                                  cp1_x, cp1_y, cp2_x, cp2_y,
                                  lower_pts, C)
    EYE_CLIP = "eye_silhouette"

    # Closed lid: the lash lines ARE the closed eye — no iris, no highlights.
    if blink == "Closed":
        return paths

    # --- Iris: solid-fill circle tucked under the upper lash ---
    # Compute iris center and radius from the ring geometry.
    # Use ALL ring points (upper + lower lash) for accurate eye opening.
    # The iris fills 70-85% of the eye opening height (XVI.2).
    xs = [p[0] for p in ring]
    ys = [p[1] for p in ring]
    eye_cx = (min(xs) + max(xs)) / 2
    eye_cy = (min(ys) + max(ys)) / 2
    eye_w = max(xs) - min(xs)
    eye_h = max(ys) - min(ys)
    iris_r = min(eye_w, eye_h) * 0.39  # iris diameter ~78% of narrower axis
    iris_cy = eye_cy - eye_h * 0.18  # shift up so top hides under upper lash
    iris_scale = 0.45 if blink == "Half" else 1.0
    iris_r *= iris_scale
    iris_cy += iris_r * (0.30 if blink == "Half" else 0.0)  # lid half-covers

    # Draw iris as a smooth circle (4 arcs).
    ir = iris_r * C
    icx = eye_cx * C
    icy = iris_cy * C
    # Approximate circle with 4 cubic arcs.
    k = 0.5523  # magic number for circle approximation
    iris_d = (
        f"M {_fmt(icx)} {_fmt(icy - ir)} "
        f"C {_fmt(icx + ir*k)} {_fmt(icy - ir)} "
        f"{_fmt(icx + ir)} {_fmt(icy - ir*k)} "
        f"{_fmt(icx + ir)} {_fmt(icy)} "
        f"C {_fmt(icx + ir)} {_fmt(icy + ir*k)} "
        f"{_fmt(icx + ir*k)} {_fmt(icy + ir)} "
        f"{_fmt(icx)} {_fmt(icy + ir)} "
        f"C {_fmt(icx - ir*k)} {_fmt(icy + ir)} "
        f"{_fmt(icx - ir)} {_fmt(icy + ir*k)} "
        f"{_fmt(icx - ir)} {_fmt(icy)} "
        f"C {_fmt(icx - ir)} {_fmt(icy - ir*k)} "
        f"{_fmt(icx - ir*k)} {_fmt(icy - ir)} "
        f"{_fmt(icx)} {_fmt(icy - ir)} Z")
    # Feature 1: the iris is CLIPPED to the eye silhouette — an oversized
    # iris never extends past the eye outline (clip_d carries the lens
    # geometry so the emitter can build <clipPath id="eye_silhouette">).
    paths.append({"d": iris_d, "fill": IRIS_FILL, "stroke": "none",
                  "stroke_width": 0,
                  "clip_id": EYE_CLIP, "clip_d": eye_sil_d})

    # --- Highlight: solid light-fill, dekame screen-frame convention (I.1,
    # I.6, XVI.2) — the key light sits UPPER-LEFT of the pupil in screen space
    # for BOTH eyes (icx - 0.3ir pushes toward -x for both the left and the
    # right eye), the rim/bounce sits LOWER-RIGHT (+x, +y). Opposite corners
    # = the spherical "wet" read; same screen side on both eyes = one light
    # source (the shōjo star-highlight convention). ---
    hl_r = iris_r * 0.42  # prominent primary highlight
    hl_cx = icx - ir * 0.28  # upper-left
    hl_cy = icy - ir * 0.32
    hl = hl_r * C
    hlx = hl_cx
    hly = hl_cy
    hl_d = (
        f"M {_fmt(hlx)} {_fmt(hly - hl)} "
        f"C {_fmt(hlx + hl*k)} {_fmt(hly - hl)} "
        f"{_fmt(hlx + hl)} {_fmt(hly - hl*k)} "
        f"{_fmt(hlx + hl)} {_fmt(hly)} "
        f"C {_fmt(hlx + hl)} {_fmt(hly + hl*k)} "
        f"{_fmt(hlx + hl*k)} {_fmt(hly + hl)} "
        f"{_fmt(hlx)} {_fmt(hly + hl)} "
        f"C {_fmt(hlx - hl*k)} {_fmt(hly + hl)} "
        f"{_fmt(hlx - hl)} {_fmt(hly + hl*k)} "
        f"{_fmt(hlx - hl)} {_fmt(hly)} "
        f"C {_fmt(hlx - hl)} {_fmt(hly - hl*k)} "
        f"{_fmt(hlx - hl*k)} {_fmt(hly - hl)} "
        f"{_fmt(hlx)} {_fmt(hly - hl)} Z")
    paths.append({"d": hl_d, "fill": HIGHLIGHT_COLOR, "stroke": "none",
                  "stroke_width": 0, "opacity": "0.85",
                  "clip_id": EYE_CLIP})

    # Small secondary highlight (lower-right, opposite corner).
    hl2_r = hl_r * 0.55  # larger secondary bounce
    hl2_cx = icx + ir * 0.22
    hl2_cy = icy + ir * 0.18
    h2 = hl2_r * C
    h2x = hl2_cx
    h2y = hl2_cy
    hl2_d = (
        f"M {_fmt(h2x)} {_fmt(h2y - h2)} "
        f"C {_fmt(h2x + h2*k)} {_fmt(h2y - h2)} "
        f"{_fmt(h2x + h2)} {_fmt(h2y - h2*k)} "
        f"{_fmt(h2x + h2)} {_fmt(h2y)} "
        f"C {_fmt(h2x + h2)} {_fmt(h2y + h2*k)} "
        f"{_fmt(h2x + h2*k)} {_fmt(h2y + h2)} "
        f"{_fmt(h2x)} {_fmt(h2y + h2)} "
        f"C {_fmt(h2x - h2*k)} {_fmt(h2y + h2)} "
        f"{_fmt(h2x - h2)} {_fmt(h2y + h2*k)} "
        f"{_fmt(h2x - h2)} {_fmt(h2y)} "
        f"C {_fmt(h2x - h2)} {_fmt(h2y - h2*k)} "
        f"{_fmt(h2x - h2*k)} {_fmt(h2y - h2)} "
        f"{_fmt(h2x)} {_fmt(h2y - h2)} Z")
    paths.append({"d": hl2_d, "fill": HIGHLIGHT_COLOR, "stroke": "none",
                  "stroke_width": 0, "opacity": "0.6",
                  "clip_id": EYE_CLIP})

    return paths


def _build_brow_paths(ring, canvas):
    """Brow construction per I.6:
    Single uniform stroke, gently arched.  The ring defines the outline;
    we smooth it into a continuous arch and render as a filled thin shape
    (to get the uniform-stroke look with a slight thickness)."""
    C = canvas
    # The brow ring is a thin closed shape (9 points defining the arch outline).
    # Smooth it, keeping the tips sharp.
    sharp = _detect_sharp_corners(ring, threshold_deg=50.0)
    d = ring_to_smooth_path(ring, canvas, sharp_indices=sharp)
    return [{"d": d, "fill": "none", "stroke": STROKE_COLOR,
             "stroke_width": STROKE_WIDTH}]


def _build_mouth_paths(ring, canvas, viseme=None):
    """Mouth construction per I.6:
    A shallow curve with a dead-center gap, no corner dots.
    The ring has 10 points forming a closed shape (upper lip + lower lip).
    We split into upper curve (points 0-4) and lower curve (points 5-9),
    each rendered as a smooth open curve with a gap at center.

    Visemes (VII.2): the ring is already the authored per-viseme shape
    (author_viseme_ring in generate_art.py — the open A/U, grin I, closed
    lip line). This builder gates the ART: the open visemes (A/U) get the
    dark interior fill (the ring's closed loop painted first, lip curves
    stroked on top) so the mouth reads OPEN.
    """
    C = canvas
    n = len(ring)
    paths = []

    if n < 10:
        return [{"d": ring_to_smooth_path(ring, canvas),
                 "fill": "none", "stroke": STROKE_COLOR,
                 "stroke_width": STROKE_WIDTH}]

    # Neutral/None: the upper lip curve IS the dead-center line; XVI.5's
    # tiny lower-lip tick tucks under its center — a shallow quadratic from
    # ring[1] through the lip-corner line at ring[2].x to ring[3].
    if viseme in (None, "Neutral"):
        tx0, ty0 = ring[1][0] * C, ring[1][1] * C
        txc, tyc = ring[2][0] * C, ring[0][1] * C
        tx1, ty1 = ring[3][0] * C, ring[3][1] * C
        paths.append({"d": (f"M {_fmt(tx0)} {_fmt(ty0)} "
                            f"Q {_fmt(txc)} {_fmt(tyc)} {_fmt(tx1)} {_fmt(ty1)}"),
                      "fill": "none", "stroke": STROKE_COLOR,
                      "stroke_width": STROKE_WIDTH})
        # Feature 3 (Issue 4): the neutral mouth's open quadratic has no
        # closed silhouette, so part_silhouette_d returns None and Mouth
        # can't occlude back layers. Add a HIDDEN closed lens-shape
        # (silhouette=True — never painted, only used for cross-layer
        # occlusion geometry). Built from the upper-lip quadratic + the
        # lower-lip ring arc closed back to the start.
        p0 = ring[1]
        p1 = ring[3]
        p2 = ring[5] if n > 5 else ring[-1]
        p3 = ring[8] if n > 8 else ring[-1]
        sil_d = (f"M {_fmt(p0[0]*C)} {_fmt(p0[1]*C)} "
                 f"Q {_fmt(ring[2][0]*C)} {_fmt(ring[0][1]*C)} "
                 f"{_fmt(p1[0]*C)} {_fmt(p1[1]*C)} "
                 f"L {_fmt(p2[0]*C)} {_fmt(p2[1]*C)} "
                 f"L {_fmt(p3[0]*C)} {_fmt(p3[1]*C)} Z")
        paths.append({"d": sil_d, "fill": "none", "stroke": "none",
                      "stroke_width": 0, "silhouette": True})
        return paths

    # Open visemes: dark interior behind the lip strokes (the mouth cavity).
    if viseme in ("A", "U"):
        paths.append({"d": ring_to_smooth_path(ring, canvas),
                      "fill": IRIS_FILL, "stroke": "none", "stroke_width": 0})

    # Upper lip: points 0-4 (left corner → center top → right corner).
    upper = ring[:5]
    # Lower lip: points 5-9 (right corner → center bottom → left corner).
    lower = ring[5:10]

    # Smooth open curve for upper lip using Catmull-Rom.
    u_pts = [(p[0], p[1]) for p in upper]
    u_d = [f"M {_fmt(u_pts[0][0]*C)} {_fmt(u_pts[0][1]*C)}"]
    for k in range(1, len(u_pts)):
        p0 = u_pts[max(0, k - 1)]
        p1 = u_pts[k]
        p2 = u_pts[min(len(u_pts) - 1, k + 1)]
        if k + 2 < len(u_pts):
            p3 = u_pts[k + 2]
        else:
            p3 = p2  # endpoint
        b1, b2 = _catmull_rom_to_bezier(p0, p1, p2, p3, tension=0.5)
        u_d.append(
            f"C {_fmt(b1[0]*C)} {_fmt(b1[1]*C)} "
            f"{_fmt(b2[0]*C)} {_fmt(b2[1]*C)} "
            f"{_fmt(p2[0]*C)} {_fmt(p2[1]*C)}")
    paths.append({"d": " ".join(u_d), "fill": "none", "stroke": STROKE_COLOR,
                  "stroke_width": STROKE_WIDTH})

    # Smooth open curve for lower lip using Catmull-Rom.
    l_pts = [(p[0], p[1]) for p in lower]
    l_d = [f"M {_fmt(l_pts[0][0]*C)} {_fmt(l_pts[0][1]*C)}"]
    for k in range(1, len(l_pts)):
        p0 = l_pts[max(0, k - 1)]
        p1 = l_pts[k]
        p2 = l_pts[min(len(l_pts) - 1, k + 1)]
        if k + 2 < len(l_pts):
            p3 = l_pts[k + 2]
        else:
            p3 = p2
        b1, b2 = _catmull_rom_to_bezier(p0, p1, p2, p3, tension=0.5)
        l_d.append(
            f"C {_fmt(b1[0]*C)} {_fmt(b1[1]*C)} "
            f"{_fmt(b2[0]*C)} {_fmt(b2[1]*C)} "
            f"{_fmt(p2[0]*C)} {_fmt(p2[1]*C)}")
    paths.append({"d": " ".join(l_d), "fill": "none", "stroke": STROKE_COLOR,
                  "stroke_width": STROKE_WIDTH})

    return paths


def _build_hair_paths(ring, canvas, part_name):
    """Hair construction per I.6 (Ribbon Theory):
    - Outer boundary: smooth S-curves between ribbon tips (sharp V-terminations).
    - Inner boundary: 10-15% outside the cranium circle (smooth curve).
    - Highlight fill: solid light-fill patch across the crown (I.1, I.6).

    The ring is the full hair outline (outer + inner as one closed polygon).
    We smooth the outer contour (keeping hair-tip vertices sharp) and add
    an inner-boundary path + highlight fill."""
    C = canvas
    paths = []

    # Detect sharp corners (hair tips have acute angles).
    sharp = _detect_sharp_corners(ring, threshold_deg=45.0)

    # Smooth the main outline.
    d = ring_to_smooth_path(ring, canvas, sharp_indices=sharp)
    paths.append({"d": d, "fill": "none", "stroke": STROKE_COLOR,
                  "stroke_width": STROKE_WIDTH})

    # Inner boundary: offset 12% inward from the top arc of the hair.
    # Compute the centroid and offset vertices toward it by 12%.
    cx = sum(p[0] for p in ring) / len(ring)
    cy = sum(p[1] for p in ring) / len(ring)
    inner_offset = 0.12  # 12% toward centroid (I.6: 10-15% outside cranium)
    inner_ring = []
    for p in ring:
        ix = p[0] + (cx - p[0]) * inner_offset
        iy = p[1] + (cy - p[1]) * inner_offset
        inner_ring.append((ix, iy))

    # Only draw the inner boundary for the upper portion (hair mass above face).
    # Find vertices above the face midline (~0.44) and along the top.
    inner_upper = []
    for i, p in enumerate(inner_ring):
        if p[1] < 0.35:  # above the brow line — top of hair mass
            inner_upper.append(p)

    if len(inner_upper) >= 3:
        inner_d = ring_to_smooth_path(inner_upper, canvas,
                                       sharp_indices=set())
        # Open curve (not closed) for the inner boundary.
        inner_d = inner_d.replace(" Z", "")
        paths.append({"d": inner_d, "fill": "none", "stroke": STROKE_COLOR,
                      "stroke_width": STROKE_WIDTH * 0.6})  # thinner inner line

    # Highlight fill: oval across the crown area (I.1, I.6).
    # Position at the top-center of the hair mass, based on the actual
    # topmost vertices (not the centroid, which includes bottom points).
    top_pts = [p for p in ring if p[1] < 0.15]
    if not top_pts:
        top_pts = sorted(ring, key=lambda p: p[1])[:max(3, len(ring)//4)]
    if top_pts:
        hx = sum(p[0] for p in top_pts) / len(top_pts)
        # Use the actual minimum Y of the top points (highest on screen).
        hy = min(p[1] for p in top_pts)
        hw = (max(p[0] for p in top_pts) - min(p[0] for p in top_pts)) * 0.4
        hh = hw * 0.3  # flat oval
        if hw < 0.01:
            hw = 0.08  # fallback minimum width
        k = 0.5523
        hx_c = hx * C
        # The gloss band must stay INSIDE the hair ring (A.10): center it one
        # full half-height below the crown so the ellipse top touches `hy`
        # exactly — the old `hy + hh*0.5` put the top at `hy - hh*0.5`,
        # spilling the fill above the silhouette.
        hy_c = (hy + hh) * C  # center one half-height below the top edge
        hw_c = hw * C
        hh_c = hh * C
        hl_d = (
            f"M {_fmt(hx_c)} {_fmt(hy_c - hh_c)} "
            f"C {_fmt(hx_c + hw_c*k)} {_fmt(hy_c - hh_c)} "
            f"{_fmt(hx_c + hw_c)} {_fmt(hy_c - hh_c*k)} "
            f"{_fmt(hx_c + hw_c)} {_fmt(hy_c)} "
            f"C {_fmt(hx_c + hw_c)} {_fmt(hy_c + hh_c*k)} "
            f"{_fmt(hx_c + hw_c*k)} {_fmt(hy_c + hh_c)} "
            f"{_fmt(hx_c)} {_fmt(hy_c + hh_c)} "
            f"C {_fmt(hx_c - hw_c*k)} {_fmt(hy_c + hh_c)} "
            f"{_fmt(hx_c - hw_c)} {_fmt(hy_c + hh_c*k)} "
            f"{_fmt(hx_c - hw_c)} {_fmt(hy_c)} "
            f"C {_fmt(hx_c - hw_c)} {_fmt(hy_c - hh_c*k)} "
            f"{_fmt(hx_c - hw_c*k)} {_fmt(hy_c - hh_c)} "
            f"{_fmt(hx_c)} {_fmt(hy_c - hh_c)} Z")
        paths.append({"d": hl_d, "fill": HIGHLIGHT_COLOR, "stroke": "none",
                      "stroke_width": 0, "opacity": "0.3"})

    return paths


def _build_profile_merge_paths(ring, canvas, mirror):
    """FaceBase Profile merge (Part IV Zone 4): at the profile states the
    Nose/Mouth/Teeth cards drop to 0% (FPSchematicLayerVisibleInState 4/8)
    and the FaceBase profile cell carries the merged contour — the nose
    bridge + tip + philtrum + the two lips added to the face line. Derived
    from the authored P90 ring's face edge (forehead -> brow -> chin), so
    the overlay tracks ring edits while the ring itself stays untouched
    (the SVG encode contract). `mirror` flips the overlay for the left-half
    Profile_L cell (the face is then on the right)."""
    C = canvas
    chin_idx = max(range(len(ring)), key=lambda i: ring[i][1])
    if chin_idx < 4:
        return []
    face = ring[1:chin_idx + 1]
    if len(face) < 3:
        return []
    brow = face[0]
    chin = face[-1]
    face_min_y = min(p[1] for p in face)

    def x_at(y):
        if y <= face_min_y:
            return face[0][0]
        for a, b in zip(face, face[1:]):
            if a[1] <= y <= b[1]:
                t = (y - a[1]) / max(b[1] - a[1], 1e-9)
                return a[0] + (b[0] - a[0]) * t
        return chin[0]

    def pt(y, dx):
        # Negative dx = protrusion OUT of the face line (toward x=0 for the
        # unmirrored left-facing profile); positive = a dip toward the skull.
        fx = x_at(y)
        return (fx + dx, y)

    # Author band: bridge dip, nose tip, philtrum settle, upper lip, mouth
    # notch, lower lip — y positions on the CURRENT authored P90 face line
    # (brow ~0.2, chin ~0.84). The Phase 3 silhouette-read gate pins these.
    pts = [brow,
           pt(0.40, +0.012),
           pt(0.52, -0.050),
           pt(0.60, +0.005),
           pt(0.665, -0.026),
           pt(0.695, +0.006),
           pt(0.735, -0.018),
           chin]
    if mirror:
        pts = [(1.0 - x, y) for x, y in pts]

    # Clamp every overlay point to stay inside the ring's face edge:
    # each pt's x must be >= the ring's face-edge x at that y, so the
    # overlay never crosses outside the cranium arc.
    face_x_at = [x_at(p[1]) for p in pts]
    pts = [(max(p[0], fx), p[1]) for p, fx in zip(pts, face_x_at)]

    # Smooth open chain using Catmull-Rom with tighter bbox clamp:
    # the overlay points are now inside the ring, but the Bezier control
    # points may still overshoot.  Clamp control points to the overlay's
    # own bbox + 2% pad so the drawn curve stays inside.
    min_x = min(p[0] for p in pts)
    max_x = max(p[0] for p in pts)
    min_y = min(p[1] for p in pts)
    max_y = max(p[1] for p in pts)
    pad = max(max_x - min_x, max_y - min_y, 1e-9) * 0.02
    clamp_lo = (min_x - pad, min_y - pad)
    clamp_hi = (max_x + pad, max_y + pad)

    def _clamp(x, y):
        return (max(clamp_lo[0], min(x, clamp_hi[0])),
                max(clamp_lo[1], min(y, clamp_hi[1])))

    d = [f"M {_fmt(pts[0][0]*C)} {_fmt(pts[0][1]*C)}"]
    for k in range(1, len(pts)):
        p0 = pts[max(0, k - 1)]
        p1 = pts[k]
        p2 = pts[min(len(pts) - 1, k + 1)]
        p3 = pts[min(len(pts) - 1, k + 2)]
        b1, b2 = _catmull_rom_to_bezier(p0, p1, p2, p3, tension=0.5)
        b1 = _clamp(*b1)
        b2 = _clamp(*b2)
        d.append(
            f"C {_fmt(b1[0]*C)} {_fmt(b1[1]*C)} "
            f"{_fmt(b2[0]*C)} {_fmt(b2[1]*C)} "
            f"{_fmt(p2[0]*C)} {_fmt(p2[1]*C)}")
    return [{"d": " ".join(d), "fill": "none", "stroke": STROKE_COLOR,
             "stroke_width": STROKE_WIDTH}]


def _build_nose_paths(ring, canvas):
    """Nose construction per I.6:
    Microscopic geometric indicator (triangle).  Stays angular —
    a tiny sharp triangle is the correct anime nose."""
    C = canvas
    # The nose ring is a 6-point polygon (small triangle).
    # Keep it angular (all sharp corners) — this IS the correct construction.
    d = ring_to_smooth_path(ring, canvas, sharp_indices=set(range(len(ring))))
    return [{"d": d, "fill": "none", "stroke": STROKE_COLOR,
             "stroke_width": STROKE_WIDTH}]


def _build_teeth_paths(ring, canvas):
    """Teeth construction: small shape inside the open mouth.
    Smooth outline, stays simple."""
    sharp = _detect_sharp_corners(ring, threshold_deg=50.0)
    d = ring_to_smooth_path(ring, canvas, sharp_indices=sharp)
    return [{"d": d, "fill": "none", "stroke": STROKE_COLOR,
             "stroke_width": STROKE_WIDTH}]


def _build_head_paths(ring, canvas, state_token=None, mirror=False):
    """Head (FaceBase) construction per art_guide I.2 / I.7 / art_tech I.2:
    Build the SVG path from geometric primitives — a perfect circular arc
    for the cranium and a smooth cubic Bézier for the jaw — so the crown
    reads as a clean circle and the jaw transitions as one continuous sweep.
    Only the chin V-apex is sharp (XIII.3 ~4:1 round:sharp).

    Geometry is derived from each ring's own vertices (not hardcoded) so
    every authored pose (P0/P45/P90/…) gets the correct shape.

    For profile/back rings where the jaw origins sit far above the cranium
    equator (non-circular silhouette), falls back to Catmull-Rom smoothing."""
    C = canvas
    k = 0.5523  # circle-approximation constant

    crown = ring[0]       # top of cranium
    chin  = ring[6]       # chin apex
    jaw_L = ring[2]       # left jaw origin
    jaw_R = ring[10]      # right jaw origin

    # Cranium center and radius
    cx = crown[0]
    cy = jaw_L[1]
    R = (jaw_R[0] - jaw_L[0]) / 2.0
    if R < 1e-9:
        R = abs(crown[1] - cy)

    # Detect front/3Q vs profile/back: front/3Q rings have jaw origins
    # sitting ON the cranium circle (within tolerance).  Profile/back/Top/
    # Bottom rings have jaw origins far from the circle (non-circular shape).
    crown_chin_dist = math.hypot(chin[0] - crown[0], chin[1] - crown[1])
    R_detect = crown_chin_dist / 2.5  # cranium radius from crown-to-chin span
    cy_detect = crown[1] + R_detect
    cx_detect = crown[0]
    d_L = math.hypot(jaw_L[0] - cx_detect, jaw_L[1] - cy_detect)
    d_R = math.hypot(jaw_R[0] - cx_detect, jaw_R[1] - cy_detect)
    jaw_err = max(abs(d_L - R_detect), abs(d_R - R_detect)) / max(R_detect, 1e-9)
    # P0: 0.00; P45: 0.18; P180: 0.17; P90+: >0.30.
    # P180 (back sphere) has symmetric wide jaw — exclude via jaw span check.
    jaw_span = (jaw_R[0] - jaw_L[0]) / max(R_detect, 1e-9)
    is_frontish = jaw_err < 0.19 and jaw_span < 2.1  # P0: 2.0; P45: 1.87; P180: 2.14

    if not is_frontish:
        # Profile/back/Top/Bottom: fall back to standard Catmull-Rom smoothing.
        sharp = _detect_sharp_corners(ring, threshold_deg=40.0)
        d = ring_to_smooth_path(ring, canvas, sharp_indices=sharp)
        paths = [{"d": d, "fill": "none", "stroke": STROKE_COLOR,
                  "stroke_width": STROKE_WIDTH}]
        # Profile merge overlay
        if state_token and state_token.startswith("Profile"):
            merge = _build_profile_merge_paths(ring, canvas, mirror)
            paths.extend(merge)
        return paths

    # --- Front/3Q: geometric construction ---
    chin_y = chin[1]
    # Cheek vertex from the ring (index 3 left / 9 right) defines the
    # cheek bulge point — CP1 aims through it for the correct contour.
    cheek_L = ring[3]  # left cheek point
    cheek_R = ring[9]  # right cheek point

    # Jaw: TWO cubic Béziers meeting at the chin apex.
    # CP1: positioned to sweep outward through the cheek contour before
    # tapering inward — this IS the cheek bulge per I.2 ("pushing slightly
    # outside the cranium's own circular drop").
    jaw_cp1_y    = cy + 0.50 * R   # below jaw origin
    # CP2: near centerline, above chin — tangent at chin ~28° off vertical
    # for a blunted V (I.2: "25-30° off vertical").
    jaw_cp2_xoff = 0.08 * R
    jaw_cp2_y    = chin_y - 0.15 * R

    def _cx(v): return v * C

    parts = []

    # M: right jaw origin
    parts.append(f"M {_fmt(_cx(jaw_R[0]))} {_fmt(_cx(jaw_R[1]))}")

    # Cranium arc: right equator → crown (quarter-circle)
    arc_cp1_y = cy - 0.75 * R
    parts.append(
        f"C {_fmt(_cx(jaw_R[0]))} {_fmt(_cx(arc_cp1_y))} "
        f"{_fmt(_cx(cx + R * k))} {_fmt(_cx(cy - R))} "
        f"{_fmt(_cx(cx))} {_fmt(_cx(cy - R))}")

    # Cranium arc: crown → left equator (quarter-circle)
    parts.append(
        f"C {_fmt(_cx(cx - R * k))} {_fmt(_cx(cy - R))} "
        f"{_fmt(_cx(jaw_L[0]))} {_fmt(_cx(arc_cp1_y))} "
        f"{_fmt(_cx(jaw_L[0]))} {_fmt(_cx(jaw_L[1]))}")

    # Jaw segment 1: left equator → chin
    # CP1 aims outward through the cheek (index 3).
    left_cp1_x = cheek_L[0] + 0.04 * R  # just past the cheek vertex
    left_cp2_x = cx - jaw_cp2_xoff
    parts.append(
        f"C {_fmt(_cx(left_cp1_x))} {_fmt(_cx(jaw_cp1_y))} "
        f"{_fmt(_cx(left_cp2_x))} {_fmt(_cx(jaw_cp2_y))} "
        f"{_fmt(_cx(chin[0]))} {_fmt(_cx(chin_y))}")

    # Jaw segment 2: chin → right equator (mirrored)
    right_cp1_x = cheek_R[0] - 0.04 * R
    right_cp2_x = cx + jaw_cp2_xoff
    parts.append(
        f"C {_fmt(_cx(right_cp2_x))} {_fmt(_cx(jaw_cp2_y))} "
        f"{_fmt(_cx(right_cp1_x))} {_fmt(_cx(jaw_cp1_y))} "
        f"{_fmt(_cx(jaw_R[0]))} {_fmt(_cx(jaw_R[1]))}")

    parts.append("Z")
    head_d = " ".join(parts)

    paths = [{"d": head_d, "fill": "none", "stroke": STROKE_COLOR,
              "stroke_width": STROKE_WIDTH}]

    # Cranium highlight (I.1, I.6) — gated on front-ish bbox (A.10)
    hl_cx, hl_cy = 0.43, 0.15
    hl_rx, hl_ry = 0.12, 0.06
    ring_min_x = min(p[0] for p in ring)
    ring_max_x = max(p[0] for p in ring)
    ring_min_y = min(p[1] for p in ring)
    ring_max_y = max(p[1] for p in ring)
    if (0.31 >= ring_min_x - 1e-9 and 0.55 <= ring_max_x + 1e-9
            and 0.09 >= ring_min_y - 1e-9 and 0.21 <= ring_max_y + 1e-9):
        hcx, hcy = hl_cx * C, hl_cy * C
        hrx, hry = hl_rx * C, hl_ry * C
        hl_d = (
            f"M {_fmt(hcx)} {_fmt(hcy - hry)} "
            f"C {_fmt(hcx + hrx*k)} {_fmt(hcy - hry)} "
            f"{_fmt(hcx + hrx)} {_fmt(hcy - hry*k)} "
            f"{_fmt(hcx + hrx)} {_fmt(hcy)} "
            f"C {_fmt(hcx + hrx)} {_fmt(hcy + hry*k)} "
            f"{_fmt(hcx + hrx*k)} {_fmt(hcy + hry)} "
            f"{_fmt(hcx)} {_fmt(hcy + hry)} "
            f"C {_fmt(hcx - hrx*k)} {_fmt(hcy + hry)} "
            f"{_fmt(hcx - hrx)} {_fmt(hcy + hry*k)} "
            f"{_fmt(hcx - hrx)} {_fmt(hcy)} "
            f"C {_fmt(hcx - hrx)} {_fmt(hcy - hry*k)} "
            f"{_fmt(hcx - hrx*k)} {_fmt(hcy - hry)} "
            f"{_fmt(hcx)} {_fmt(hcy - hry)} Z")
        paths.append({"d": hl_d, "fill": HIGHLIGHT_COLOR, "stroke": "none",
                      "stroke_width": 0, "opacity": "0.2"})

    # Profile merge overlay
    if state_token and state_token.startswith("Profile"):
        merge = _build_profile_merge_paths(ring, canvas, mirror)
        paths.extend(merge)

    return paths


def _build_smooth_paths(ring, canvas, part_name, state_token=None, mirror=False):
    """Generic smooth construction: Catmull-Rom curves for all non-sharp
    vertices, sharp corners preserved at high-curvature points."""
    sharp = _detect_sharp_corners(ring, threshold_deg=40.0)
    d = ring_to_smooth_path(ring, canvas, sharp_indices=sharp)

    paths = [{"d": d, "fill": "none", "stroke": STROKE_COLOR,
              "stroke_width": STROKE_WIDTH}]

    # FaceBase profile merge (Part IV Zone 4): the FaceBase Profile cell
    # carries the nose-bridge + lips contour (Nose/Mouth/Teeth hide there).
    # Applies to the Profile / Profile_L cells only; the overlay mirrors
    # with the cell so both reads show the merged contour on the face side.
    if part_name == "Head" and state_token and state_token.startswith("Profile"):
        merge = _build_profile_merge_paths(ring, canvas, mirror)
        paths.extend(merge)

    return paths


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------
def ring_to_svg_paths(part_name, ring, canvas=1000.0, state_token=None,
                      mirror=False):
    """Convert a part's ring to a list of SVG path dicts, following the
    art_guide's attractiveness and construction rules.

    Args:
        part_name: the part (Head/EyeL/...)
        ring: list of (x, y) in [0,1]^2
        canvas: SVG canvas size (coordinates = ring * canvas)
        state_token: the cell's state token — drives the authored art
            gates: blink frames (Closed/Half drop/shrink the iris),
            visemes (A/U add the dark mouth interior), Profile (the
            FaceBase merged contour).
        mirror: True for left-half cells (the Profile merge overlay flips).

    Returns:
        list of dicts, each with keys:
            d: SVG path d-string (coordinates in canvas units)
            fill: fill color or "none"
            stroke: stroke color or "none"
            stroke_width: stroke width (0 if no stroke)
            opacity: (optional) fill opacity
            clip_id: (optional) name of a clip group this path belongs to
                (Feature 1: the eye cell's iris + highlight paths carry
                "eye_silhouette"). The emitter wraps consecutive paths
                sharing a clip_id inside <g clip-path="url(#id)">.
            clip_d: (optional) the silhouette d-string that defines the
                clip geometry. Only needs to be on the first path of a
                clip group; the emitter builds <clipPath id="..."> from it.
            silhouette: (optional) True marks a geometry-only path that is
                never painted visibly (used by part_silhouette_d to derive
                a part's outline when no closed stroke exists).
    """
    ptype = _PART_TYPE.get(part_name, "smooth")

    if part_name == "Head":
        return _build_head_paths(ring, canvas, state_token, mirror)
    elif ptype == "eye":
        blink = state_token if state_token in ("Open", "Half", "Closed") else None
        return _build_eye_paths(ring, canvas, blink)
    elif ptype == "brow":
        return _build_brow_paths(ring, canvas)
    elif ptype == "mouth":
        viseme = state_token if state_token in (
            "A", "I", "U", "Closed", "Neutral") else None
        return _build_mouth_paths(ring, canvas, viseme)
    elif ptype == "nose":
        return _build_nose_paths(ring, canvas)
    elif ptype == "teeth":
        return _build_teeth_paths(ring, canvas)
    elif ptype == "hair":
        return _build_hair_paths(ring, canvas, part_name)
    elif ptype == "bangs":
        return _build_hair_paths(ring, canvas, part_name)
    else:
        return _build_smooth_paths(ring, canvas, part_name, state_token, mirror)


def _path_attrs_xml(p):
    """Build the attribute string for one <path> element from a path dict."""
    attrs = (
        f'd="{p["d"]}" '
        f'fill="{p.get("fill", "none")}" '
        f'stroke="{p.get("stroke", "none")}" '
        f'stroke-width="{p.get("stroke_width", 0):.1f}"'
    )
    if "opacity" in p:
        attrs += f' opacity="{p["opacity"]}"'
    if p.get("stroke", "none") != "none":
        attrs += ' stroke-linecap="round" stroke-linejoin="round"'
    return attrs


def render_paths_xml(paths, indent="  ", clip_scope=None):
    """Render a path-dict list as SVG <path> elements, grouping consecutive
    paths that share a ``clip_id`` inside ``<g mask="url(#...)">``.
    Feature-1 mask grouping (the iris + highlights sit inside such a group;
    the lash strokes outside it). ``clip_scope`` namespaces every mask id
    so a grid SVG containing many cells keeps IDs globally unique. Paths
    flagged ``silhouette=True`` are emitted as hidden ``<path
    id="silhouette">`` elements — never painted, but findable by
    part_silhouette_d for cross-layer occlusion geometry (Issue 4:
    Mouth neutral's closed lens shape)."""
    out = []
    i = 0
    n = len(paths)
    while i < n:
        p = paths[i]
        if p.get("silhouette"):
            out.append(f'{indent}<path id="silhouette" d="{p["d"]}" '
                       f'fill="none" stroke="none"/>')
            i += 1
            continue
        cid = p.get("clip_id")
        if not cid:
            out.append(f"{indent}<path {_path_attrs_xml(p)}/>")
            i += 1
            continue
        # Run of consecutive paths sharing the same clip_id.
        scoped = _scope_clip_id(cid, clip_scope)
        out.append(f'{indent}<g mask="url(#{scoped})">')
        while i < n and paths[i].get("clip_id") == cid:
            if not paths[i].get("silhouette"):
                out.append(f"{indent}  <path {_path_attrs_xml(paths[i])}/>")
            i += 1
        out.append(f"{indent}</g>")
    return "\n".join(out)


def render_defs_xml(paths, indent="  ", clip_scope=None, canvas=1000.0):
    """Render a <defs> block containing one containment <mask> per unique
    clip_id found among ``paths``. The mask geometry comes from the first
    path carrying both ``clip_id`` and ``clip_d`` (collect_clip_defs). The
    silhouette is painted WHITE on the mask's implicit black background —
    white = opaque (visible), black = transparent (hidden), so the masked
    content shows only inside the silhouette. Returns an empty string if
    no mask defs are present."""
    defs = collect_clip_defs(paths, clip_scope)
    if not defs:
        return ""
    cv = int(canvas)
    lines = [f"{indent}<defs>"]
    for cid, d in defs:
        lines.append(
            f'{indent}  <mask id="{cid}" maskUnits="userSpaceOnUse" '
            f'x="0" y="0" width="{cv}" height="{cv}">')
        lines.append(f'{indent}    <path d="{d}" fill="white"/>')
        lines.append(f"{indent}  </mask>")
    lines.append(f"{indent}</defs>")
    return "\n".join(lines)


def emit_svg(part, state_token, yaw_token, pitch_token, paths,
             canvas=1000.0, clip_scope=None):
    """Build a complete SVG string from a list of path dicts. Emits a
    <defs> block with one containment <mask> per unique clip_id (Feature
    1: the eye cell's lens-shape silhouette painted white-on-black so the
    iris + highlights show only inside the eye) and wraps the masked
    paths in <g mask="url(#...)">. ``clip_scope`` namespacing keeps IDs
    unique when the same paths are inlined into a multi-cell grid SVG."""
    defs_xml = render_defs_xml(paths, indent="  ", clip_scope=clip_scope,
                               canvas=canvas)
    paths_xml = render_paths_xml(paths, indent="  ", clip_scope=clip_scope)
    defs_block = (defs_xml + "\n") if defs_xml else ""
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- {part} / {yaw_token} {pitch_token} ({state_token}) — FaceParallax placeholder art.
     Generated by generate_art.py (smooth_art engine) from
     FPSchematicAuthoredPoseTable() in FaceParallaxSchematic.h.
     Vector line art on the Part I geometry canvas [0,1]^2 scaled to
     {int(canvas)}x{int(canvas)} (Y down).  Curves follow art_guide.md
     I.1/I.6/I.7 (monoline, construction geometry, appeal principles).
     Feature 1: any iris / highlight paths are masked to the eye
     silhouette via <mask> (white-on-black containment) so an oversized
     iris never extends past the eye outline. -->
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {int(canvas)} {int(canvas)}">
{defs_block}{paths_xml}
</svg>
"""
