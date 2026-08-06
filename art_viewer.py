#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""art_viewer.py — FaceParallax ART VIEWER (composition + dashboard).

COMPOSITION: every rule value comes from the compiled system. The bridge
(art_viewer_bridge.exe, built from art_viewer_bridge.cpp which #includes the
canonical FaceParallaxSchematic.h / FaceParallaxSvgParse.h) prints the
per-state visibility / Z-order / resolved-cell-key truth by calling the REAL
contract functions (FPSchematicLayerVisibleInState / OrderInState,
CollapseViewStateForFeature + FeatureCellKey). This script only ASSEMBLES:
it extracts the resolved cells from Art/_grids/<Feature>.svg (the same single
import source the runtime uses) and stacks them far-to-near into
Art/_views/View_<State>.svg — one composed character view per state, exactly
as the editor canvas paints them. No rule is re-implemented here; recompiling
the bridge always follows the current system.

DASHBOARD: a stdlib-only local HTTP server showing the 14 composed views in a
grid (front center, top on top, left on left, back bottom-center, plus the
transition states in a strip) and a gallery of EVERY authored art file
(Art/_tokens.json). "Regenerate all art" runs generate_art.py (the system's
generator), recompiles the bridge, re-emits the views and live-reloads.

Usage:
  py art_viewer.py                 # serve the dashboard (opens the browser)
  py art_viewer.py --emit-only     # compose the 14 views headless
  py art_viewer.py --port 9000 --no-open
"""

import html
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
BRIDGE_CPP = os.path.join(REPO_ROOT, "art_viewer_bridge.cpp")
BRIDGE_EXE = os.path.join(REPO_ROOT, "art_viewer_bridge.exe")
SCHEMATIC_H = os.path.join(REPO_ROOT, "FaceParallaxSchematic.h")
SVGPARSE_H = os.path.join(REPO_ROOT, "FaceParallaxSvgParse.h")
ART_ROOT = os.path.join(REPO_ROOT, "Art")
GRID_ROOT = os.path.join(ART_ROOT, "_grids")
VIEWS_OUT = os.path.join(ART_ROOT, "_views")
TOKENS_JSON = os.path.join(ART_ROOT, "_tokens.json")

GRID_LAYOUT = [
    [10, 12, 2],
    [8, 0, 4],
    [7, 6, 5],
]
STRIP = [11, 9, 13, 3, 1]

CELL_RE = re.compile(r'<g id="([^"]+)">(.*?)</g>', re.S)
FILE_RE = re.compile(r"^[A-Za-z0-9_]+\.svg$")


def _extract_top_level_cells(src):
    """Depth-aware extractor: returns {cell_id: inner_xml} for every
    top-level ``<g id="...">...</g>`` block in ``src``. A depth-aware walk
    is required because Feature 1 (per-cell eye-silhouette clip) makes Eye
    cells contain a NESTED ``<g clip-path>`` — the non-greedy ``CELL_RE``
    would stop at the inner ``</g>`` and capture an unbalanced fragment."""
    cells = {}
    i = 0
    n = len(src)
    while i < n:
        m = re.search(r'<g id="([^"]+)">', src[i:])
        if not m:
            break
        cell_id = m.group(1)
        open_start = i + m.start()
        open_end = i + m.end()
        depth = 1
        j = open_end
        while j < n and depth > 0:
            o = src.find("<g", j)
            c = src.find("</g>", j)
            if c < 0:
                break
            if o >= 0 and o < c:
                depth += 1
                j = o + 2
            else:
                depth -= 1
                j = c + 4
        if depth != 0:
            raise RuntimeError(
                f"unbalanced <g> for cell '{cell_id}' in grid source")
        inner = src[open_end:j - len("</g>")]
        cells[cell_id] = inner
        i = j
    return cells


# ---------------------------------------------------------------------------
# Bridge (the compiled system).
# ---------------------------------------------------------------------------
def find_compiler():
    return shutil.which("g++") or shutil.which("clang++")


def bridge_stale():
    if not os.path.exists(BRIDGE_EXE):
        return True
    exe_m = os.path.getmtime(BRIDGE_EXE)
    for f in (BRIDGE_CPP, SCHEMATIC_H, SVGPARSE_H):
        if os.path.exists(f) and os.path.getmtime(f) > exe_m:
            return True
    return False


def ensure_bridge(force=False):
    if not force and not bridge_stale():
        return
    cc = find_compiler()
    if not cc:
        raise RuntimeError(
            "no g++/clang++ on PATH - install msys2 ucrt64 g++ (the same "
            "compiler Tests\\run_tests.ps1 requires)")
    cmd = [cc, "-std=c++17", "-O2", "-o", BRIDGE_EXE, BRIDGE_CPP,
           "-Werror", "-Wall", "-Wextra"]
    r = subprocess.run(cmd, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode != 0:
        raise RuntimeError("bridge compile failed:\n" + r.stdout + r.stderr)


def run_bridge():
    ensure_bridge()
    r = subprocess.run([BRIDGE_EXE], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode != 0:
        raise RuntimeError("bridge run failed:\n" + r.stdout + r.stderr)
    try:
        return json.loads(r.stdout)
    except json.JSONDecodeError as e:
        raise RuntimeError(f"bridge JSON invalid: {e}\n{r.stdout[:500]}")


# ---------------------------------------------------------------------------
# Composition (pure assembly of the system's answers).
# ---------------------------------------------------------------------------
def load_grid_cells(feature):
    path = os.path.join(GRID_ROOT, feature + ".svg")
    if not os.path.exists(path):
        raise RuntimeError(f"grid file missing: {path} (run generate_art.py)")
    with open(path, "r", encoding="utf-8") as f:
        src = re.sub(r"<!--.*?-->", "", f.read(), flags=re.S)
    return _extract_top_level_cells(src)


def load_grid_masks(feature):
    """Extract every ``<mask id="X">...</mask>`` block from a feature's
    grid file's ``<defs>``. Returns ``{mask_id: mask_xml}``. These are the
    per-cell containment masks (Feature 1: the Eye cell's iris-eye mask).
    They MUST be re-emitted in the composed view's own ``<defs>`` so the
    cell's ``mask="url(#X)"`` references resolve — the grid's ``<defs>``
    is NOT carried automatically when cell XML is extracted."""
    path = os.path.join(GRID_ROOT, feature + ".svg")
    if not os.path.exists(path):
        return {}
    with open(path, "r", encoding="utf-8") as f:
        src = re.sub(r"<!--.*?-->", "", f.read(), flags=re.S)
    masks = {}
    for m in re.finditer(r'<mask\s+id="([^"]+)"', src):
        mask_id = m.group(1)
        start = m.start()
        close = src.find("</mask>", start)
        if close < 0:
            continue
        masks[mask_id] = src[start:close + len("</mask>")]
    return masks


def load_all_grid_cells(manifest):
    return {f["feature"]: load_grid_cells(f["feature"]) for f in manifest["features"]}


def load_all_grid_masks(manifest):
    return {f["feature"]: load_grid_masks(f["feature"]) for f in manifest["features"]}


# ---------------------------------------------------------------------------
# Silhouette extraction (Feature 2 + Feature 3 — the "virtual filled
# silhouette" of every part, used as clip geometry for the universal
# occlusion rule and the eyes-clipped-to-face containment rule).
# ---------------------------------------------------------------------------
_PATH_RE = re.compile(r"<path\b([^>]*?)/?>", re.S)
_ATTR_RE = re.compile(r'(\w[\w-]*)="([^"]*)"')


def _strip_nested_groups(xml):
    """Remove nested ``<g ...>...</g>`` blocks (and their content) from a
    cell's XML, leaving only top-level ``<path>`` elements. Used by
    part_silhouette_d so the Eye cell's iris + highlights (which live
    inside a ``<g clip-path>``) are ignored — only the lash outline is the
    part's silhouette."""
    out = []
    i = 0
    n = len(xml)
    while i < n:
        g = xml.find("<g", i)
        if g < 0:
            out.append(xml[i:])
            break
        out.append(xml[i:g])
        depth = 1
        j = g + 2
        while j < n and depth > 0:
            o = xml.find("<g", j)
            c = xml.find("</g>", j)
            if c < 0:
                break
            if o >= 0 and o < c:
                depth += 1
                j = o + 2
            else:
                depth -= 1
                j = c + 4
        i = j
    return "".join(out)


