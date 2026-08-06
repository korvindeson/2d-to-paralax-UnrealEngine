#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_art.py — FaceParallax placeholder ART LIBRARY generator (vector only).

The user's task (clarified): "import generated vector art into Unreal; show
the vector character assembled in the widget, rotatable via yaw/pitch sliders
with per-state auto part sync; click any part to replace exactly that piece
with your own vector art; discard restores default." This script is the
single generator for the vector placeholder library.

Answers to the scoping questions:

  * NO raster — every art piece is VECTOR SVG line art (infinitely zoomable).
  * ALL views per the art-guide grid — one folder per FEATURE token
    (art_guide Part VIII: `$Feature_$State_Y$YawZone_P$PitchZone`), the
    24-cell rig grid (Part VI / XI.3 / XI.5): yaw {Y00,Y22,Y45,Y67,Y90,
    Y135,Y180} x pitch {P00,P45,Pn45} + the Y00 P90 Top cell + the
    REQUIRED Y00 Pn45 UnderPlane cell (Part V.4 / XI.2 — the Bottom hard
    swap at -45.1 deg is carried by the Pn45 asset, parallax stretches it
    to -90). P20/Pn20 are parallax tokens, NOT asset corners (V.1), so no
    P20/Pn20 files exist.
  * The Y22/Y67 sub-rows exist ONLY for Eye_Near / Eye_Far / Proj (XI.5) —
    every other feature resolves its coarse cell for those bands.
  * AUTHOR RIGHT-HALF ONLY (Part III.3: "full asset sets are authored for
    0 -> +180 deg yaw only; the -180 -> 0 range is produced by horizontally
    mirroring"). The per-feature GRID files (Art/_grids/<Feature>.svg, one
    <g id="<full cell key>"> per cell) contain the mirrored left cells so a
    single import produces the complete Cells map.

Feature tokens (mirror of FPSvg::FeatureTable in FaceParallaxSvgParse.h):
  Head->FaceBase, Nose->Nose, Bangs->HairFront, Hair->HairBack,
  BackHair->BackHair, EyeR->Eye_Near, EyeL->Eye_Far,
  BrowR->Brow_Near, BrowL->Brow_Far, CheekR->Cheek_Near, CheekL->Cheek_Far,
  EarR->Ear_Near, EarL->Ear_Far, Mouth, Teeth, Chin, Neck.
  (E5 split: Nose is its OWN token; "Proj" is the AUXILIARY snout/horn
  feature below, authored from the Nose ring.)

Extras (Part VII.2/VII.3): the 5 authored viseme shapes {Closed, A, I, U,
Neutral} x {front, 3Q, profile} in the Mouth feature and the blink frames
{Open, Half, Closed} x {front, 3Q, profile} in both eye features (P00-only,
right-half yaw rows; negative yaw renders the mirror).

Every piece resolves the EXACT ring the runtime resolves for that view
(FPSchematicStatePoseOut + the FPSchematicPairPartner swap + the Y22/Y67
feature variant), then passes it through the smooth_art engine (I.1/I.6/I.7).

Self-verification: table parse coverage, front-glyph parity, left-half
mirror identity, sub-row gating, pitch-corner duplication, UnderPlane
presence, no-Pn90/no-P20 tokens, file-name token parse, exact per-feature
file/cell counts, grid cell-key set equality, and the _tokens.json sidecar
(consumed by deploy.py for the zero-gap verify).

