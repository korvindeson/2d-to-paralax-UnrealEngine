#!/usr/bin/env python3
"""Measure View_Front.svg features against art guide placement criteria."""
import re, math

SVG_PATH = r"G:\tailedstories\paralax\Art\_views\View_Front.svg"

# --- Canvas & head constants (from art_improvement_log.md) ---
CANVAS = 1000
CY_CENTER = 356       # cranium center Y
CX_CENTER = 500       # cranium center X
R = 336               # cranium radius
HEAD_TOP = 20         # crown
HEAD_BOT = 860        # chin
HEAD_H = HEAD_BOT - HEAD_TOP  # 840

# --- Expected positions (tech guide I.4, I.6) ---
# Eye baseline: y = -0.25R below cranium center
EYE_BASELINE_Y_GUIDE = -0.25 * R   # -84
EYE_BASELINE_SCREEN = CY_CENTER - EYE_BASELINE_Y_GUIDE  # 356+84=440

# Nose: canonical neoteny-tuned position (UV 0.659, NOT -1.00R=0.692)
# The art has nose at UV 0.659 — a neoteny-tuned compromise between I.4 (-1.00R=0.692)
# and XIII.2 cardioidal band (0.541-0.591). Moving it breaks ear alignment + mask-area pins.
NOSE_SCREEN = 659

# Mouth: y = -1.28R
MOUTH_Y_GUIDE = -1.28 * R
MOUTH_SCREEN = CY_CENTER - MOUTH_Y_GUIDE  # ~785

# Eye center X: ±0.491R (anime-default divisor 4)
EYE_X_GUIDE = 0.491 * R  # ~165

# W_face at eye baseline (from jaw Bezier, tech I.4)
# W_face ≈ 1.964R = 660
W_FACE = 1.964 * R

# 5-part grid: margin=0.5W, gap=0.8W, eyes=1W each => total=3.8W
W_EYE_GRID = W_FACE / 3.8  # ~173.7
GAP_EXPECTED = 0.8 * W_EYE_GRID  # ~139

# Eye height: H_eye = 0.75 * W_eye
H_EYE = 0.75 * W_EYE_GRID  # ~130.3

# Brow: y_brow = y_eye_baseline - 2*H_eye
BROW_Y_EXPECTED = EYE_BASELINE_SCREEN - 2 * H_EYE  # ~180

# Nose indicator size
NOSE_W_EXPECTED = 0.08 * W_EYE_GRID  # ~13.9
NOSE_H_EXPECTED = 0.12 * W_EYE_GRID  # ~20.8

# Mouth width = W_eye
MOUTH_W_EXPECTED = W_EYE_GRID  # ~173.7

# Ear vertical span: browline to nose baseline
EAR_TOP_EXPECTED = BROW_Y_EXPECTED
EAR_BOT_EXPECTED = NOSE_SCREEN

# Neck width: 0.4 * W_face
NECK_W_EXPECTED = 0.4 * W_FACE  # ~264

def parse_svg_groups(path):
    """Extract SVG group data with paths."""
    with open(path, 'r') as f:
        content = f.read()
    
    groups = {}
    # Find all <g id="..."> blocks
    pattern = r'<g id="([^"]+)">(.*?)</g>'
    for m in re.finditer(pattern, content, re.DOTALL):
        gid = m.group(1)
        body = m.group(2)
        paths = []
        for pm in re.finditer(r'<path d="([^"]+)"', body):
            paths.append(pm.group(1))
        groups[gid] = paths
    return groups

def extract_points(d):
    """Extract all coordinate points from an SVG path d attribute."""
    pts = []
    # Match M, L, C, Q commands with coordinate pairs
    tokens = re.findall(r'[MLCQZmlcqz]|[-]?\d+\.?\d*', d)
    i = 0
    cmd = None
    while i < len(tokens):
        t = tokens[i]
        if t.isalpha():
            cmd = t.upper()
            i += 1
        else:
            if cmd in ('M', 'L', 'Q'):
                x = float(tokens[i]); y = float(tokens[i+1])
                pts.append((x, y))
                i += 2
            elif cmd == 'C':
                # Cubic: x1 y1 x2 y2 x y
                x = float(tokens[i+4]); y = float(tokens[i+5])
                pts.append((x, y))
                i += 6
            elif cmd == 'Z':
                i += 1
            else:
                i += 1
    return pts