def part_silhouette_d(cell_xml):
    """The d-string of the part's outline silhouette — the "virtual filled
    silhouette" used for cross-layer clipping (Feature 3 occlusion +
    Feature 2 eyes-clipped-to-face).

    Preference order:
      1. An explicit ``<path id="silhouette">`` hidden element (Issue 4:
         the Mouth neutral's closed lens shape, emitted by smooth_art but
         never painted — fill=none stroke=none).
      2. The first top-level closed STROKE path (``fill="none"`` +
         ``stroke != none`` + d-string ending in Z). This is the head
         outline, the smoothed hair ring, the eye upper-lash wedge, etc.
      3. The first top-level closed FILL path (e.g. Mouth visemes' dark
         interior — the mouth cavity IS the silhouette for occlusion).

    Returns None if no closed path exists (E10 empties)."""
    top = _strip_nested_groups(cell_xml)
    # 1. Explicit hidden silhouette path (Issue 4: Mouth neutral).
    m = re.search(r'<path\s+id="silhouette"\s+d="([^"]+)"', top)
    if m:
        return m.group(1)
    paths = []
    for m in _PATH_RE.finditer(top):
        paths.append(dict(_ATTR_RE.findall(m.group(1))))
    for p in paths:
        d = p.get("d", "").rstrip()
        if d and d[-1] in ("Z", "z") \
                and p.get("fill", "none") == "none" \
                and p.get("stroke", "none") != "none":
            return p["d"]
    for p in paths:
        d = p.get("d", "").rstrip()
        if d and d[-1] in ("Z", "z") and p.get("fill", "none") != "none":
            return p["d"]
    return None


# Parts whose strokes are CONTAINED to the face silhouette (Feature 2 —
# "massive eyes that would go off the face would be masked out of bounds").
# Per the owner's clarification: eyes only (brows/bangs/nose/mouth are not
# contained to the face — they may overhang the jaw / extend past the
# hairline as their authored art dictates).
PARTS_CLIPPED_TO_FACE = frozenset({"EyeL", "EyeR"})
FACE_PART = "Head"