Idempotent: re-running overwrites the same 337 files + 17 grid files
deterministically. Run from the repo root:  py generate_art.py
"""

import os
import re
import sys
import json
import math

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
SCHEMATIC_H = os.path.join(REPO_ROOT, "FaceParallaxSchematic.h")
ART_ROOT = os.path.join(REPO_ROOT, "Art")
GRID_ROOT = os.path.join(ART_ROOT, "_grids")

# Import the smooth-art engine (art_guide I.1/I.6/I.7 compliance).
sys.path.insert(0, REPO_ROOT)
from smooth_art import (
    ring_to_svg_paths,
    emit_svg as _smooth_emit_svg,
    render_paths_xml as _smooth_render_paths_xml,
    collect_clip_defs as _smooth_collect_clip_defs,
    part_silhouette_d as _smooth_part_silhouette_d,
)

CANVAS = 1000.0          # SVG viewBox side; [0,1]^2 canvas * 1000

# ---------------------------------------------------------------------------
# Feature table (mirror of FPSvg::FeatureTable in FaceParallaxSvgParse.h).
# Each entry: (right-half authoring part, feature token).
# The Nose part carries its OWN token ("Nose", art_guide I.6); "Proj" is an
# AUXILIARY token (snout/horn for creature presets, art_guide VIII) authored
# by the nose ring and listed in AUXILIARY below — never part of the 17.
# ---------------------------------------------------------------------------
FEATURES = [
    ("Head", "FaceBase"),
    ("Nose", "Nose"),
    ("Bangs", "HairFront"),
    ("Hair", "HairBack"),
    ("BackHair", "BackHair"),
    ("EyeR", "Eye_Near"),
    ("EyeL", "Eye_Far"),
    ("BrowR", "Brow_Near"),
    ("BrowL", "Brow_Far"),
    ("CheekR", "Cheek_Near"),
    ("CheekL", "Cheek_Far"),
    ("EarR", "Ear_Near"),
    ("EarL", "Ear_Far"),
    ("Mouth", "Mouth"),
    ("Teeth", "Teeth"),
    ("Chin", "Chin"),
    ("Neck", "Neck"),
]
FEATURE_PART = {f: p for p, f in FEATURES}
FEATURES_ALL = [f for _, f in FEATURES]

# Non-canonical library tokens: (feature, authoring part). Each keeps its own
# folder + grid + manifest entry; the canonical 17 never include them. "Proj"
# (snout/horn projection) authors from the Nose ring and keeps its Y22
# sub-row (SUB_ROWS) — the review's E5 split.
AUXILIARY = [("Proj", "Nose")]
AUTHORS_ALL = FEATURES_ALL + [f for f, _p in AUXILIARY]

# E10 — walk-behind empties (art_guide XII.4 / Part IV Zone 5): the runtime
# hides EVERY BridgeSafe card in the walk-behind states (5/6/7 — Back3Q/Back/
# Back3Q_L; FPSchematicLayerVisibleInState shows only AnchorCritical parts
# there), so the placeholder library ships EMPTY cells for those rows instead
# of art that can never render, flagged redundant:true in the manifest.
ANCHOR_CRITICAL_FEATURES = {
    "FaceBase", "HairFront", "HairBack", "BackHair", "Ear_Near", "Ear_Far",
}
BRIDGE_SAFE_FEATURES = [f for f in AUTHORS_ALL if f not in ANCHOR_CRITICAL_FEATURES]
WALK_BEHIND_FILES = (5, 6)      # Back3Q, Back (right-half files)
WALK_BEHIND_CELLS = (5, 6, 7)   # + Back3Q_L grid mirror

# Right-half yaw rows: (state_idx, state_token, yaw_token).
RIGHT_ROWS = [
    (0, "Front", "Y00"),
    (1, "Narrow", "Y22"),
    (2, "3Q", "Y45"),
    (3, "Sliver", "Y67"),
    (4, "Profile", "Y90"),
    (5, "Back3Q", "Y135"),
    (6, "Back", "Y180"),
]
# Right state index -> left state index (mirror of FPSvg::MirrorIndexForIndex).
LEFT_MIRROR = {1: 11, 2: 10, 3: 8, 4: 9, 5: 7}
PITCH_BANDS = ["P00", "P45", "Pn45"]
TOP_ROW = (12, "Top", "Y00", "P90")
UNDERPLANE_ROW = (13, "UnderPlane", "Y00", "Pn45")

# Y22/Y67 sub-rows exist ONLY for eyes + Proj (art_guide XI.5).
SUB_ROWS = {
    "Y22": {"Eye_Near", "Eye_Far", "Proj"},
    "Y67": {"Eye_Near", "Eye_Far"},
}

# Extras: the 5 authored visemes (VII.2) + the blink frames (VII.3), each x
# {front, 3Q, profile} at P00. Content: AUTHORED shapes (not placeholder
# centroid scales) — visemes are a piecewise open/grin/purse/closed
# construction (author_viseme_ring), blinks keep the lid squash about the
# centroid (a closed lid IS a Y squash) with the ART gated in smooth_art.py
# (Closed = lash lines only, Half = iris at 45% under the half lid).
VISEMES = ["A", "I", "U", "Closed", "Neutral"]
VISEME_FORCE = {
    # (f_up, f_down, f_x): upper lip pull-up / lower lip pull-down about the
    # lip-corner line, X about the ring center. The corner points stay on
    # the corner line — a real opening, not a centroid scale.
    "Closed": (0.32, 0.32, 1.00),
    "A": (1.90, 1.90, 1.12),
    "I": (1.15, 1.15, 1.55),
    "U": (1.45, 1.45, 0.82),
    "Neutral": (1.00, 1.00, 1.00),
}
BLINKS = ["Open", "Half", "Closed"]
BLINK_SCALE = {"Open": 1.0, "Half": 0.5, "Closed": 0.1}
EXTRA_YAW_STATES = [("Y00", 0), ("Y45", 2), ("Y90", 4)]

# Paired parts resolve the PARTNER's ring mirrored for states 7-11
# (FPSchematicPairPartner: near/far role split follows the turn).
PAIR_PARTNER = {
    "EyeL": "EyeR", "EyeR": "EyeL",
    "BrowL": "BrowR", "BrowR": "BrowL",
    "CheekL": "CheekR", "CheekR": "CheekL",
    "EarL": "EarR", "EarR": "EarL",
}

# State-center yaw table (FPSchematicStateCenterYaw mirror).
STATE_CENTER_YAW = {
    0: 0.0, 1: 22.5, 2: 45.0, 3: 67.5, 4: 90.0, 5: 135.0, 6: 180.0,
    7: -135.0, 8: -90.0, 9: -67.5, 10: -45.0, 11: -22.5, 12: 0.0, 13: 0.0,
}

# Ring slots in the authored pose set (FPSchematicPoseSet field order):
# P0, P45, P90, P135, P180, PTop, PBottom
RING_SLOT = ["P0", "P45", "P90", "P135", "P180", "PTop", "PBottom"]


class Vector2:
    __slots__ = ("X", "Y")

    def __init__(self, x, y):
        self.X = float(x)
        self.Y = float(y)


# ---------------------------------------------------------------------------
# Parsing: canonical table + flat front glyphs, anchor-based (same approach
# as the remediate.py regeneration script — re-running is idempotent).
# ---------------------------------------------------------------------------
def _matching_brace(src, open_idx, end):
    """Given src[open_idx] == '{', return the index of its matching '}'."""
    depth = 0
    for k in range(open_idx, end):
        c = src[k]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return k
    return -1


def _ring_from_body(body):
    pts = [Vector2(m.group(1), m.group(2))
           for m in re.finditer(
               r"SPT\(\s*([0-9]*\.?[0-9]+)\s*,\s*([0-9]*\.?[0-9]+)\s*\)", body)]
    return pts if pts else None


def parse_authored_pose_table():
    """Parse FPSchematicAuthoredPoseTable() -> { part_name: [7 rings] }.

    The entry is '{ "Name", { ring0..ring6 } }'. We scan the quoted part
    names, skip the entry brace + the ring-list '{', then read 7 top-level
    '{ ... }' blocks (brace-depth aware so multi-line rings parse)."""
    with open(SCHEMATIC_H, "r", encoding="utf-8") as f:
        src = f.read()
    fn = "FPSchematicAuthoredPoseTable"
    fidx = src.find(fn)
    if fidx < 0:
        raise SystemExit(f"[FATAL] {SCHEMATIC_H}: '{fn}' not found")
    start = src.find("Table[] = {", fidx)
    if start < 0:
        raise SystemExit(f"[FATAL] {SCHEMATIC_H}: 'Table[] = {{' not found")
    start += len("Table[] = {")
    end = src.find("return Table;", start)
    if end < 0:
        raise SystemExit(f"[FATAL] {SCHEMATIC_H}: table terminator not found")
    table = {}
    i = start
    while True:
        i = src.find('"', i, end)
        if i < 0:
            break
        name_start = i + 1
        name_end = src.find('"', name_start)
        if name_end < 0 or name_end >= end:
            break
        name = src[name_start:name_end]
        entry_brace = src.find("{", name_end, end)
        if entry_brace < 0:
            break
        close = _matching_brace(src, entry_brace, end)
        if close < 0:
            break
        rings = []
        k = entry_brace + 1
        while k < close:
            ob = src.find("{", k, close)
            if ob < 0:
                break
            cb = _matching_brace(src, ob, close)
            if cb < 0:
                break
            ring = _ring_from_body(src[ob + 1:cb])
            if ring:
                rings.append(ring)
            k = cb + 1
        if len(rings) == 7:
            table[name] = rings
        i = name_end + 1
    return table


def parse_front_glyphs():
    """Parse DefaultPartSchematics() -> { part_name: ring } (the flat front
    glyphs; P0 of every authored part must equal these exactly)."""
    with open(SCHEMATIC_H, "r", encoding="utf-8") as f:
        src = f.read()
    fn = "inline std::vector<FPSchematicPart> DefaultPartSchematics()"
    fidx = src.find(fn)
    if fidx < 0:
        raise SystemExit(f"[FATAL] {SCHEMATIC_H}: '{fn}' not found")
    start = src.find("return {", fidx)
    if start < 0:
        raise SystemExit(f"[FATAL] {SCHEMATIC_H}: 'return {{' not found")
    start += len("return {")
    depth = 0
    i = start - 1
    end = len(src)
    while i < end:
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
        i += 1
    glyphs = {}
    for m in re.finditer(r'\{ "(\w+)",\s*\{([^}]*)\}', src[start:end]):
        name = m.group(1)
        pts = [Vector2(p.group(1), p.group(2))
               for p in re.finditer(
                   r"SPT\(\s*([0-9]*\.?[0-9]+)\s*,\s*([0-9]*\.?[0-9]+)\s*\)",
                   m.group(2))]
        if pts:
            glyphs[name] = pts
    return glyphs


# ---------------------------------------------------------------------------
# View resolution (mirror of FPSchematicOutlineForState).
# ---------------------------------------------------------------------------
def scale_ring_about_centroid(ring, sx, sy):
    """FPSchematicScaleRingAboutCentroid mirror: uniform-scale about the ring
    centroid (mirror-invariant, so the left-half variant order never matters)."""
    cx = sum(p.X for p in ring) / len(ring)
    cy = sum(p.Y for p in ring) / len(ring)
    return [Vector2(cx + (p.X - cx) * sx, cy + (p.Y - cy) * sy) for p in ring]


def scale_ring_about_canthus(ring, sx, sy):
    """FPSchematicScaleRingAboutCanthus mirror (E11): anisotropic scale about
    the ring centroid in a frame ALIGNED WITH THE RING'S OWN CANTHUS CHORD
    (indices 0 -> 9 — the outer to inner corner). Preserves the canthus angle
    exactly while the card foreshortens; falls back to the centroid scale for
    degenerate (too-short) rings, exactly like the C++ helper."""
    if len(ring) < 10:
        return scale_ring_about_centroid(ring, sx, sy)
    dx = ring[9].X - ring[0].X
    dy = ring[9].Y - ring[0].Y
    length = math.sqrt(dx * dx + dy * dy)
    if length <= 0.0:
        return scale_ring_about_centroid(ring, sx, sy)
    c = dx / length
    s = dy / length
    cx = sum(p.X for p in ring) / len(ring)
    cy = sum(p.Y for p in ring) / len(ring)
    out = []
    for p in ring:
        a = (p.X - cx) * c + (p.Y - cy) * s     # along the chord
        b = -(p.X - cx) * s + (p.Y - cy) * c    # across the chord
        out.append(Vector2(cx + (sx * a) * c - (sy * b) * s,
                           cy + (sx * a) * s + (sy * b) * c))
    return out


def author_viseme_ring(ring, viseme):
    """Authored mouth viseme shapes (VII.2): piecewise Y about the lip-corner
    line (the midpoint of the ring's y-span — the mouth's corners sit there),
    upper lip pulled UP / lower lip DOWN by the viseme's force, X about the
    ring center. NOT a centroid scale: the corners stay on the corner line so
    the mouth opens from the center (A = tall open, I = wide grin, U = small
    purse, Closed = thin line, Neutral = identity)."""
    if viseme == "Neutral":
        return ring
    f_up, f_down, f_x = VISEME_FORCE[viseme]
    ys = [p.Y for p in ring]
    xs = [p.X for p in ring]
    mid = (min(ys) + max(ys)) * 0.5
    cx = (min(xs) + max(xs)) * 0.5
    out = []
    for p in ring:
        if p.Y <= mid:
            y = mid - (mid - p.Y) * f_up
        else:
            y = mid + (p.Y - mid) * f_down
        out.append(Vector2(cx + (p.X - cx) * f_x, y))
    return out


def feature_variant(part, ring, state_idx):
    """FPSchematicFeatureVariantAt mirror (WI1): the Y22/Y67 sub-threshold eye
    variants are pure transforms applied AFTER the pose resolve, scaled in the
    ring's OWN CANTHUS frame (E11: FPSchematicScaleRingAboutCanthus — the
    foreshortened cards keep the tareme canthus angle instead of rotating it,
    axis-aligned Y squash steepened the sliver chord to ~40 deg).
    Role-keyed via FPSchematicIsFarSide at the state's center yaw — the far
    eye narrows (0.85 at Y22, 0.30 at Y67), the near eye takes the 3Q card
    (0.88) at Y67. Every other part is returned unchanged."""
    if part not in ("EyeL", "EyeR"):
        return ring
    cy = STATE_CENTER_YAW.get(state_idx, 0.0)
    far = (part.endswith("L") and cy > 0.0) or (part.endswith("R") and cy < 0.0)
    if state_idx in (1, 11):
        return scale_ring_about_canthus(ring, 0.85, 0.95) if far else ring
    if state_idx in (3, 9):
        return scale_ring_about_canthus(ring, 0.30, 0.95) if far \
            else scale_ring_about_canthus(ring, 0.88, 0.95)
    return ring


# E9 — independently authored pitch-corner cells (art_guide XI.3 / XVI.3).
# A +45/-45 pitch view foreshortens the card vertically (uniform Y squash
# about the ring centroid — real 2D art never deforms, so this is a pose
# authoring choice for the PLACEHOLDER library, mirroring the runtime pitch
# contract's directions) and shifts it per role: features + hair ENCROACH
# toward the view (down at +pitch / up at -pitch), ears + V-chin + neck
# COUNTER-translate, the face base near-static (FPOrientationVerticalShift's
# role directions from the schematic header). The shift is clamped to the
# card so every ring stays inside [0,1]^2; X is untouched, so paired parts
# keep exact mirror consistency.
PITCH_ROLE_SHIFT = {
    "P45":  {"feature": 0.012, "counter": -0.008, "base": 0.004},
    "Pn45": {"feature": -0.012, "counter": 0.008, "base": -0.004},
}
PITCH_COUNTER_PARTS = ("EarR", "EarL", "Chin", "Neck")
PITCH_BASE_PARTS = ("Head",)
PITCH_CORNER_SQUASH_Y = 0.95


def pitch_corner_ring(part, ring, band):
    """The authored P45/Pn45 cell ring for `ring`: Y-squash about the centroid
    + a role-keyed vertical shift (clamped to the card). NEVER the P00 ring —
    the E9 self-check asserts non-duplication."""
    role = ("counter" if part in PITCH_COUNTER_PARTS
            else ("base" if part in PITCH_BASE_PARTS else "feature"))
    shift = PITCH_ROLE_SHIFT[band][role]
    cy = sum(p.Y for p in ring) / len(ring)
    out = [Vector2(p.X, cy + (p.Y - cy) * PITCH_CORNER_SQUASH_Y) for p in ring]
    ys = [p.Y for p in out]
    if shift < 0.0:
        shift = max(shift, -min(ys))
    else:
        shift = min(shift, 1.0 - max(ys))
    for p in out:
        p.Y += shift
    return out


def corner_apply(part, ring, pitch):
    """E9: apply the authored pitch-corner transform to a resolved ring."""
    if pitch in ("P45", "Pn45"):
        return pitch_corner_ring(part, ring, pitch)
    return ring


def resolve_state_ring(part, state_idx, table):
    """The exact ring the runtime resolves for `part` at `state_idx` (0..13).
    Mirrors FPSchematicStatePoseOut + the partner swap + the Y22/Y67 variant
    inside FPSchematicOutlineForState."""
    src_part = part
    if 7 <= state_idx <= 11 and part in PAIR_PARTNER:
        src_part = PAIR_PARTNER[part]
    rings = table.get(src_part)
    if not rings:
        return []
    slot = 0
    mirror = False
    if state_idx in (1, 11):
        slot = 0          # Narrow: front P0, left half mirrored
        mirror = state_idx == 11
    elif state_idx in (2, 3, 10, 9):
        slot = 1          # P45 (3/4 art until the profile sliver key)
        mirror = state_idx in (9, 10)
    elif state_idx in (4, 8):
        slot = 2          # P90
        mirror = state_idx == 8
    elif state_idx in (5, 7):
        slot = 3          # P135
        mirror = state_idx == 7
    elif state_idx == 6:
        slot = 4          # P180
    elif state_idx == 12:
        slot = 5          # PTop
    elif state_idx == 13:
        slot = 6          # PBottom (the UnderPlane corner)
    else:
        slot = 0          # Front (state 0)
    ring = rings[slot]
    if mirror:
        ring = [Vector2(1.0 - p.X, p.Y) for p in ring]
    return feature_variant(part, ring, state_idx)


# ---------------------------------------------------------------------------
# Grid rows.
# ---------------------------------------------------------------------------
def feature_has_row(feature, yaw_token):
    """Y22/Y67 sub-rows exist only for Eye_Near/Eye_Far/Proj (XI.5)."""
    if yaw_token in SUB_ROWS:
        return feature in SUB_ROWS[yaw_token]
    return True


def authored_rows(feature):
    """(state_idx, state_token, yaw_token, pitch_token) for the right-half
    FILES of a feature (the authored library pieces)."""
    rows = []
    for idx, st, yaw in RIGHT_ROWS:
        if not feature_has_row(feature, yaw):
            continue
        for band in PITCH_BANDS:
            rows.append((idx, st, yaw, band))
    rows.append(TOP_ROW)
    rows.append(UNDERPLANE_ROW)
    return rows


def cell_rows(feature):
    """All asset CELLS for a feature: right half, mirrored left half (the
    guide's '-180..0 range is produced by mirroring', III.3), Top, UnderPlane.
    Returns (state_idx, state_token, yaw_token, pitch_token, bLeftMirror)."""
    rows = []
    for idx, st, yaw, pitch in authored_rows(feature):
        rows.append((idx, st, yaw, pitch, False))
        if idx in LEFT_MIRROR:
            rows.append((LEFT_MIRROR[idx], st + "_L", yaw, pitch, True))
    return rows


def extra_cell_keys(feature):
    """Extra cell keys for a feature: visemes (Mouth) / blinks (eyes)."""
    keys = []
    if feature == "Mouth":
        for v in VISEMES:
            for yaw, _st in EXTRA_YAW_STATES:
                keys.append((feature, v, yaw, "P00"))
    elif feature in ("Eye_Near", "Eye_Far"):
        for b in BLINKS:
            for yaw, _st in EXTRA_YAW_STATES:
                keys.append((feature, b, yaw, "P00"))
    return keys


# ---------------------------------------------------------------------------
# Emission.
# ---------------------------------------------------------------------------
def paths_for(part, ring, state_token=None, mirror=False):
    return ring_to_svg_paths(part, [(p.X, p.Y) for p in ring], canvas=CANVAS,
                             state_token=state_token, mirror=mirror)


def paths_xml(paths, indent="      ", clip_scope=None):
    """Render path dicts as <path> elements, wrapping consecutive paths
    that share a clip_id in <g clip-path="url(#scope__id)"> (Feature 1).
    ``clip_scope`` namespaces every clip id so the per-feature grid SVG
    (many cells in one document) keeps ids globally unique. The matching
    <clipPath> definitions are emitted once per grid by emit_grid_svg
    (see collect_clip_defs)."""
    return _smooth_render_paths_xml(paths, indent=indent, clip_scope=clip_scope)


def emit_empty_svg(part, state_token, yaw_token, pitch_token, canvas=CANVAS):
    """E10: a walk-behind EMPTY cell. The runtime hides every BridgeSafe card
    in the walk-behind states (art_guide XII.4 / Part IV Zone 5), so the
    placeholder ships a documented empty file instead of art that can never
    render. Flagged redundant:true in Art/_tokens.json."""
    return (f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<!-- {part} / {yaw_token} {pitch_token} ({state_token}) — FaceParallax placeholder art.\n'
            f'     EMPTY CELL (E10, art_guide XII.4 / Part IV Zone 5): walk-behind\n'
            f'     view — the runtime hides this BridgeSafe card at states 5/6/7\n'
            f'     (FPSchematicLayerVisibleInState), so there is no art to render.\n'
            f'     Flagged redundant:true in Art/_tokens.json.\n'
            f'     Generated by generate_art.py from FPSchematicAuthoredPoseTable()\n'
            f'     in FaceParallaxSchematic.h. Canvas [0,1]^2 scaled to '
            f'{int(canvas)}x{int(canvas)} (Y down). -->\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'viewBox="0 0 {int(canvas)} {int(canvas)}">\n'
            f'</svg>\n')


def emit_grid_svg(feature, groups, canvas=CANVAS):
    """One grid SVG per feature: a <g id="<full cell key>"> per cell.

    Feature 1: every cell's clip_id ("eye_silhouette") is namespaced with
    its own cell key (cell_key__eye_silhouette) so the grid document keeps
    ids globally unique, and every clip def is collected into one top-of-
    document <defs> block. The cell's paths then reference the scoped id
    via <g mask="url(#cell_key__eye_silhouette)">."""
    defs = []
    seen_ids = set()
    body = []
    for key, paths in groups:
        for cid, d in _smooth_collect_clip_defs(paths, clip_scope=key):
            if cid in seen_ids:
                continue
            seen_ids.add(cid)
            defs.append((cid, d))
        body.append(f'    <g id="{key}">\n'
                    f'{paths_xml(paths, clip_scope=key)}\n'
                    f'    </g>')
    if defs:
        defs_lines = ["  <defs>"]
        for cid, d in defs:
            defs_lines.append(
                f'    <mask id="{cid}" maskUnits="userSpaceOnUse" '
                f'x="0" y="0" width="{int(canvas)}" height="{int(canvas)}">')
            defs_lines.append(f'      <path d="{d}" fill="white"/>')
            defs_lines.append(f'    </mask>')
        defs_lines.append("  </defs>")
        defs_block = "\n".join(defs_lines) + "\n"
    else:
        defs_block = ""
    return (f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<!-- {feature} grid — FaceParallax placeholder art library.\n'
            f'     Generated by generate_art.py (smooth_art engine) from\n'
            f'     FPSchematicAuthoredPoseTable() in FaceParallaxSchematic.h.\n'
            f'     One <g id="<cell key>"> per cell (right + mirrored left +\n'
            f'     Top + UnderPlane + extras). Vector line art on the Part I\n'
            f'     geometry canvas [0,1]^2 scaled to {int(canvas)}x{int(canvas)}\n'
            f'     (Y down). Curves follow art_guide.md I.1/I.6/I.7.\n'
            f'     Feature 1: per-cell <mask> ids namespace the eye silhouette\n'
            f'     (white-on-black containment) so the iris / highlights mask\n'
            f'     to the eye outline inside every cell. -->\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'viewBox="0 0 {int(canvas)} {int(canvas)}">\n'
            + defs_block +
            "\n".join(body) +
            f'\n</svg>\n')


# ---------------------------------------------------------------------------
# Verification helpers (mirror the Phase 6 validator).
# ---------------------------------------------------------------------------
def ring_valid(ring):
    return ring is not None and len(ring) >= 3 and all(
        0.0 <= p.X <= 1.0 and 0.0 <= p.Y <= 1.0 for p in ring)


def near(a, b, eps=1e-6):
    return abs(a - b) <= eps


def rings_equal(a, b, eps=1e-6):
    if len(a) != len(b):
        return False
    return all(near(p.X, q.X, eps) and near(p.Y, q.Y, eps) for p, q in zip(a, b))


# ---------------------------------------------------------------------------
def main():
    table = parse_authored_pose_table()
    glyphs = parse_front_glyphs()

    missing = [p for p, _ in FEATURES if p not in table]
    if missing:
        raise SystemExit(f"[FAIL] authored table missing parts: {missing}")

    # Front glyph parity: P0 of every authored part == flat DefaultPartSchematics.
    for part, _f in FEATURES:
        rings = table[part]
        if len(rings) != 7:
            raise SystemExit(f"[FAIL] {part}: expected 7 rings, got {len(rings)}")
        front = glyphs.get(part)
        if front is None:
            raise SystemExit(f"[FAIL] {part}: no flat front glyph")
        if not rings_equal(rings[0], front, eps=1e-9):
            raise SystemExit(f"[FAIL] {part}: P0 != DefaultPartSchematics front glyph")

    # Per-part ring validity + point-count invariance.
    for part, _f in FEATURES:
        for slot in range(7):
            if not ring_valid(table[part][slot]):
                raise SystemExit(f"[FAIL] {part} ring[{slot}] invalid")
            if len(table[part][slot]) != len(table[part][0]):
                raise SystemExit(f"[FAIL] {part} ring[{slot}] count mismatch")

    os.makedirs(ART_ROOT, exist_ok=True)
    os.makedirs(GRID_ROOT, exist_ok=True)

    written = 0
    grid_files = {}
    manifest = {"features": {}}
    art_check = []  # (feature, cell key, paths) for the global escape/seam gate
    author_part = dict(FEATURE_PART)
    author_part.update({f: p for f, p in AUXILIARY})

    def emit_one(feature, part):
        """Write one feature folder + grid (canonical or auxiliary token),
        fill the manifest entry; returns the number of files written."""
        feature_dir = os.path.join(ART_ROOT, feature)
        os.makedirs(feature_dir, exist_ok=True)

        file_keys = set()
        cells = {}
        redundant = []
        for idx, st, yaw, pitch in authored_rows(feature):
            if feature in BRIDGE_SAFE_FEATURES and idx in WALK_BEHIND_FILES:
                key = f"{feature}_{st}_{yaw}_{pitch}"
                fname = f"{key}.svg"
                file_keys.add(fname)
                cells[key] = []
                redundant.append(key)
                art_check.append((feature, key, []))
                with open(os.path.join(feature_dir, fname), "w", encoding="utf-8") as f:
                    f.write(emit_empty_svg(part, st, yaw, pitch))
                continue
            ring = resolve_state_ring(part, idx, table)
            if not ring_valid(ring):
                raise SystemExit(f"[FAIL] {feature} {st} {yaw} {pitch}: ring invalid")
            ring = corner_apply(part, ring, pitch)
            if not ring_valid(ring):
                raise SystemExit(f"[FAIL] {feature} {st} {yaw} {pitch}: corner ring invalid")
            fname = f"{feature}_{st}_{yaw}_{pitch}.svg"
            file_keys.add(fname)
            paths = paths_for(part, ring, state_token=st)
            with open(os.path.join(feature_dir, fname), "w", encoding="utf-8") as f:
                f.write(_smooth_emit_svg(part, st, yaw, pitch, paths, canvas=CANVAS))
            cells[f"{feature}_{st}_{yaw}_{pitch}"] = paths
            art_check.append((feature, f"{feature}_{st}_{yaw}_{pitch}", paths))

        extras = extra_cell_keys(feature)
        for _feat, st, yaw, pitch in extras:
            key = f"{feature}_{st}_{yaw}_{pitch}"
            if st in VISEMES:
                src_part = "Mouth"
                _st_map = {y: i for y, i in EXTRA_YAW_STATES}
                ring = resolve_state_ring(src_part, _st_map[yaw], table)
                ring = author_viseme_ring(ring, st)
            else:
                src_part = part
                _st_map = {y: i for y, i in EXTRA_YAW_STATES}
                ring = resolve_state_ring(src_part, _st_map[yaw], table)
                # Scale about upper-lash centroid (points 0-9) for eyes,
                # so the closed lid stays centered where the open eye was.
                if part in ("EyeR", "EyeL") and len(ring) >= 10:
                    upper = ring[:10]
                    cx = sum(p.X for p in upper) / len(upper)
                    cy = sum(p.Y for p in upper) / len(upper)
                    ring = [Vector2(cx + (p.X - cx) * 1.0,
                                    cy + (p.Y - cy) * BLINK_SCALE[st])
                            for p in ring]
                else:
                    ring = scale_ring_about_centroid(ring, 1.0, BLINK_SCALE[st])
            if not ring_valid(ring):
                raise SystemExit(f"[FAIL] {feature} extra {st} {yaw}: ring invalid")
            fname = f"{key}.svg"
            file_keys.add(fname)
            paths = paths_for(part, ring, state_token=st)
            with open(os.path.join(feature_dir, fname), "w", encoding="utf-8") as f:
                f.write(_smooth_emit_svg(part, st, yaw, pitch, paths, canvas=CANVAS))
            cells[key] = paths
            art_check.append((feature, key, paths))

        # Left cells: the mirror of the partner's right cell (III.3). The
        # resolved left ring IS that mirror (verify below), so build the
        # smooth paths from the resolved left ring directly.
        grid_groups = []
        for idx, st, yaw, pitch, bLeft in cell_rows(feature):
            key = f"{feature}_{st}_{yaw}_{pitch}"
            if bLeft:
                if feature in BRIDGE_SAFE_FEATURES and idx in WALK_BEHIND_CELLS:
                    paths = []
                    cells[key] = paths
                    redundant.append(key)
                    art_check.append((feature, key, paths))
                else:
                    ring = resolve_state_ring(part, idx, table)
                    if not ring_valid(ring):
                        raise SystemExit(f"[FAIL] {feature} left {st}: ring invalid")
                    ring = corner_apply(part, ring, pitch)
                    if not ring_valid(ring):
                        raise SystemExit(f"[FAIL] {feature} left {st} {pitch}: corner ring invalid")
                    paths = paths_for(part, ring, state_token=st, mirror=True)
                    cells[key] = paths
                    art_check.append((feature, key, paths))
            else:
                paths = cells[key]
            grid_groups.append((key, paths))
        for _feat, st, yaw, pitch in extras:
            key = f"{feature}_{st}_{yaw}_{pitch}"
            grid_groups.append((key, cells[key]))

        grid_name = f"{feature}.svg"
        with open(os.path.join(GRID_ROOT, grid_name), "w", encoding="utf-8") as f:
            f.write(emit_grid_svg(feature, grid_groups))
        grid_files[feature] = grid_name

        # Profile merge (Part IV Zone 4): the FaceBase Profile / Profile_L
        # cells carry the nose-bridge + lips contour overlay (Nose/Mouth/
        # Teeth hide at 4/8). The smooth Head builder emits the outline plus
        # the merge overlay for those cells — assert both reads have it.
        if feature == "FaceBase":
            for prof_key in ("FaceBase_Profile_Y90_P00", "FaceBase_Profile_L_Y90_P00"):
                prof_paths = cells.get(prof_key, [])
                if len(prof_paths) < 2:
                    raise SystemExit(
                        f"[FAIL] profile merge: {prof_key} lacks the contour overlay")

        base_cells = len(cell_rows(feature))
        cell_key_list = sorted(
            f"{feature}_{st}_{yaw}_{pitch}"
            for idx, st, yaw, pitch, _bLeft in cell_rows(feature))
        cell_key_list.extend(
            f"{feature}_{st}_{yaw}_{pitch}"
            for _feat, st, yaw, pitch in extras)
        manifest["features"][feature] = {
            "files": sorted(file_keys),
            "cells": base_cells + len(extras),
            "cell_keys": sorted(cell_key_list),
            "redundant": sorted(redundant),
            "grid": grid_name,
            "part": part,
        }
        return len(file_keys)

    for feature in AUTHORS_ALL:
        written += emit_one(feature, author_part[feature])

    # --- Self-checks ---
    # 1. Left-half mirror identity: the resolved left ring == mirror of the
    #    partner's right ring (paired parts swap via FPSchematicPairPartner).
    def partner_of(part):
        return PAIR_PARTNER.get(part, part)

    for feature in AUTHORS_ALL:
        part = author_part[feature]
        for dst, src in ((11, 1), (10, 2), (9, 3), (8, 4), (7, 5)):
            m = [Vector2(1.0 - p.X, p.Y)
                 for p in resolve_state_ring(partner_of(part), src, table)]
            r = resolve_state_ring(part, dst, table)
            if not rings_equal(m, r):
                raise SystemExit(
                    f"[FAIL] {feature} ({part}): state {dst} != mirror(partner {src})")

    # 2. Pitch-corner authorship (E9, art_guide XI.3/XVI.3): the P45/Pn45
    #    cells are INDEPENDENTLY authored (vertical foreshorten + per-role
    #    pitch shift) — no pitch-corner cell is a copy of its P00 sibling.
    #    (Walk-behind rows of BridgeSafe features are E10 empties — skipped.)
    for feature in AUTHORS_ALL:
        part = author_part[feature]
        for idx, st, yaw in RIGHT_ROWS:
            if not feature_has_row(feature, yaw):
                continue
            if feature in BRIDGE_SAFE_FEATURES and idx in WALK_BEHIND_FILES:
                continue
            base = resolve_state_ring(part, idx, table)
            for band in ("P45", "Pn45"):
                corner = pitch_corner_ring(part, base, band)
                if rings_equal(base, corner):
                    raise SystemExit(
                        f"[FAIL] {feature} {st} {yaw} {band}: corner duplicates P00")
                if not ring_valid(corner):
                    raise SystemExit(
                        f"[FAIL] {feature} {st} {yaw} {band}: corner ring invalid")
                for p in corner:
                    if p.X != p.X or p.Y != p.Y:
                        raise SystemExit(f"[FAIL] {feature} {st} {yaw} {band}: NaN in corner")

    # 3. Exact counts (mirror of the FPSvg grid contract).
    expected_files = {}
    expected_cells = {}
    for feature in AUTHORS_ALL:
        rows = authored_rows(feature)
        expected_files[feature] = len(rows) + len(extra_cell_keys(feature))
        expected_cells[feature] = len(cell_rows(feature)) + len(extra_cell_keys(feature))
    for feature in AUTHORS_ALL:
        mf = manifest["features"][feature]
        if len(mf["files"]) != expected_files[feature]:
            raise SystemExit(f"[FAIL] {feature}: {len(mf['files'])} files, expected {expected_files[feature]}")
        if mf["cells"] != expected_cells[feature]:
            raise SystemExit(f"[FAIL] {feature}: {mf['cells']} cells, expected {expected_cells[feature]}")

    total_files = sum(len(manifest["features"][f]["files"]) for f in AUTHORS_ALL)
    total_cells = sum(manifest["features"][f]["cells"] for f in AUTHORS_ALL)
    manifest["total_files"] = total_files
    manifest["total_cells"] = total_cells
    manifest["grid_files"] = len(AUTHORS_ALL)

    # 4. Token hygiene: every written file parses per the guide token rules
    #    (feature prefix + valid state/yaw/pitch triple), no Pn90/P20 tokens,
    #    no _L files (left cells live in the grid files only).
    for feature in FEATURES_ALL:
        for fname in manifest["features"][feature]["files"]:
            if not fname.endswith(".svg"):
                raise SystemExit(f"[FAIL] {feature}: stray file {fname}")
            stem = fname[:-4]
            if not stem.startswith(feature + "_"):
                raise SystemExit(f"[FAIL] {feature}: bad prefix {fname}")
            rest = stem[len(feature) + 1:]
            tail = rest.rsplit("_", 2)
            if len(tail) != 3:
                raise SystemExit(f"[FAIL] {feature}: bad token shape {fname}")
            state, yaw, pitch = tail
            if "_L" in state:
                raise SystemExit(f"[FAIL] {feature}: _L file {fname}")
            if state not in ("Front", "Narrow", "3Q", "Sliver", "Profile",
                             "Back3Q", "Back", "Top", "UnderPlane") \
                    and state not in VISEMES and state not in BLINKS:
                raise SystemExit(f"[FAIL] {feature}: bad state token {fname}")
            if yaw not in ("Y00", "Y22", "Y45", "Y67", "Y90", "Y135", "Y180"):
                raise SystemExit(f"[FAIL] {feature}: bad yaw token {fname}")
            if pitch not in ("P00", "P45", "Pn45", "P90"):
                raise SystemExit(f"[FAIL] {feature}: bad pitch token {fname}")
            if pitch == "P90" and state != "Top":
                raise SystemExit(f"[FAIL] {feature}: P90 outside Top {fname}")
            if state in VISEMES + BLINKS and pitch != "P00":
                raise SystemExit(f"[FAIL] {feature}: extras must be P00 {fname}")
            if state in VISEMES and feature != "Mouth" \
                    and not (state in BLINKS and feature in ("Eye_Near", "Eye_Far")):
                raise SystemExit(f"[FAIL] {feature}: viseme outside Mouth {fname}")
            if state in BLINKS and feature not in ("Eye_Near", "Eye_Far") \
                    and not (state in VISEMES and feature == "Mouth"):
                raise SystemExit(f"[FAIL] {feature}: blink outside eyes {fname}")

    # 5. Grid cell-key set == the manifest cell set for every feature.
    for feature in AUTHORS_ALL:
        with open(os.path.join(GRID_ROOT, grid_files[feature]), "r", encoding="utf-8") as f:
            src = f.read()
        src = re.sub(r"<!--.*?-->", "", src, flags=re.S)
        ids = re.findall(r'<g id="([^"]+)"', src)
        expected = set()
        for idx, st, yaw, pitch, _bLeft in cell_rows(feature):
            expected.add(f"{feature}_{st}_{yaw}_{pitch}")
        for _feat, st, yaw, pitch in extra_cell_keys(feature):
            expected.add(f"{feature}_{st}_{yaw}_{pitch}")
        if set(ids) != expected:
            raise SystemExit(
                f"[FAIL] {feature}: grid ids != cell set "
                f"(missing {expected - set(ids)}, extra {set(ids) - expected})")
        if len(ids) != len(set(ids)):
            raise SystemExit(f"[FAIL] {feature}: duplicate grid ids")

    # 6. Top == authored PTop (exact), Back == P180 (exact).
    for feature in AUTHORS_ALL:
        part = author_part[feature]
        if not rings_equal(resolve_state_ring(part, 12, table), table[part][5]):
            raise SystemExit(f"[FAIL] {feature}: Top != PTop")
        if not rings_equal(resolve_state_ring(part, 6, table), table[part][4]):
            raise SystemExit(f"[FAIL] {feature}: Back != P180")

    # 7. UnderPlane present for every feature.
    for feature in AUTHORS_ALL:
        if f"{feature}_UnderPlane_Y00_Pn45.svg" not in manifest["features"][feature]["files"]:
            raise SystemExit(f"[FAIL] {feature}: UnderPlane file missing")

    # 10. E10 walk-behind empties: every BridgeSafe feature's Back3Q/Back
    #     files are EMPTY and flagged redundant:true; every AnchorCritical
    #     feature KEEPS real back art (the silhouettes + ear back-fuzz carry
    #     the walk-behind read).
    for feature in BRIDGE_SAFE_FEATURES:
        red = set(manifest["features"][feature].get("redundant", []))
        for idx, st, yaw in ((5, "Back3Q", "Y135"), (6, "Back", "Y180")):
            for band in PITCH_BANDS:
                key = f"{feature}_{st}_{yaw}_{band}"
                fname = f"{key}.svg"
                if key not in red:
                    raise SystemExit(f"[FAIL] E10: {feature}: {key} not flagged redundant")
                with open(os.path.join(ART_ROOT, feature, fname), "r", encoding="utf-8") as f:
                    content = f.read()
                if "<path" in content:
                    raise SystemExit(f"[FAIL] E10: {feature}: {fname} is not empty")
                if content.count("EMPTY CELL") != 1:
                    raise SystemExit(f"[FAIL] E10: {feature}: {fname} lacks the empty marker")
        for band in PITCH_BANDS:
            key = f"{feature}_Back3Q_L_Y135_{band}"
            if key not in red:
                raise SystemExit(f"[FAIL] E10: {feature}: grid cell {key} not flagged redundant")
    for feature in sorted(ANCHOR_CRITICAL_FEATURES):
        for st, yaw in (("Back3Q", "Y135"), ("Back", "Y180")):
            fname = f"{feature}_{st}_{yaw}_P00.svg"
            with open(os.path.join(ART_ROOT, feature, fname), "r", encoding="utf-8") as f:
                content = f.read()
            if "<path" not in content:
                raise SystemExit(f"[FAIL] E10: {feature}: {fname} must keep its back art")

    # 8. Silhouette-read gate (Phase 3): the 6 anchor-critical parts resolve
    #    for ALL 14 states (the read is never a formula fallback), and the
    #    FaceBase Profile merge overlay's author bands sit on the CURRENT
    #    authored P90 face line (brow ~0.2, chin ~0.84) — if a ring edit
    #    moves the profile, the gate fires instead of shipping a drift.
    ANCHOR_CRITICAL = ("Head", "Bangs", "Hair", "BackHair", "EarL", "EarR")
    for part in ANCHOR_CRITICAL:
        for s in range(14):
            if not ring_valid(resolve_state_ring(part, s, table)):
                raise SystemExit(f"[FAIL] silhouette read: {part} state {s} invalid")
    head_p90 = table["Head"][2]
    chin_y = max(p.Y for p in head_p90)
    brow_y = head_p90[2].Y
    if not (0.78 <= chin_y <= 0.90):
        raise SystemExit(f"[FAIL] silhouette read: Head P90 chin y {chin_y:.3f} outside [0.78,0.90]")
    if not (0.15 <= brow_y <= 0.28):
        raise SystemExit(f"[FAIL] silhouette read: Head P90 brow y {brow_y:.3f} outside [0.15,0.28]")
    if not all(brow_y < b < chin_y for b in (0.40, 0.52, 0.60, 0.665, 0.695, 0.735)):
        raise SystemExit("[FAIL] silhouette read: Head P90 merge bands outside the face line")

    # 9. Art-escape + seam gate (Phase 3): every emitted path's ON-CURVE
    #    geometry stays INSIDE the card (the A.10-class fill-escape check on
    #    the PYTHON engine — the C++ A.10 test only sees the C++ chain), and
    #    every FILL patch keeps >= SEAM_MIN_MARGIN from all four card edges
    #    so no fill ever crosses the turn seam. Stroke control points may
    #    bulge outside from Catmull-Rom smoothing (clipped by the viewBox;
    #    bounded to STROKE_OVERSHOOT so the curve bulge stays sub-pixel).
    SEAM_MIN_MARGIN = 0.03
    STROKE_OVERSHOOT = 0.08

    def path_geometry(d_str):
        """Parse an emitted SVG d-string into (on_curve, all_points) in UV
        (canvas units / CANVAS). M/L targets, C's end and Q's end are
        on-curve; C's two controls and Q's control are steer-only (may
        overshoot)."""
        tokens = d_str.replace(",", " ").split()
        on_curve, all_pts = [], []

        def uv(x, y):
            return (x / CANVAS, y / CANVAS)

        i = 0
        while i < len(tokens):
            cmd = tokens[i]
            if cmd in ("M", "L"):
                x, y = uv(float(tokens[i + 1]), float(tokens[i + 2]))
                i += 3
                on_curve.append((x, y))
                all_pts.append((x, y))
            elif cmd == "Q":
                x1, y1 = uv(float(tokens[i + 1]), float(tokens[i + 2]))
                x2, y2 = uv(float(tokens[i + 3]), float(tokens[i + 4]))
                i += 5
                all_pts.append((x1, y1))
                on_curve.append((x2, y2))
                all_pts.append((x2, y2))
            elif cmd == "C":
                x1, y1 = uv(float(tokens[i + 1]), float(tokens[i + 2]))
                x2, y2 = uv(float(tokens[i + 3]), float(tokens[i + 4]))
                x3, y3 = uv(float(tokens[i + 5]), float(tokens[i + 6]))
                i += 7
                all_pts.append((x1, y1))
                all_pts.append((x2, y2))
                on_curve.append((x3, y3))
                all_pts.append((x3, y3))
            else:
                i += 1  # Z (or unknown)
        return on_curve, all_pts

    fill_patches = 0
    for feature, key, paths in art_check:
        for p in paths:
            on_curve, all_pts = path_geometry(p["d"])
            if p.get("fill", "none") != "none":
                pts = all_pts
            else:
                pts = on_curve
            for x, y in pts:
                if x < -0.0015 or x > 1.0015 or y < -0.0015 or y > 1.0015:
                    raise SystemExit(
                        f"[FAIL] art-escape {key}: coordinate ({x:.4f},{y:.4f}) outside card")
            if p.get("fill", "none") != "none":
                fill_patches += 1
                xs = [x for x, _y in all_pts]
                ys = [y for _x, y in all_pts]
                # The turn seam is the VERTICAL card edge (the parallax slide
                # is horizontal — FPSchematicParallaxSlidePeak); the top/
                # bottom card boundaries are not seams (pitch swaps, never
                # slides), so only x-margins carry the seam margin. The
                # y-bounds are already enforced by the in-card escape check.
                if (min(xs) < SEAM_MIN_MARGIN or max(xs) > 1.0 - SEAM_MIN_MARGIN):
                    raise SystemExit(f"[FAIL] seam gate {key}: fill patch crosses the turn seam")
            else:
                for x, y in all_pts:
                    if (x < -STROKE_OVERSHOOT or x > 1.0 + STROKE_OVERSHOOT
                            or y < -STROKE_OVERSHOOT or y > 1.0 + STROKE_OVERSHOOT):
                        raise SystemExit(
                            f"[FAIL] art-escape {key}: stroke control "
                            f"({x:.4f},{y:.4f}) overshoots {STROKE_OVERSHOOT}")

    # 11. Feature 1 — iris masked to eye silhouette: every Eye_Near / Eye_Far
    #     non-empty non-Closed cell ships exactly one <mask id="..."> +
    #     <g mask="url(#...)"> wrapping the iris + 2 highlight paths; the
    #     lash strokes stay OUTSIDE the mask group (they are the visible
    #     outline and must never be masked). Closed blink cells drop the
    #     iris entirely (no mask). E10 walk-behind empties carry no art.
    #     Grid files: every <mask id> is globally unique (scoped by cell
    #     key) so a multi-cell document never has colliding ids.
    EYE_FEATURES = ("Eye_Near", "Eye_Far")
    for feature in EYE_FEATURES:
        grid_path = os.path.join(GRID_ROOT, feature + ".svg")
        with open(grid_path, "r", encoding="utf-8") as f:
            grid_src = re.sub(r"<!--.*?-->", "", f.read(), flags=re.S)
        mask_ids = re.findall(r'<mask id="([^"]+)"', grid_src)
        if len(mask_ids) != len(set(mask_ids)):
            dup = [c for c in set(mask_ids) if mask_ids.count(c) > 1]
            raise SystemExit(
                f"[FAIL] Feature 1: {feature} grid has duplicate mask ids: {dup[:3]}")
        # Each mask def must have a matching url(#id) reference and vice-versa.
        refs = re.findall(r'mask="url\(#([^)]+)\)"', grid_src)
        if set(mask_ids) != set(refs):
            raise SystemExit(
                f"[FAIL] Feature 1: {feature} grid mask ids != refs "
                f"(defs-set={set(mask_ids)}, ref-set={set(refs)})")

        for cell_key in manifest["features"][feature]["cell_keys"]:
            # Find the cell's outer <g id="cell_key"> ... </g> block,
            # depth-aware (the cell contains nested <g> groups for the
            # mask — a naive .*? stops at the first inner </g>).
            open_tag = f'<g id="{cell_key}">'
            start = grid_src.find(open_tag)
            if start < 0:
                continue
            depth = 0
            i = start
            end = -1
            while i < len(grid_src):
                o = grid_src.find("<g", i)
                c = grid_src.find("</g>", i)
                if c < 0:
                    break
                if o >= 0 and o < c:
                    depth += 1
                    i = o + 2
                else:
                    depth -= 1
                    if depth == 0:
                        end = c + len("</g>")
                        break
                    i = c + 4
            if end < 0:
                raise SystemExit(
                    f"[FAIL] Feature 1: {cell_key} unbalanced <g> in grid")
            block_start = start + len(open_tag)
            inner = grid_src[block_start:end - len("</g>")]
            # Walk-behind empties: <g id="..."> with no <path> (E10 marker
            # lives only in the standalone file, not the grid).
            if "<path" not in inner:
                continue
            is_closed = "_Closed_" in cell_key
            if is_closed:
                if "<mask" in inner or "mask=" in inner:
                    raise SystemExit(
                        f"[FAIL] Feature 1: {cell_key} (Closed blink) must not mask")
                continue
            # Non-closed, non-empty eye cell: exactly one mask group, three
            # masked <path> elements (iris + 2 highlights), and the mask
            # id must be scoped to this cell.
            scoped_id = f"{cell_key}__eye_silhouette"
            if f'<mask id="{scoped_id}"' not in grid_src:
                raise SystemExit(
                    f"[FAIL] Feature 1: {cell_key} missing mask def {scoped_id}")
            mask_group_count = len(re.findall(r'<g mask="', inner))
            if mask_group_count != 1:
                raise SystemExit(
                    f"[FAIL] Feature 1: {cell_key} has {mask_group_count} mask "
                    f"groups (expected exactly 1)")
            # The 2 lash strokes (fill="none") sit OUTSIDE the mask group;
            # the 3 iris/highlight paths (fill != none) sit INSIDE.
            g_match = re.search(
                r'<g mask="url\(#' + re.escape(scoped_id) + r'\)">\s*(.*?)\s*</g>',
                inner, re.S)
            if not g_match:
                raise SystemExit(
                    f"[FAIL] Feature 1: {cell_key} mask group not found")
            inside = g_match.group(1)
            outside = inner[:g_match.start()] + inner[g_match.end():]
            inside_paths = re.findall(r'<path\b', inside)
            outside_paths = re.findall(r'<path\b', outside)
            if len(inside_paths) != 3:
                raise SystemExit(
                    f"[FAIL] Feature 1: {cell_key} expected 3 masked paths "
                    f"(iris + 2 highlights), got {len(inside_paths)}")
            if len(outside_paths) < 2:
                raise SystemExit(
                    f"[FAIL] Feature 1: {cell_key} expected >= 2 lash strokes "
                    f"outside the mask, got {len(outside_paths)}")
            # The masked paths must be FILLS (the iris + highlights); the
            # unmasked paths must be STROKES (the lash lines).
            for pm in re.finditer(r'<path\b([^>]*)/>', inside):
                if 'fill="none"' in pm.group(1):
                    raise SystemExit(
                        f"[FAIL] Feature 1: {cell_key} stroke path leaked "
                        f"inside the mask group")
            for pm in re.finditer(r'<path\b([^>]*)/>', outside):
                if 'fill="none"' not in pm.group(1) and 'fill="#' in pm.group(1):
                    raise SystemExit(
                        f"[FAIL] Feature 1: {cell_key} fill path leaked "
                        f"outside the mask group")
            # Every mask def must carry a white-filled silhouette path
            # (containment mask semantics: white = visible).
            mask_block = re.search(
                r'<mask id="' + re.escape(scoped_id) + r'"[^>]*>(.*?)</mask>',
                grid_src, re.S)
            if not mask_block:
                raise SystemExit(
                    f"[FAIL] Feature 1: {cell_key} mask block not found")
            if 'fill="white"' not in mask_block.group(1):
                raise SystemExit(
                    f"[FAIL] Feature 1: {cell_key} mask must contain a "
                    f"white-filled silhouette path")

    # 12. Feature 1 — non-Eye features must NOT ship any mask (the
    #     iris-eye mask is the only per-cell mask in the placeholder library).
    for feature in AUTHORS_ALL:
        if feature in EYE_FEATURES:
            continue
        grid_path = os.path.join(GRID_ROOT, feature + ".svg")
        with open(grid_path, "r", encoding="utf-8") as f:
            src_nc = re.sub(r"<!--.*?-->", "", f.read(), flags=re.S)
        if "<mask" in src_nc or "mask=" in src_nc:
            raise SystemExit(
                f"[FAIL] Feature 1: {feature} must not carry any mask "
                f"(only Eye cells have an iris-eye mask)")

    with open(os.path.join(ART_ROOT, "_tokens.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    print(f"[OK] generated {written} vector art files "
          f"({total_files} total incl. {len(VISEMES) * 3} visemes + "
          f"{len(BLINKS) * 3 * 2} blinks) across {len(AUTHORS_ALL)} feature "
          f"folders in {ART_ROOT}")
    print(f"[OK] {manifest['grid_files']} per-feature grid files in {GRID_ROOT} "
          f"({total_cells} cells)")
    print(f"[OK] E5 split: Nose is its own token ({len(manifest['features']['Nose']['files'])} files); "
          f"Proj kept for snout/horn ({len(manifest['features']['Proj']['files'])} files); "
          f"manifest carries both")
    print(f"[OK] E10 walk-behind empties: {len(BRIDGE_SAFE_FEATURES)} BridgeSafe features ship "
          f"empty Back3Q/Back cells (flagged redundant), {len(ANCHOR_CRITICAL_FEATURES)} "
          f"AnchorCritical features keep their back art")
    print(f"[OK] Bottom excluded from files; the REQUIRED UnderPlane corner "
          f"(Part V.4/XI.2) is generated per feature")
    print(f"[OK] all 17 P0 rings == DefaultPartSchematics front glyphs (1e-9)")
    print(f"[OK] left-half mirror identity, sub-row gating, E9 pitch-corner "
          f"authorship (P45/Pn45 != P00), token hygiene + counts verified")
    print(f"[OK] Phase 3: authored visemes (piecewise open/grin/purse/closed) "
          f"+ blink art gates (Closed lid / Half iris) + FaceBase Profile "
          f"merged contour (Part IV Zone 4)")
    print(f"[OK] Phase 3 gates: silhouette-read (6 anchor-critical x 14 "
          f"states + P90 merge bands), art-escape (all paths in-card), "
          f"seam gate ({fill_patches} fill patches clear of the card edges)")
    print(f"[OK] Feature 1: every non-closed Eye_Near/Eye_Far cell ships "
          f"one <mask id='<cell>__eye_silhouette'> (white-on-black containment) "
          f"+ exactly 3 masked paths (iris + 2 highlights); lash strokes "
          f"stay un-masked; grid mask ids unique; non-Eye features mask-free")


if __name__ == "__main__":
    main()