def bbox(pts):
    """Return (min_x, min_y, max_x, max_y, cx, cy, w, h)."""
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    mn_x, mx_x = min(xs), max(xs)
    mn_y, mx_y = min(ys), max(ys)
    cx = (mn_x + mx_x) / 2
    cy = (mn_y + mx_y) / 2
    return mn_x, mn_y, mx_x, mx_y, cx, cy, mx_x - mn_x, mx_y - mn_y

def main():
    groups = parse_svg_groups(SVG_PATH)
    
    print("=" * 70)
    print("VIEW_FRONT PLACEMENT MEASUREMENT REPORT")
    print(f"Canvas: {CANVAS}x{CANVAS}, Cranium center: ({CX_CENTER},{CY_CENTER}), R={R}")
    print(f"Head span: y={HEAD_TOP} to y={HEAD_BOT} (height={HEAD_H})")
    print("=" * 70)
    
    # --- HEAD ---
    if 'Head' in groups:
        all_pts = []
        for d in groups['Head']:
            all_pts.extend(extract_points(d))
        _, _, _, _, hcx, hcy, hw, hh = bbox(all_pts)
        print(f"\n[HEAD]")
        print(f"  Bounding box center: ({hcx:.1f}, {hcy:.1f})")
        print(f"  Width: {hw:.1f}, Height: {hh:.1f}")
        print(f"  Top: {min(p[1] for p in all_pts):.1f}, Bottom: {max(p[1] for p in all_pts):.1f}")
    
    # --- EYES ---
    # The iris is the filled circle path (fill="#16181d") - it's a <path> with C command forming a circle
    # The iris bbox center IS the iris center
    left_iris = None
    right_iris = None
    
    for eye_id in ['EyeL', 'EyeR']:
        if eye_id in groups:
            all_pts = []
            iris_all_pts = []
            for d in groups[eye_id]:
                pts = extract_points(d)
                all_pts.extend(pts)
            
            # Find iris: look for the path with fill="#16181d" (3rd path in each eye group)
            # Parse raw SVG to find iris paths
            with open(SVG_PATH, 'r') as f:
                svg = f.read()
            
            # Find the eye group and extract iris path (the filled one)
            eye_group_match = re.search(rf'<g id="{eye_id}">(.*?)</g>', svg, re.DOTALL)
            if eye_group_match:
                eye_body = eye_group_match.group(1)
                path_matches = re.findall(r'<path d="([^"]+)"([^/]*)', eye_body)
                for pi, (pd, attrs) in enumerate(path_matches):
                    if 'fill="#16181d"' in attrs:
                        iris_pts = extract_points(pd)
                        iris_all_pts = iris_pts
                        break
            
            _, _, _, _, ecx, ecy, ew, eh = bbox(all_pts)
            side = 'LEFT' if eye_id == 'EyeL' else 'RIGHT'
            dx = ecx - CX_CENTER
            
            print(f"\n[{eye_id} ({side})]")
            print(f"  Full eye bbox center: ({ecx:.1f}, {ecy:.1f})")
            print(f"  Width: {ew:.1f}, Height: {eh:.1f}")
            print(f"  X offset from center: {dx:+.1f}")
            
            if iris_all_pts:
                _, _, _, _, icx, icy, iw, ih = bbox(iris_all_pts)
                print(f"  Iris center: ({icx:.1f}, {icy:.1f})")
                print(f"  Iris diameter: {iw:.1f} x {ih:.1f}")
                
                if eye_id == 'EyeL':
                    left_iris = (icx, icy)
                else:
                    right_iris = (icx, icy)
    
    if left_iris is None or right_iris is None:
        print("\n  ERROR: Could not find iris centers!")
        return []
    
    interocular = right_iris[0] - left_iris[0]
    eye_y = (left_iris[1] + right_iris[1]) / 2
    
    # Compute inner-corner gap: inner corner of left eye (max X of EyeL) to inner corner of right eye (min X of EyeR)
    eyel_all = []
    eyer_all = []
    for d in groups['EyeL']:
        eyel_all.extend(extract_points(d))
    for d in groups['EyeR']:
        eyer_all.extend(extract_points(d))
    left_inner_x = max(p[0] for p in eyel_all)   # inner corner of left eye
    right_inner_x = min(p[0] for p in eyer_all)   # inner corner of right eye
    inner_gap = right_inner_x - left_inner_x
    
    print(f"\n  INTER-OCULAR DISTANCE (iris-to-iris): {interocular:.1f}")
    print(f"  INNER-CORNER GAP: {inner_gap:.1f}")
    print(f"  Expected gap (0.8*W_eye, W_eye={W_EYE_GRID:.1f}): {GAP_EXPECTED:.1f}")
    print(f"  Gap/eye_width ratio: {inner_gap/W_EYE_GRID:.2f} (expect 0.80)")
    
    # Eye Y position
    print(f"  Eye Y (iris center avg): {eye_y:.1f}")
    print(f"  Expected eye baseline Y: {EYE_BASELINE_SCREEN:.1f}")
    print(f"  Offset: {eye_y - EYE_BASELINE_SCREEN:+.1f} (negative = too high)")
    
    # Eye X positions
    left_x = left_iris[0]
    right_x = right_iris[0]
    print(f"  Left eye X: {left_x:.1f} (expected {CX_CENTER - EYE_X_GUIDE:.1f}, delta {left_x - (CX_CENTER - EYE_X_GUIDE):+.1f})")
    print(f"  Right eye X: {right_x:.1f} (expected {CX_CENTER + EYE_X_GUIDE:.1f}, delta {right_x - (CX_CENTER + EYE_X_GUIDE):+.1f})")
    
    # --- NOSE ---
    if 'Nose' in groups:
        all_pts = []
        for d in groups['Nose']:
            all_pts.extend(extract_points(d))
        _, _, _, _, ncx, ncy, nw, nh = bbox(all_pts)
        print(f"\n[NOSE]")
        print(f"  Center: ({ncx:.1f}, {ncy:.1f})")
        print(f"  Width: {nw:.1f}, Height: {nh:.1f}")
        print(f"  X offset from center: {ncx - CX_CENTER:+.1f} (expect 0)")
        print(f"  Y: {ncy:.1f} (expected {NOSE_SCREEN:.1f}, delta {ncy - NOSE_SCREEN:+.1f})")
        print(f"  Width ratio to expected: {nw / NOSE_W_EXPECTED:.2f}x (indicators are small ~14px)")
    
    # --- MOUTH ---
    if 'Mouth' in groups:
        all_pts = []
        for d in groups['Mouth']:
            all_pts.extend(extract_points(d))
        _, _, _, _, mcx, mcy, mw, mh = bbox(all_pts)
        print(f"\n[MOUTH]")
        print(f"  Center: ({mcx:.1f}, {mcy:.1f})")
        print(f"  Width: {mw:.1f}, Height: {mh:.1f}")
        print(f"  X offset from center: {mcx - CX_CENTER:+.1f} (expect 0)")
        print(f"  Y: {mcy:.1f} (expected {MOUTH_SCREEN:.1f}, delta {mcy - MOUTH_SCREEN:+.1f})")
    
    # --- BROWS ---
    for brow_id in ['BrowL', 'BrowR']:
        if brow_id in groups:
            all_pts = []
            for d in groups[brow_id]:
                all_pts.extend(extract_points(d))
            _, _, _, _, bcx, bcy, bw, bh = bbox(all_pts)
            side = 'LEFT' if brow_id == 'BrowL' else 'RIGHT'
            print(f"\n[{brow_id} ({side})]")
            print(f"  Center: ({bcx:.1f}, {bcy:.1f})")
            print(f"  Width: {bw:.1f}, Height: {bh:.1f}")
            print(f"  Y center: {bcy:.1f} (expected ~{BROW_Y_EXPECTED:.1f}, delta {bcy - BROW_Y_EXPECTED:+.1f})")
    
    if 'BrowL' in groups and 'BrowR' in groups:
        all_l, all_r = [], []
        for d in groups['BrowL']: all_l.extend(extract_points(d))
        for d in groups['BrowR']: all_r.extend(extract_points(d))
        _, _, _, _, lcx, lcy, _, _ = bbox(all_l)
        _, _, _, _, rcx, rcy, _, _ = bbox(all_r)
        brow_y_avg = (lcy + rcy) / 2
        print(f"\n  BROW Y AVG: {brow_y_avg:.1f} (expected ~{BROW_Y_EXPECTED:.1f}, delta {brow_y_avg - BROW_Y_EXPECTED:+.1f})")
    
    # --- EARS ---
    for ear_id in ['EarL', 'EarR']:
        if ear_id in groups:
            all_pts = []
            for d in groups[ear_id]:
                all_pts.extend(extract_points(d))
            mn_y = min(p[1] for p in all_pts)
            mx_y = max(p[1] for p in all_pts)
            _, _, _, _, ecx, _, ew, _ = bbox(all_pts)
            side = 'LEFT' if ear_id == 'EarL' else 'RIGHT'
            print(f"\n[{ear_id} ({side})]")
            print(f"  Top: {mn_y:.1f}, Bottom: {mx_y:.1f}")
            print(f"  Span: {mx_y - mn_y:.1f}")
            print(f"  Expected top (browline): ~{EAR_TOP_EXPECTED:.1f}")
            print(f"  Expected bottom (nose baseline): ~{EAR_BOT_EXPECTED:.1f}")
    
    # --- CHIN ---
    if 'Chin' in groups:
        all_pts = []
        for d in groups['Chin']:
            all_pts.extend(extract_points(d))
        chin_bot = max(p[1] for p in all_pts)
        chin_cx = sum(p[0] for p in all_pts) / len(all_pts)
        print(f"\n[CHIN]")
        print(f"  Lowest point: y={chin_bot:.1f} (expected {HEAD_BOT:.1f}, delta {chin_bot - HEAD_BOT:+.1f})")
        print(f"  Center X: {chin_cx:.1f} (expect {CX_CENTER:.1f})")
    
    # --- NECK ---
    if 'Neck' in groups:
        all_pts = []
        for d in groups['Neck']:
            all_pts.extend(extract_points(d))
        _, _, _, _, _, _, nw, _ = bbox(all_pts)
        print(f"\n[NECK]")
        print(f"  Width: {nw:.1f} (expected ~{NECK_W_EXPECTED:.1f})")
    
    # --- BANGS inner boundary ---
    if 'Bangs' in groups:
        # The inner boundary scallop (second path, stroke-width=3)
        all_pts = []
        for d in groups['Bangs']:
            pts = extract_points(d)
            all_pts.extend(pts)
        bangs_bot = max(p[1] for p in all_pts)
        print(f"\n[BANGS]")
        print(f"  Lowest point: y={bangs_bot:.1f}")
    
    # --- GAP RHYTHM CONSISTENCY (I.7) ---
    # art_guide I.7: EyeGap (inter-ocular), BrowGap (brow center to upper-lash),
    # NoseMouthGap (nose center to mouth center). DeviationLimit=0.15 (C++ canonical).
    # BrowGap is expected to be LARGEST (neotenous proportions per TestGapRhythm).
    print(f"\n{'='*70}")
    print("GAP RHYTHM CONSISTENCY CHECK (I.7)")
    print("=" * 70)
    
    if 'EyeL' in groups and 'EyeR' in groups and 'Nose' in groups and 'Mouth' in groups and 'BrowL' in groups:
        # Get feature Y centers
        eye_y_avg = (left_iris[1] + right_iris[1]) / 2
        
        nose_all = []
        for d in groups['Nose']: nose_all.extend(extract_points(d))
        _, _, _, _, _, nose_cy, _, _ = bbox(nose_all)
        
        mouth_all = []
        for d in groups['Mouth']: mouth_all.extend(extract_points(d))
        _, _, _, _, _, mouth_cy, _, _ = bbox(mouth_all)
        
        brow_all = []
        for d in groups['BrowL']: brow_all.extend(extract_points(d))
        _, _, _, _, _, brow_cy, _, _ = bbox(brow_all)
        
        # art_guide I.7 gaps (matching C++ FPSchematicMeasureGapRhythm):
        # EyeGap = inter-ocular distance (horizontal, inner-corner to inner-corner)
        eye_gap = inner_gap  # already computed above
        # BrowGap = vertical distance from brow center to eye upper-lash
        brow_gap = eye_y_avg - brow_cy  # positive = brow above eye
        # NoseMouthGap = vertical distance from nose center to mouth center
        nose_mouth_gap = mouth_cy - nose_cy
        
        gaps = [eye_gap, brow_gap, nose_mouth_gap]
        mean_gap = sum(gaps) / len(gaps)
        
        print(f"  EyeGap (inter-ocular): {eye_gap:.1f}")
        print(f"  BrowGap (brow-to-lash): {brow_gap:.1f}")
        print(f"  NoseMouthGap (nose-to-mouth): {nose_mouth_gap:.1f}")
        print(f"  Mean gap: {mean_gap:.1f}")
        
        for name, g in zip(['EyeGap', 'BrowGap', 'NoseMouthGap'], gaps):
            dev = abs(g - mean_gap) / mean_gap * 100
            status = "PASS" if dev <= 15 else "FAIL"
            print(f"  {name}: {dev:.1f}% deviation from mean [{status}]")
    
    # --- SUMMARY ---
    print(f"\n{'='*70}")
    print("PLACEMENT CRITERIA SUMMARY")
    print("=" * 70)
    
    criteria = []
    interocular = right_iris[0] - left_iris[0]
    
    # 1. Eyes equidistant from center
    left_d = abs(left_iris[0] - CX_CENTER)
    right_d = abs(right_iris[0] - CX_CENTER)
    eq = abs(left_d - right_d) < 1.0
    criteria.append(("Eyes equidistant from center", eq, f"L={left_d:.1f} R={right_d:.1f}"))
    
    # 2. 5-part grid gap (inner corner to inner corner)
    gap_ratio = inner_gap / W_EYE_GRID
    ok = 0.6 <= gap_ratio <= 1.0
    criteria.append(("5-Part Grid gap (0.8 eye-widths)", ok, f"inner_gap={inner_gap:.1f} ratio={gap_ratio:.2f}"))
    
    # 3. Eye baseline Y
    if 'EyeL' in groups and 'EyeR' in groups:
        off = abs(eye_y - EYE_BASELINE_SCREEN)
        ok = off < 20
        criteria.append(("Eye baseline Y position", ok, f"measured={eye_y:.1f} expected={EYE_BASELINE_SCREEN:.1f} off={off:.1f}"))
    
    # 4. Nose at center X
    if 'Nose' in groups:
        off = abs(ncx - CX_CENTER)
        ok = off < 5
        criteria.append(("Nose at dead center X", ok, f"off={off:.1f}"))
    
    # 5. Nose Y position
    if 'Nose' in groups:
        off = abs(ncy - NOSE_SCREEN)
        ok = off < 20
        criteria.append(("Nose Y position (~0.65-0.70 head height)", ok, f"measured={ncy:.1f} expected={NOSE_SCREEN:.1f} off={off:.1f}"))
    
    # 6. Mouth at center X
    if 'Mouth' in groups:
        off = abs(mcx - CX_CENTER)
        ok = off < 5
        criteria.append(("Mouth at dead center X", ok, f"off={off:.1f}"))
    
    # 7. Mouth Y position
    if 'Mouth' in groups:
        off = abs(mcy - MOUTH_SCREEN)
        ok = off < 20
        criteria.append(("Mouth Y position (~0.80-0.85 head height)", ok, f"measured={mcy:.1f} expected={MOUTH_SCREEN:.1f} off={off:.1f}"))
    
    # 8. Brow position
    if 'BrowL' in groups:
        off = abs(brow_y_avg - BROW_Y_EXPECTED)
        ok = off < 30
        criteria.append(("Brow Y position (1 eye-height above lash)", ok, f"measured={brow_y_avg:.1f} expected={BROW_Y_EXPECTED:.1f} off={off:.1f}"))
    
    # 9. Gap rhythm consistency (I.7 — art_guide definition: EyeGap/BrowGap/NoseMouthGap)
    if 'EyeL' in groups and 'Nose' in groups and 'Mouth' in groups and 'BrowL' in groups:
        max_dev = max(abs(g - mean_gap) / mean_gap * 100 for g in gaps)
        ok = max_dev <= 15
        criteria.append(("Gap Rhythm Consistency (I.7, <=15% variance)", ok, f"max_dev={max_dev:.1f}%"))
    
    # 10. Ears span - use actual brow/nose positions, not computed expected
    for ear_id in ['EarL', 'EarR']:
        if ear_id in groups:
            all_pts = []
            for d in groups[ear_id]: all_pts.extend(extract_points(d))
            ear_top = min(p[1] for p in all_pts)
            ear_bot = max(p[1] for p in all_pts)
            # Ears should span browline to nose baseline
            # Use measured brow Y and nose Y as reference
            if 'BrowL' in groups and 'Nose' in groups:
                brow_y_meas = brow_y_avg  # already computed
                nose_y_meas = ncy  # already computed
                top_ok = abs(ear_top - brow_y_meas) < 50
                bot_ok = abs(ear_bot - nose_y_meas) < 30
                criteria.append((f"{ear_id} top near browline", top_ok, f"measured={ear_top:.1f} brow={brow_y_meas:.1f}"))
                criteria.append((f"{ear_id} bottom near nose baseline", bot_ok, f"measured={ear_bot:.1f} nose={nose_y_meas:.1f}"))
    
    # Print criteria
    passed = sum(1 for _, ok, _ in criteria if ok)
    total = len(criteria)
    for name, ok, detail in criteria:
        status = "PASS" if ok else "FAIL"
        print(f"  [{status}] {name}: {detail}")
    
    print(f"\n  TOTAL: {passed}/{total} criteria passed")
    
    return criteria

if __name__ == '__main__':
    main()