def validate_all_cells(manifest, grid_cache):
    missing = []
    for e in manifest["cells"]:
        if e["key"] not in grid_cache[e["feature"]]:
            missing.append(f"state {e['state']} {e['feature']}: {e['key']}")
    if missing:
        raise RuntimeError(
            "cells resolved by the system missing from the art library:\n"
            + "\n".join(missing[:20]))


def compose_view(manifest, state_idx, grid_cache, grid_mask_cache=None):
    feats = {f["part"]: f["feature"] for f in manifest["features"]}
    vis_by_part = {}
    for e in manifest["visibility"]:
        if e["state"] == state_idx:
            vis_by_part[e["part"]] = e
    cell_by_feature = {}
    for e in manifest["cells"]:
        if e["state"] == state_idx:
            cell_by_feature[e["feature"]] = e["key"]

    layers = []
    for part, feature in feats.items():
        v = vis_by_part[part]
        if not v["visible"]:
            continue
        key = cell_by_feature.get(feature)
        if not key:
            raise RuntimeError(f"state {state_idx} part {part}: no resolved cell")
        cell = grid_cache[feature].get(key)
        if cell is None:
            raise RuntimeError(
                f"state {state_idx} part {part}: cell '{key}' not found in "
                f"Art/_grids/{feature}.svg")
        if "<path" not in cell:
            raise RuntimeError(
                f"state {state_idx} part {part}: cell '{key}' is an E10 empty "
                "- the visibility matrix should exclude it")
        layers.append((v["order"], part, feature, cell))
    layers.sort(key=lambda t: t[0], reverse=True)

    # Feature 2 + Feature 3 — extract every visible part's silhouette (the
    # "virtual filled silhouette" used for both the eyes-masked-to-face
    # containment and the universal occlusion rule).
    silhouettes = {}
    for _order, part, _feat, cell in layers:
        silhouettes[part] = part_silhouette_d(cell)

    state_token = manifest["states"][state_idx]["token"]
    CANVAS_PX = 1000

    # Feature 1 (per-cell iris mask carry-over) — every cell may reference
    # per-cell masks (eye_silhouette) via mask="url(#X)". Those <mask>
    # defs live in the GRID file's <defs>, NOT in the cell XML. They MUST
    # be re-emitted in this view's <defs> or every per-cell mask reference
    # dangles and silently does nothing. Scan every cell's mask refs and
    # collect the corresponding mask defs from grid_mask_cache.
    cell_mask_defs = {}  # mask_id -> mask_xml
    if grid_mask_cache:
        for _order, part, feature, cell in layers:
            for ref in re.findall(r'mask="url\(#([^)]+)\)"', cell):
                if ref in cell_mask_defs:
                    continue
                feat_masks = grid_mask_cache.get(feature, {})
                if ref in feat_masks:
                    cell_mask_defs[ref] = feat_masks[ref]

    # Feature 2 — face containment mask (eyes-masked-to-face). White-filled
    # face silhouette on the mask's implicit black background: white =
    # opaque (eye strokes visible), black = transparent (eye strokes hidden
    # outside the face outline).
    face_mask_id = f"face_mask__{state_token}"
    face_d = silhouettes.get(FACE_PART)
    has_face_mask = bool(face_d)

    # Feature 3 — universal occlusion: each back layer L's strokes are
    # masked to OUTSIDE the union of every in-front layer's silhouette.
    # White rect = visible everywhere by default; each occluder silhouette
    # painted BLACK = hidden. Overlapping occluders union correctly (black
    # on black = still black — no evenodd holes, no winding dependency).
    # The closest layer (smallest order, no in-front occluders) gets no
    # occlusion mask.
    occ_ids = {}  # part -> occlusion mask id (None when no occluders)
    occ_occluders = {}  # part -> list of occluder silhouette d-strings
    for order, part, _feat, _cell in layers:
        occluder_ds = []
        seen_d = set()
        for o_order, o_part, _of, _oc in layers:
            if o_order >= order:
                continue
            d = silhouettes.get(o_part)
            if not d or d in seen_d:
                continue
            seen_d.add(d)
            occluder_ds.append(d)
        if not occluder_ds:
            occ_ids[part] = None
        else:
            occ_ids[part] = f"occ_mask__{state_token}__{part}"
            occ_occluders[part] = occluder_ds

    # --- emit one <defs> with every mask used in this view ---
    defs = []
    # Feature 1: per-cell mask carry-over (verbatim from the grid).
    for mask_id, mask_xml in cell_mask_defs.items():
        defs.append(f"    {mask_xml}")
    # Feature 2: face containment mask.
    if has_face_mask:
        defs.append(
            f'    <mask id="{face_mask_id}" maskUnits="userSpaceOnUse" '
            f'x="0" y="0" width="{CANVAS_PX}" height="{CANVAS_PX}">\n'
            f'      <path d="{face_d}" fill="white"/>\n'
            f'    </mask>')
    # Feature 3: per-layer occlusion masks.
    for part, occluder_ds in occ_occluders.items():
        occ_id = occ_ids[part]
        body_paths = [
            f'      <rect x="0" y="0" width="{CANVAS_PX}" height="{CANVAS_PX}" fill="white"/>']
        for d in occluder_ds:
            body_paths.append(f'      <path d="{d}" fill="black"/>')
        defs.append(
            f'    <mask id="{occ_id}" maskUnits="userSpaceOnUse" '
            f'x="0" y="0" width="{CANVAS_PX}" height="{CANVAS_PX}">\n'
            + "\n".join(body_paths) + "\n"
            f'    </mask>')
    defs_block = ("  <defs>\n" + "\n".join(defs) + "\n  </defs>\n") if defs else ""

    # --- emit body with masks applied per layer ---
    body = []
    for _order, part, _feat, cell in layers:
        wraps = []
        # Feature 2: eyes-masked-to-face containment (outer mask).
        if part in PARTS_CLIPPED_TO_FACE and has_face_mask:
            wraps.append(f'<g mask="url(#{face_mask_id})">')
        # Feature 3: universal occlusion (inner mask — additional cut).
        occ_id = occ_ids.get(part)
        if occ_id:
            wraps.append(f'<g mask="url(#{occ_id})">')
        if wraps:
            inner = "".join(wraps) + "\n" + cell + "\n" + ("</g>\n" * len(wraps))
        else:
            inner = cell
        body.append(f'  <g id="{part}">\n{inner}\n  </g>')

    st = manifest["states"][state_idx]
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<!-- Composed view {st["token"]} (state {state_idx}, yaw '
        f'{st["yaw"]}, pitch {st["centerPitch"]:g}).\n'
        '     Layers resolved by art_viewer_bridge.exe from the canonical\n'
        '     FaceParallaxSchematic.h / FaceParallaxSvgParse.h contracts\n'
        '     (FPSchematicLayerVisibleInState / OrderInState + FeatureCellKey);\n'
        '     cell art extracted verbatim from Art/_grids/. Painted far-to-near\n'
        '     (FPZDepth 5 first .. 1 last). Canvas [0,1]^2 scaled to 1000x1000\n'
        '     (Y down). Generated by art_viewer.py.\n'
        '     Feature 1: each Eye cell\'s iris + highlights arrive pre-masked\n'
        '     to the eye silhouette (per-cell <mask> from the grid, carried\n'
        '     into this view\'s <defs> verbatim).\n'
        '     Feature 2: EyeL/EyeR additionally masked to the face silhouette\n'
        '     (<mask id="face_mask__<state>">, white-on-black containment).\n'
        '     Feature 3: every non-closest layer\'s strokes are masked to\n'
        '     OUTSIDE the union of every in-front layer\'s "virtual filled\n'
        '     silhouette" (cel-cleanup: lines blocked by a closer layer\'s\n'
        '     outline do not show up) via per-layer <mask> with a white rect\n'
        '     + black occluder silhouettes (reliable union, no evenodd holes). -->\n'
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 1000">\n'
        + defs_block +
        "\n".join(body) + "\n</svg>\n")


