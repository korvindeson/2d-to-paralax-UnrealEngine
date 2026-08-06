#!/usr/bin/env python3
"""HairFront (Bangs) surgical fix v4.

Goal: Make the ahoge prominent and off-center, add scallop variation.
Strategy: parse SVG path d, modify coordinates directly.
"""
import re
from pathlib import Path

ART_DIR = Path(__file__).parent / "Art" / "HairFront"


def parse_d(d_str):
    tokens = re.findall(r'[MmZzLlHhVvCcSsQqTtAa]|[-+]?(?:\d+\.?\d*|\.\d+)', d_str)
    cmds = []
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        if tok.isalpha():
            cmd = tok
            i += 1
            vals = []
            while i < len(tokens) and not tokens[i].isalpha():
                vals.append(float(tokens[i]))
                i += 1
            cmds.append((cmd, vals))
        else:
            i += 1
    return cmds


def d_str(cmds):
    parts = []
    for cmd, vals in cmds:
        if cmd in ('Z', 'z'):
            parts.append(cmd)
        else:
            parts.append(cmd + ' ' + ' '.join(f'{v:.1f}' for v in vals))
    return ' '.join(parts)


def remove_existing_ahoge(cmds):
    """Remove the tiny existing ahoge peak (the 2 small C segments
    between the ~450,20 and ~500,20 crown peaks in the front view).
    
    The existing ahoge is: ...C 455,9 C 470,5 C 484,9... 
    We look for a sequence of 3 consecutive C commands where:
    - all have y < 15 (tiny spike)
    - the middle one has the lowest y
    Then we remove those 2 segments (the rise and fall).
    """
    result = list(cmds)
    i = 0
    while i < len(result) - 2:
        c0 = result[i]
        c1 = result[i+1]
        c2 = result[i+2]
        if (c0[0] == 'C' and c1[0] == 'C' and c2[0] == 'C'
            and len(c0[1]) >= 6 and len(c1[1]) >= 6 and len(c2[1]) >= 6):
            y0 = c0[1][5]
            y1 = c1[1][5]
            y2 = c2[1][5]
            # All endpoints in the crown zone, middle is the peak
            if y0 < 20 and y1 < 8 and y2 < 20:
                # Check that y1 is the minimum (the spike tip)
                x0, x2 = c0[1][4], c2[1][4]
                if x2 > x0:  # proper left-to-right order
                    del result[i+1:i+3]  # remove the 2 ahoge segments
                    return result, True
        i += 1
    return result, False


def insert_ahoge_at_valley(cmds, x_target, spike_h=55):
    """Insert a wide, prominent ahoge spike at the valley nearest x_target.
    
    Since the crown sits at y≈9 (near top of viewBox), a tall upward spike 
    clips off-canvas. Instead, we create a WIDE, ASYMMETRIC protrusion that 
    extends to the RIGHT and slightly up, making it visually prominent 
    without needing extreme height.
    """
    best_idx = None
    best_score = 999999
    
    for i in range(1, len(cmds) - 1):
        c_prev = cmds[i-1]
        c_curr = cmds[i]
        if c_prev[0] != 'C' or c_curr[0] != 'C':
            continue
        if len(c_prev[1]) < 6 or len(c_curr[1]) < 6:
            continue
        x_end = c_curr[1][4]
        y_end = c_curr[1][5]
        if 5 < y_end < 35:
            dist = abs(x_end - x_target)
            if dist < best_score:
                best_score = dist
                best_idx = i
    
    if best_idx is None:
        return cmds, False
    
    prev_cmd = cmds[best_idx - 1]
    start_x = prev_cmd[1][4]
    start_y = prev_cmd[1][5]
    
    # Wide ahoge: extends significantly RIGHT and UP from crown
    # Crown is at y≈9, viewBox expanded to -60 -40 1120 1080
    # Make it LARGE and clearly off-center to the right
    peak_x = start_x + 65   # extend far to the right (clearly off-center)
    peak_y = start_y - 30    # 30px above crown (visible with expanded viewBox)
    
    # First segment: sweeps right and up to peak (sharp ascent)
    cp1_x = start_x + 15
    cp1_y = start_y - 18
    cp2_x = peak_x - 20
    cp2_y = peak_y + 10
    
    # Second segment: descends back with asymmetric curve (wider, slower descent)
    cp3_x = peak_x + 10
    cp3_y = peak_y + 6
    cp4_x = start_x + 70
    cp4_y = start_y + 5
    end_x = start_x + 68
    end_y = start_y + 6  # back near crown level
    
    ahoge = [
        ('C', [cp1_x, cp1_y, cp2_x, cp2_y, peak_x, peak_y]),
        ('C', [cp3_x, cp3_y, cp4_x, cp4_y, end_x, end_y]),
    ]
    
    new_cmds = cmds[:best_idx] + ahoge + cmds[best_idx:]
    return new_cmds, True


