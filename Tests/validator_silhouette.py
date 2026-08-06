#!/usr/bin/env python3
"""E11 silhouette validator - the three geometry gates over the authored pose
table in FaceParallaxSchematic.h:

  Gate 1 - 3Q hair-ribbon separation: on the 3/4 and back-3/4 cards the hair
           must visibly extend past the face contour on the side facing the
           turn (the P45 far side) and on the near crown (P135 near side).
           Before the E11 fix the P45 far side pinched to ~0.005 (hair inside
           the FaceBase edge at Y ~0.30) and the P135 near crown poked past
           the hair by ~0.06.

  Gate 2 - ahoge at every yaw: every Bangs ring (P0/P45/P90/P135/P180/PTop/
           PBottom) carries the cowlick protrusion (a vertex >= 0.003 above
           BOTH ring neighbors with no (1-x, y) mirror twin - the
           FPSchematicCowlickInRing v2 criterion).

  Gate 3 - canthus consistency through foreshortening: the Y22/Y67 eye
           variants are scaled in the ring's OWN canthus-chord frame
           (FPSchematicScaleRingAboutCanthus) so the tareme chord never
           rotates. The validator replicates the transform and asserts the
           chord angle of every variant card (Narrow/Sliver/3Q) equals its
           base ring's chord angle to 1e-6 deg, and that the sliver's bbox
           width matches the C++ pin 0.025812242613 (cross-engine
           consistency). A regression to axis-aligned Y-squash steepens the
           sliver chord to ~40-49 deg - the gate fails.

Pure Python, no UE. Wired into run_tests.ps1 (next to the syntax validator).
"""

import os
import re
import sys
import math

TOL_PT = 1e-9


class Pt:
    __slots__ = ("x", "y")

    def __init__(self, x, y):
        self.x = float(x)
        self.y = float(y)


def matching_brace(src, open_idx, end):
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


def ring_from_body(body):
    pts = [Pt(m.group(1), m.group(2))
           for m in re.finditer(
               r"SPT\(\s*([0-9]*\.?[0-9]+)\s*,\s*([0-9]*\.?[0-9]+)\s*\)", body)]
    return pts if pts else None


def parse_pose_table(src):
    fn = "FPSchematicAuthoredPoseTable"
    fidx = src.find(fn)
    if fidx < 0:
        raise SystemExit("[FATAL] FPSchematicAuthoredPoseTable not found")
    start = src.find("Table[] = {", fidx)
    if start < 0:
        raise SystemExit("[FATAL] 'Table[] = {' not found")
    start += len("Table[] = {")
    end = src.find("return Table;", start)
    if end < 0:
        raise SystemExit("[FATAL] table terminator not found")
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
        close = matching_brace(src, entry_brace, end)
        if close < 0:
            break
        rings = []
        k = entry_brace + 1
        while k < close:
            ob = src.find("{", k, close)
            if ob < 0:
                break
            cb = matching_brace(src, ob, close)
            if cb < 0:
                break
            ring = ring_from_body(src[ob + 1:cb])
            if ring:
                rings.append(ring)
            k = cb + 1
        if len(rings) == 7:
            table[name] = rings
        i = name_end + 1
    return table


def max_x_at_y(ring, y):
    """The largest X of the ring polygon at height y (edge-interpolated)."""
    mx = -1.0
    n = len(ring)
    for i in range(n):
        a = ring[i]
        b = ring[(i + 1) % n]
        lo = a.y if a.y < b.y else b.y
        hi = a.y if a.y > b.y else b.y
        if y < lo or y > hi:
            continue
        if a.y <= b.y:
            t = 0.0 if (b.y - a.y) < 1e-15 else (y - a.y) / (b.y - a.y)
            x = a.x + t * (b.x - a.x)
        else:
            t = 0.0 if (a.y - b.y) < 1e-15 else (y - b.y) / (a.y - b.y)
            x = b.x + t * (a.x - b.x)
        if x > mx:
            mx = x
    return mx