def _verify_composed_views(manifest, grid_cache, files):
    """Feature 1/2/3 self-checks on the composed View_*.svg files.

    For every view:
      * every ``mask="url(#X)"`` reference resolves to a ``<mask id="X">``
        IN THE SAME FILE (the critical check that catches the previous
        failure mode where per-cell mask defs from the grid were dropped
        during composition, leaving dangling references that silently
        disabled Feature 1 in every composed view);
      * exactly one ``face_mask__<state>`` <mask> (FaceBase is always
        visible with a silhouette);
      * every visible EyeL / EyeR layer wraps its cell in BOTH the
        ``face_mask__<state>`` mask (Feature 2 containment) AND its
        own ``occ_mask__<state>__<part>`` mask (Feature 3 occlusion);
      * every non-closest visible layer wraps its cell in the matching
        ``occ_mask__<state>__<part>`` mask;
      * every <mask id> is globally unique inside the document;
      * every occlusion mask carries one white <rect> + N black occluder
        <path> elements (N >= 1).
    """
    for st in manifest["states"]:
        token = st["token"]
        name = files.get(token)
        if not name:
            continue
        path = os.path.join(VIEWS_OUT, name)
        with open(path, "r", encoding="utf-8") as f:
            src = re.sub(r"<!--.*?-->", "", f.read(), flags=re.S)

        mask_ids = re.findall(r'<mask\s+id="([^"]+)"', src)
        if len(mask_ids) != len(set(mask_ids)):
            dup = sorted({c for c in mask_ids if mask_ids.count(c) > 1})
            raise RuntimeError(
                f"{name}: duplicate mask ids: {dup[:3]}")

        # THE critical check — every mask= reference must resolve in-file.
        mask_refs = re.findall(r'mask="url\(#([^)]+)\)"', src)
        unresolved = sorted(set(mask_refs) - set(mask_ids))
        if unresolved:
            raise RuntimeError(
                f"{name}: UNRESOLVED mask references (the grid's <defs> was "
                f"not carried into the view): {unresolved[:5]}")

        face_id = f"face_mask__{token}"
        face_defs = [c for c in mask_ids if c == face_id]
        if len(face_defs) != 1:
            raise RuntimeError(
                f"{name}: expected exactly one {face_id}, got {len(face_defs)}")

        # Verify each occlusion mask is well-formed (white rect + black
        # occluder paths).
        for m in re.finditer(
                r'<mask\s+id="(occ_mask__[^"]+)"[^>]*>(.*?)</mask>',
                src, re.S):
            cid, body = m.group(1), m.group(2)
            if len(re.findall(r'<rect\b', body)) != 1:
                raise RuntimeError(
                    f"{name}: {cid} must contain exactly one <rect>")
            if 'fill="white"' not in body:
                raise RuntimeError(
                    f"{name}: {cid} rect must be fill=\"white\"")
            black_paths = len(re.findall(
                r'<path\b[^>]*fill="black"', body))
            if black_paths < 1:
                raise RuntimeError(
                    f"{name}: {cid} must contain >=1 black occluder <path>")
            if 'fill="black"' not in body:
                raise RuntimeError(
                    f"{name}: {cid} must contain black occluder fills")

        # Verify the EyeL/EyeR / non-closest layer wrappers via the
        # manifest's per-state visibility (source of truth for which
        # parts/rows should be masked).
        vis_by_part = {e["part"]: e for e in manifest["visibility"]
                       if e["state"] == st["idx"] and e["visible"]}
        orders = sorted({vis_by_part[p]["order"] for p in vis_by_part})
        closest_order = orders[0] if orders else None
        for part, v in vis_by_part.items():
            m2 = re.search(r'<g id="' + re.escape(part) + r'">', src)
            if not m2:
                raise RuntimeError(f"{name}: <g id=\"{part}\"> missing")
            i = m2.start()
            # Walk to the matching </g> (depth-aware — the layer contains
            # nested mask groups).
            depth = 1
            j = src.find(">", i) + 1
            while j < len(src) and depth > 0:
                o = src.find("<g", j)
                c = src.find("</g>", j)
                if c < 0:
                    break
                if o >= 0 and o < c:
                    depth += 1
                    j = o + 2
                else:
                    depth -= 1
                    j = c + 4
            block = src[i:j]
            refs = re.findall(r'mask="url\(#([^)]+)\)"', block)
            # Feature 2: Eye parts must reference the face mask.
            if part in PARTS_CLIPPED_TO_FACE:
                if face_id not in refs:
                    raise RuntimeError(
                        f"{name}: {part} must reference {face_id}")
            # Feature 3: every non-closest layer must reference its occ mask.
            if v["order"] != closest_order:
                occ_id = f"occ_mask__{token}__{part}"
                if occ_id not in refs:
                    raise RuntimeError(
                        f"{name}: {part} must reference {occ_id}")
                if occ_id not in mask_ids:
                    raise RuntimeError(
                        f"{name}: {occ_id} mask def missing")
            else:
                # Closest layer: no occlusion mask should be applied.
                occ_ref = [r for r in refs if r.startswith(f"occ_mask__{token}__")]
                if occ_ref:
                    raise RuntimeError(
                        f"{name}: closest layer {part} must not carry an "
                        f"occlusion mask (got {occ_ref})")