def vary_bottom_scallops(cmds, amount=4.0, y_threshold=200):
    """Vary bottom hem scallop sizes for visual rhythm.
    
    Instead of uniform ±amount, use varied amplitudes per scallop.
    """
    # Varied pattern — irregular scallop sizes
    pattern = [0, 3.5, -2.0, 5.0, -1.0, 4.0, -3.5, 1.5, 2.5, -4.0, 3.0, -1.5]
    new_cmds = []
    idx = 0
    for cmd, vals in cmds:
        if cmd == 'C' and len(vals) >= 6:
            y_end = vals[5]
            if y_end > y_threshold:
                v = pattern[idx % len(pattern)] * (amount / 4.0)
                new_vals = vals[:]
                new_vals[1] += v * 0.25   # CP1 y
                new_vals[3] += v * 0.4    # CP2 y
                new_vals[5] += v          # endpoint y
                # Also shift x slightly for asymmetry
                x_shift = (pattern[(idx + 3) % len(pattern)]) * 0.8
                new_vals[0] += x_shift * 0.3  # CP1 x
                new_vals[2] += x_shift * 0.5  # CP2 x
                new_vals[4] += x_shift         # endpoint x
                new_cmds.append((cmd, new_vals))
                idx += 1
            else:
                new_cmds.append((cmd, vals[:]))
        else:
            new_cmds.append((cmd, vals[:]))
    return new_cmds


def vary_crown_peaks(cmds, amount=5.0, y_threshold=40):
    """Vary crown peak heights for irregularity."""
    pattern = [0, 2.0, -3.0, 4.0, -1.5, 2.5, -2.0, 1.0]
    new_cmds = []
    idx = 0
    for cmd, vals in cmds:
        if cmd == 'C' and len(vals) >= 6:
            y_end = vals[5]
            if y_end < y_threshold:
                v = pattern[idx % len(pattern)] * (amount / 3.0)
                new_vals = vals[:]
                new_vals[5] += v  # shift endpoint y
                new_cmds.append((cmd, new_vals))
                idx += 1
            else:
                new_cmds.append((cmd, vals[:]))
        else:
            new_cmds.append((cmd, vals[:]))
    return new_cmds


def process_file(filepath, ahoge_x, ahoge_h, scallop_y_thresh=200):
    """Process one SVG file."""
    content = filepath.read_text(encoding='utf-8')
    
    # Expand viewBox to accommodate ahoge going above y=0
    content = content.replace(
        'viewBox="0 0 1000 1000"',
        'viewBox="-60 -40 1120 1080"'
    )
    
    path_matches = list(re.finditer(r'(<path\s[^>]*?)d="([^"]*)"', content))
    
    new_content = content
    modified = False
    for idx, m in enumerate(path_matches):
        full = m.group(0)
        d_val = m.group(2)
        cmds = parse_d(d_val)
        
        if idx == 0:  # Outer contour
            # 1. Remove existing tiny ahoge
            cmds, removed = remove_existing_ahoge(cmds)
            
            # 2. Insert tall ahoge at the target x position
            cmds, inserted = insert_ahoge_at_valley(cmds, ahoge_x, ahoge_h)
            
            # 3. Vary crown peaks
            cmds = vary_crown_peaks(cmds, amount=4.0)
            
            # 4. Vary bottom scallops
            cmds = vary_bottom_scallops(cmds, amount=4.5, y_threshold=scallop_y_thresh)
            
            new_d = d_str(cmds)
            modified = True
        elif idx == 1:  # Inner boundary
            cmds = vary_bottom_scallops(cmds, amount=3.0, y_threshold=scallop_y_thresh - 10)
            new_d = d_str(cmds)
        else:
            continue
        
        new_full = full.replace(f'd="{d_val}"', f'd="{new_d}"', 1)
        new_content = new_content.replace(full, new_full, 1)
    
    if modified:
        filepath.write_text(new_content, encoding='utf-8')
    return modified


def main():
    print("=== HairFront Fix v4 ===")
    
    # Step 1: Regenerate from source
    print("Regenerating from source...")
    import subprocess
    subprocess.run(["python", "generate_art.py"], check=False)
    
    # Step 2: Apply fixes per view
    # View configs: (pattern, ahoge_x, ahoge_h, scallop_y_thresh)
    # ahoge_h = height in px; crown is at y≈20, peak must stay in viewBox (y>0)
    # 0.15-0.25 of head-height (~860px) = 130-215px BUT crown sits at y≈20
    # so actual spike from crown = 15-40px keeps it visible and prominent
    # Back3Q must come BEFORE 3Q to avoid false match
    configs = [
        ("Front",       re.compile(r"^HairFront_Front_"),  525, 35, 200),
        ("Profile",     re.compile(r"^HairFront_Profile_"), 640, 28, 260),
        ("Back3Q",      re.compile(r"^HairFront_Back3Q_"), 625, 30, 280),
        ("Back",        re.compile(r"^HairFront_Back_"),   520, 18, 500),
        ("Top",         re.compile(r"^HairFront_Top_"),    530, 22, 170),
        ("UnderPlane",  re.compile(r"^HairFront_UnderPlane_"), 525, 20, 300),
        ("3Q",          re.compile(r"^HairFront_3Q_"),     545, 32, 180),
    ]
    
    for svg in sorted(ART_DIR.glob("*.svg")):
        for view_name, pattern, ahoge_x, ahoge_h, scallop_y in configs:
            if pattern.match(svg.stem):
                ok = process_file(svg, ahoge_x, ahoge_h, scallop_y)
                if ok:
                    print(f"  {view_name}: {svg.name}")
                break
    
    print("\nDone.")


if __name__ == "__main__":
    main()