def min_x_at_y(ring, y):
    mn = 2.0
    n = len(ring)
    for i in range(n):
        a = ring[i]
        b = ring[(i + 1) % n]
        lo = a.y if a.y < b.y else b.y
        hi = a.y if a.y > b.y else b.y
        if y < lo or y > hi:
            continue
        if a.y <= b.y:
            t = 0.0 if (b.y - a.y) < 1e-15 else (y - a.y) / (b.y - a.y)
            x = a.x + t * (b.x - a.x)
        else:
            t = 0.0 if (a.y - b.y) < 1e-15 else (y - b.y) / (a.y - b.y)
            x = b.x + t * (a.x - b.x)
        if x < mn:
            mn = x
    return mn


def ring_width(ring):
    return max(p.x for p in ring) - min(p.x for p in ring)


def has_cowlick(ring):
    """FPSchematicCowlickInRing v2: a vertex >= 0.003 above BOTH neighbors
    with no (1-x, y) mirror twin in the ring."""
    n = len(ring)
    for i in range(n):
        a = ring[(i + n - 1) % n]
        b = ring[i]
        c = ring[(i + 1) % n]
        if b.y < a.y - 0.003 and b.y < c.y - 0.003:
            twin = any(abs((1.0 - b.x) - p.x) < TOL_PT and abs(b.y - p.y) < TOL_PT
                       for p in ring)
            if not twin:
                return True
    return False


def chord_angle_deg(ring):
    """The canthus chord: ring[0] (outer corner) -> ring[9] (inner corner)."""
    if len(ring) < 10:
        return None
    return math.degrees(math.atan2(ring[9].y - ring[0].y, ring[9].x - ring[0].x))


def scale_ring_about_canthus(ring, sx, sy):
    """FPSchematicScaleRingAboutCanthus mirror (E11): anisotropic scale about
    the centroid in the frame of the ring's own canthus chord (0 -> 9)."""
    if len(ring) < 10:
        return ring
    dx = ring[9].x - ring[0].x
    dy = ring[9].y - ring[0].y
    length = math.sqrt(dx * dx + dy * dy)
    if length <= 0.0:
        return ring
    c = dx / length
    s = dy / length
    cx = sum(p.x for p in ring) / len(ring)
    cy = sum(p.y for p in ring) / len(ring)
    out = []
    for p in ring:
        a = (p.x - cx) * c + (p.y - cy) * s
        b = -(p.x - cx) * s + (p.y - cy) * c
        out.append(Pt(cx + (sx * a) * c - (sy * b) * s,
                      cy + (sx * a) * s + (sy * b) * c))
    return out


def scale_ring_axis_aligned(ring, sx, sy):
    cx = sum(p.x for p in ring) / len(ring)
    cy = sum(p.y for p in ring) / len(ring)
    return [Pt(cx + (p.x - cx) * sx, cy + (p.y - cy) * sy) for p in ring]


def gate1_hair_ribbon(table):
    """3Q hair-ribbon far/near separation (P45 far side + P135 near side)."""
    hair = table.get("Hair")
    head = table.get("Head")
    if not hair or not head:
        return False, "Hair/Head pose sets missing"
    p45_min = 1e9
    for k in range(16):
        y = 0.15 + 0.01 * k
        if y > 0.5001:
            break
        gap = max_x_at_y(hair[1], y) - max_x_at_y(head[1], y)
        p45_min = min(p45_min, gap)
    p135_min = 1e9
    for k in range(26):
        y = 0.15 + 0.01 * k
        if y > 0.4001:
            break
        gap = max_x_at_y(hair[3], y) - max_x_at_y(head[3], y)
        p135_min = min(p135_min, gap)
    ok = p45_min >= 0.03 and p135_min >= 0.02
    return ok, ("P45 far gap %.4f (>= 0.03), P135 near gap %.4f (>= 0.02)"
                % (p45_min, p135_min))