def emit_all_views(manifest, grid_cache, grid_mask_cache=None):
    validate_all_cells(manifest, grid_cache)
    os.makedirs(VIEWS_OUT, exist_ok=True)
    files = {}
    for st in manifest["states"]:
        idx = st["idx"]
        name = f"View_{st['token']}.svg"
        with open(os.path.join(VIEWS_OUT, name), "w", encoding="utf-8") as f:
            f.write(compose_view(manifest, idx, grid_cache, grid_mask_cache))
        files[st["token"]] = name
    _verify_composed_views(manifest, grid_cache, files)
    return files


def run_art_generator():
    script = os.path.join(REPO_ROOT, "generate_art.py")
    r = subprocess.run([sys.executable, script], cwd=REPO_ROOT,
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    out = (r.stdout or "") + (r.stderr or "")
    if r.returncode != 0:
        raise RuntimeError("generate_art.py failed:\n" + out[-2000:])
    return out


# ---------------------------------------------------------------------------
# Server state + HTTP dashboard.
# ---------------------------------------------------------------------------
class ServerState:
    def __init__(self):
        self.lock = threading.RLock()
        self.manifest = None
        self.grid_cache = None
        self.grid_mask_cache = None
        self.view_files = {}
        self.error = None
        self.last_gen = None

    def build(self, force_bridge=False):
        with self.lock:
            ensure_bridge(force_bridge)
            self.manifest = run_bridge()
            self.grid_cache = load_all_grid_cells(self.manifest)
            self.grid_mask_cache = load_all_grid_masks(self.manifest)
            self.view_files = emit_all_views(self.manifest, self.grid_cache,
                                             self.grid_mask_cache)
            self.error = None
            return self

    def regenerate(self):
        with self.lock:
            self.last_gen = run_art_generator()
            self.build(force_bridge=True)
            return self.last_gen


STATE = ServerState()


def load_tokens():
    if not os.path.exists(TOKENS_JSON):
        return None
    with open(TOKENS_JSON, "r", encoding="utf-8") as f:
        return json.load(f)


def state_label(st):
    return f"{st['token']}  ({st['yaw']}, pitch {st['centerPitch']:g})"


def view_cell_html(idx, manifest, files, cb):
    st = manifest["states"][idx]
    name = files.get(st["token"])
    img = ""
    if name:
        img = (f'<img src="/views/{html.escape(name)}?v={cb}" '
               f'alt="{html.escape(st["token"])}">')
    else:
        img = '<div class="noart">no view</div>'
    yaw = f"{st['centerYaw']:g}°" if st["centerYaw"] else "0°"
    pitch = f"{st['centerPitch']:g}°" if st["centerPitch"] else "0°"
    return (f'<figure class="view">\n  {img}\n'
            f'  <figcaption>{html.escape(st["token"])}'
            f'<span class="ang">yaw {yaw} · pitch {pitch}</span></figcaption>\n'
            f'</figure>')


def gallery_html(tokens, cb):
    if not tokens:
        return ("<p class='noart'>Art library not generated yet - press "
                "&quot;Regenerate all art&quot;.</p>")
    sections = []
    for feature, info in tokens["features"].items():
        files = info.get("files", [])
        redundant = set(info.get("redundant", []))
        cells = []
        for name in files:
            badge = ""
            if name[:-4] in redundant:
                badge = ' <span class="badge">EMPTY (E10)</span>'
            cells.append(
                f'<figure class="piece">'
                f'<img src="/art/{html.escape(feature)}/{html.escape(name)}'
                f'?v={cb}" alt="{html.escape(name)}" loading="lazy">'
                f'<figcaption>{html.escape(name)}{badge}</figcaption>'
                f'</figure>')
        part = info.get("part", "")
        sections.append(
            f'<h2>{html.escape(feature)}'
            f'<span class="meta">part {html.escape(part)} · '
            f'{len(files)} files · {info.get("cells", 0)} cells</span></h2>'
            f'<div class="gallery-grid">' + "\n".join(cells) + "</div>")
    return "\n".join(sections)


def page_html(cb):
    try:
        manifest = STATE.manifest
    except Exception:
        manifest = None
    if manifest is None:
        return ("<h1>Art viewer unavailable</h1>"
                f"<p class='err'>{html.escape(str(STATE.error or 'no state'))}</p>")

    grid = ('<div class="grid3">' +
            "\n".join(view_cell_html(i, manifest, STATE.view_files, cb)
                      for row in GRID_LAYOUT for i in row) +
            "</div>")
    strip = ('<div class="strip">' +
             "\n".join(view_cell_html(i, manifest, STATE.view_files, cb)
                       for i in STRIP) +
             "</div>")

    tokens = load_tokens()
    total_files = tokens["total_files"] if tokens else 0
    total_cells = tokens["total_cells"] if tokens else 0

    return PAGE_TEMPLATE.replace("@GRID@", grid) \
        .replace("@STRIP@", strip) \
        .replace("@GALLERY@", gallery_html(tokens, cb)) \
        .replace("@STATUS@", html.escape(str(STATE.error or ""))) \
        .replace("@FILES@", str(total_files)) \
        .replace("@CELLS@", str(total_cells))


class ViewerHandler(BaseHTTPRequestHandler):
    server_version = "FaceParallaxArtViewer/1"

    def log_message(self, fmt, *args):
        pass

    def _send(self, code, body, ctype):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj), "application/json")

    def _serve_svg(self, root, name):
        if not FILE_RE.match(name):
            self._send(404, "bad name", "text/plain")
            return
        path = os.path.join(root, name)
        if not os.path.exists(path):
            self._send(404, "missing", "text/plain")
            return
        with open(path, "rb") as f:
            self._send(200, f.read(), "image/svg+xml")

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        try:
            if path == "/" or path == "/index.html":
                cb = int(time.time())
                self._send(200, page_html(cb), "text/html; charset=utf-8")
            elif path == "/api/status":
                tokens = load_tokens()
                self._json({
                    "ok": STATE.manifest is not None,
                    "error": STATE.error,
                    "totalFiles": tokens["total_files"] if tokens else 0,
                    "totalCells": tokens["total_cells"] if tokens else 0,
                    "views": sorted(STATE.view_files.values()),
                    "lastGen": STATE.last_gen,
                    "bridge": os.path.basename(BRIDGE_EXE)
                              if os.path.exists(BRIDGE_EXE) else None,
                })
            elif path == "/api/views":
                manifest = STATE.manifest or run_bridge()
                self._json({
                    "states": manifest["states"],
                    "grid": GRID_LAYOUT,
                    "strip": STRIP,
                    "files": STATE.view_files,
                })
            elif path.startswith("/views/"):
                self._serve_svg(VIEWS_OUT, path[len("/views/"):])
            elif path.startswith("/art/"):
                rest = path[len("/art/"):]
                if "/" not in rest:
                    self._send(404, "bad path", "text/plain")
                    return
                feature, name = rest.split("/", 1)
                feature_dir = os.path.join(ART_ROOT, feature)
                if not os.path.isdir(feature_dir):
                    self._send(404, "unknown feature", "text/plain")
                    return
                self._serve_svg(feature_dir, name)
            elif path == "/favicon.ico":
                self._send(404, "", "text/plain")
            else:
                self._send(404, "not found", "text/plain")
        except Exception as e:
            self._send(500, str(e), "text/plain")

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        try:
            if path == "/regenerate":
                out = STATE.regenerate()
                lines = [l for l in out.splitlines() if l.startswith("[OK]")]
                self._json({
                    "ok": True,
                    "views": len(STATE.view_files),
                    "okLines": lines[-12:],
                    "output": out[-2000:],
                })
            elif path == "/rebuild":
                STATE.build(force_bridge=True)
                self._json({"ok": True, "views": len(STATE.view_files)})
            else:
                self._json({"ok": False, "error": "unknown endpoint"}, 404)
        except Exception as e:
            self._json({"ok": False, "error": str(e)}, 500)