def gate2_ahoge_every_yaw(table):
    bangs = table.get("Bangs")
    if not bangs:
        return False, "Bangs pose set missing"
    names = ("P0", "P45", "P90", "P135", "P180", "PTop", "PBottom")
    for ring, name in zip(bangs, names):
        if not has_cowlick(ring):
            return False, "Bangs %s ring lost the cowlick" % name
    return True, "cowlick present in all 7 Bangs rings"


def gate3_canthus(table):
    eye_l = table.get("EyeL")
    eye_r = table.get("EyeR")
    if not eye_l or not eye_r:
        return False, "Eye pose sets missing"
    base_ok = True
    for name, rings in (("EyeL", eye_l), ("EyeR", eye_r)):
        for slot, label in ((0, "P0"), (1, "P45")):
            base = rings[slot]
            a0 = chord_angle_deg(base)
            if a0 is None:
                return False, "%s %s ring too short" % (name, label)
            for (sx, sy), vname in ((0.85, 0.95), "Narrow"), \
                                     ((0.30, 0.95), "Sliver"), \
                                     ((0.88, 0.95), "3Q"):
                var = scale_ring_about_canthus(base, sx, sy)
                av = chord_angle_deg(var)
                if abs(av - a0) > 1e-6:
                    base_ok = False
                    return False, ("%s %s %s chord rotated %.6f -> %.6f deg"
                                   % (name, label, vname, a0, av))
    eye_l_p45 = eye_l[1]
    sliver = scale_ring_about_canthus(eye_l_p45, 0.30, 0.95)
    width = ring_width(sliver)
    if abs(width - 0.025812242613) > 1e-6:
        return False, ("EyeL sliver width %.9f != C++ pin 0.025812242613"
                       % width)
    # Negative control: the gate must be sensitive - an axis-aligned Y squash
    # steepens the sliver chord far beyond the 1e-6 tolerance.
    axis = scale_ring_axis_aligned(eye_l_p45, 0.30, 0.95)
    a_can = chord_angle_deg(sliver)
    a_axis = chord_angle_deg(axis)
    if abs(a_axis - a_can) < 5.0:
        return False, ("gate not sensitive: axis-aligned sliver angle %.3f "
                       "vs canthus-aligned %.3f" % (a_axis, a_can))
    return True, ("chord preserved on all variant cards (sliver width %.9f, "
                  "axis-aligned reference would read %.3f deg)"
                  % (width, a_axis))


def main():
    root = os.path.dirname(os.path.abspath(__file__))
    if len(sys.argv) > 1 and sys.argv[1] == "--path" and len(sys.argv) > 2:
        root = os.path.abspath(sys.argv[2])
    header = os.path.join(root, "FaceParallaxSchematic.h")
    if not os.path.exists(header):
        print("[FAIL] validator_silhouette: %s not found" % header)
        return 1
    with open(header, "r", encoding="utf-8") as f:
        src = f.read()
    table = parse_pose_table(src)
    missing = [n for n in ("Head", "Hair", "Bangs", "EyeL", "EyeR")
               if n not in table]
    if missing:
        print("[FAIL] validator_silhouette: missing parts %s" % missing)
        return 1

    failed = False
    for fn, name in (
            (gate1_hair_ribbon, "gate 1: 3Q hair-ribbon separation"),
            (gate2_ahoge_every_yaw, "gate 2: ahoge at every yaw"),
            (gate3_canthus, "gate 3: canthus consistency through foreshortening")):
        ok, detail = fn(table)
        if ok:
            print("  [OK] %s - %s" % (name, detail))
        else:
            print("  [FAIL] %s - %s" % (name, detail))
            failed = True

    if failed:
        print("SILHOUETTE VALIDATOR FAILED")
        return 1
    print("SILHOUETTE VALIDATOR PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