PAGE_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>FaceParallax Art Viewer</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  html { scroll-behavior: smooth; }
  body {
    margin: 0;
    background: #121418; color: #e8e8ec;
    font-family: 'Segoe UI', system-ui, sans-serif;
  }
  /* top bar lives INSIDE the views section: the first viewport is exactly
     topbar + character views, the sections below are reached by normal
     page scrolling */
  .topbar {
    flex: 0 0 auto; padding: 8px 0 6px;
    border-bottom: 1px solid #262a34; margin-bottom: 6px;
  }
  h1 { font-size: 17px; margin: 0 0 8px; color: #f0f2f6; }
  h2 { font-size: 14px; margin: 10px 0 8px; color: #dfe3ea; }
  .meta { color: #9aa2b1; font-weight: normal; font-size: 12px; margin-left: 8px; }
  .bar { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
  button {
    background: #2b6cb0; color: #fff; border: 0; border-radius: 6px;
    padding: 7px 14px; font-size: 13px; cursor: pointer;
  }
  button:hover { background: #347cc4; }
  button:disabled { opacity: .5; cursor: wait; }
  #status { font-size: 12px; color: #9aa2b1; }
  #status.err { color: #ff8f8f; }
  :focus-visible { outline: 2px solid #4c9aff; outline-offset: 2px; }

  /* character views: exactly one full window height. The grid gets every
     remaining pixel, so the art always fits the window height; on windows
     shorter than the floor the page scrolls instead of collapsing. The
     floor sizes guarantee every grid row stays >= 200px tall, so the art
     never shrinks below a readable size and nothing overflows onto the
     strip below (no element overlap). */
  section.views {
    height: 100vh; min-height: 860px;
    display: flex; flex-direction: column;
    padding: 6px 24px 8px; overflow: hidden;
  }
  .grid3 {
    flex: 1 1 auto; min-height: 620px;
    display: grid; grid-template-columns: repeat(3, 1fr);
    grid-template-rows: repeat(3, minmax(200px, 1fr)); gap: 10px;
  }
  .strip {
    flex: 0 0 auto; height: 13vh; min-height: 100px;
    display: grid; grid-template-columns: repeat(5, 1fr);
    grid-template-rows: 1fr; gap: 10px; margin-top: 8px;
  }
  figure.view { margin: 0; display: flex; flex-direction: column; min-height: 0; }
  figure.view img {
    flex: 1 1 auto; min-height: 0; width: 100%;
    object-fit: contain; cursor: zoom-in;
    /* light canvas behind the dark line art: max contrast */
    background: #f4f2ed; border: 1px solid #2c303b; border-radius: 8px;
  }
  figure.view img:hover { border-color: #4c9aff; }

  /* full art library: a normal section below the fold, reached by scrolling */
  section.gallery-sec {
    padding: 8px 24px 26px; border-top: 1px solid #262a34;
  }
  .gallery-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 10px; }
  figure.piece { margin: 0; display: flex; flex-direction: column; }
  figure.piece img {
    height: 120px; width: 100%; object-fit: contain; cursor: zoom-in;
    background: #f4f2ed; border: 1px solid #2c303b; border-radius: 6px;
    transition: transform .15s ease, box-shadow .15s ease, border-color .15s ease;
  }
  figure.piece img:hover {
    transform: scale(1.05); border-color: #4c9aff;
    box-shadow: 0 6px 18px rgba(0, 0, 0, .5);
  }
  figcaption {
    font-size: 12px; color: #c6cad4; margin-top: 5px;
    text-align: center; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .ang { color: #9aa2b1; display: block; font-size: 11px; }
  .badge { background: #6d3333; color: #ffd9d9; border-radius: 3px; padding: 0 4px; font-size: 9px; }
  .noart { color: #ff8f8f; font-size: 12px; }
  img { -webkit-user-drag: none; user-select: none; }

  /* lightbox: click any art piece to enlarge */
  .lb { position: fixed; inset: 0; z-index: 100; display: flex; align-items: center; justify-content: center; }
  .lb[hidden] { display: none; }
  .lb-backdrop { position: absolute; inset: 0; background: rgba(8, 9, 12, .84); }
  .lb-card {
    position: relative; margin: 0; max-width: 94vw; max-height: 96vh;
    display: flex; flex-direction: column;
  }
  .lb-card img {
    min-width: 300px; min-height: 300px;
    max-width: 92vw; max-height: 88vh; object-fit: contain;
    background: #f4f2ed; border-radius: 10px; border: 1px solid #3a3f4c;
  }
  .lb-close {
    position: absolute; top: -14px; right: -14px; width: 34px; height: 34px;
    border-radius: 50%; background: #2b6cb0; color: #fff; border: 0;
    font-size: 18px; line-height: 1; cursor: pointer;
  }
  .lb-close:hover { background: #347cc4; }
  .lb-cap { text-align: center; margin-top: 10px; color: #dfe3ea; font-size: 13px; }
</style>
</head>
<body>
<section class="views">
  <div class="topbar">
    <h1>FaceParallax Art Viewer</h1>
    <div class="bar">
      <button id="btn-regen" onclick="regenerate()">Regenerate all art</button>
      <button id="btn-rebuild" onclick="rebuild()">Rebuild views</button>
      <span id="status">@STATUS@</span>
      <span class="meta">@FILES@ files · @CELLS@ cells (Art/_tokens.json)</span>
    </div>
  </div>
  <h2>Character views <span class="meta">composed by art_viewer_bridge.exe from the canonical contracts (front center, top on top, left on left)</span></h2>
  @GRID@
  @STRIP@
</section>
<section class="gallery-sec">
  <h2>Full art library <span class="meta">every authored SVG, per feature — click any piece to enlarge</span></h2>
  @GALLERY@
</section>
<div id="lightbox" class="lb" hidden>
  <div class="lb-backdrop"></div>
  <figure class="lb-card">
    <button class="lb-close" aria-label="Close">×</button>
    <img id="lb-img" alt="">
    <figcaption class="lb-cap" id="lb-cap"></figcaption>
  </figure>
</div>
<script>
function setStatus(msg, ok){ const el=document.getElementById('status'); el.textContent=msg; el.className=ok?'':'err'; }
async function post(path){ const r=await fetch(path,{method:'POST'}); return r.json(); }
async function regenerate(){
  const b=document.getElementById('btn-regen'); b.disabled=true;
  setStatus('Running generate_art.py ...');
  try{
    const j=await post('/regenerate');
    if(j.ok){ setStatus('Regenerated: '+j.views+' views composed · '+(j.okLines||[]).join(' · '), true); setTimeout(()=>location.reload(),900); }
    else setStatus('FAILED: '+j.error);
  }catch(e){ setStatus('Request failed: '+e); }
  finally{ b.disabled=false; }
}
async function rebuild(){
  const b=document.getElementById('btn-rebuild'); b.disabled=true;
  setStatus('Recompiling bridge + re-emitting views ...');
  try{
    const j=await post('/rebuild');
    if(j.ok){ setStatus('Rebuilt: '+j.views+' views', true); setTimeout(()=>location.reload(),500); }
    else setStatus('FAILED: '+j.error);
  }catch(e){ setStatus('Request failed: '+e); }
  finally{ b.disabled=false; }
}
const lb=document.getElementById('lightbox');
const lbImg=document.getElementById('lb-img');
const lbCap=document.getElementById('lb-cap');
function openLb(src, cap){ lbImg.src=src; lbImg.alt=cap; lbCap.textContent=cap; lb.hidden=false; }
function closeLb(){ lb.hidden=true; lbImg.removeAttribute('src'); }
document.addEventListener('click', (e) => {
  const img = e.target.closest('figure img:not(#lb-img)');
  if (img) {
    const fig = img.closest('figure');
    const cap = (fig && fig.querySelector('figcaption')?.textContent) || img.alt;
    openLb(img.src, cap);
    return;
  }
  if (e.target.closest('.lb-backdrop') || e.target.closest('.lb-close')) closeLb();
});
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeLb(); });
</script>
</body>
</html>
"""


# ---------------------------------------------------------------------------
# Entry points.
# ---------------------------------------------------------------------------
def emit_only():
    STATE.build()
    manifest = STATE.manifest
    print(f"[OK] bridge: {BRIDGE_EXE}")
    for st in manifest["states"]:
        idx = st["idx"]
        n = sum(1 for e in manifest["visibility"]
                if e["state"] == idx and e["visible"])
        print(f"[OK] View_{st['token']}.svg  state {idx:2d}  {n:2d} layers  "
              f"yaw {st['centerYaw']:7g}  pitch {st['centerPitch']:7g}")
    print(f"[OK] {len(STATE.view_files)} composed views in {VIEWS_OUT}")
    print(f"[OK] Feature 1: per-cell eye-silhouette <mask> defs carried from "
          f"grid <defs> into every view's <defs> (no dangling references)")
    print(f"[OK] Feature 2: EyeL/EyeR masked to face silhouette "
          f"(<mask id=\"face_mask__<state>\"> white-on-black containment)")
    print(f"[OK] Feature 3: every non-closest layer masked to OUTSIDE every "
          f"in-front layer's silhouette (per-layer <mask> white rect + black "
          f"occluder silhouettes — reliable union, no evenodd holes)")
    tokens = load_tokens()
    if tokens:
        print(f"[OK] library manifest: {tokens['total_files']} files, "
              f"{tokens['total_cells']} cells")


def serve(port, open_browser):
    STATE.build()
    print(f"[OK] bridge: {BRIDGE_EXE}")
    print(f"[OK] {len(STATE.view_files)} composed views in {VIEWS_OUT}")
    url = f"http://127.0.0.1:{port}/"
    print(f"[OK] dashboard: {url}")
    if open_browser:
        webbrowser.open(url)
    ThreadingHTTPServer.allow_reuse_address = True
    httpd = ThreadingHTTPServer(("127.0.0.1", port), ViewerHandler)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


def main():
    import argparse
    ap = argparse.ArgumentParser(description="FaceParallax art viewer")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-open", action="store_true",
                    help="do not open the browser")
    ap.add_argument("--emit-only", action="store_true",
                    help="compose the view SVGs and exit (headless)")
    args = ap.parse_args()
    if args.emit_only:
        emit_only()
    else:
        serve(args.port, not args.no_open)


if __name__ == "__main__":
    main()
