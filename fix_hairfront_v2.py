#!/usr/bin/env python3
"""Surgically fix HairFront SVGs by modifying specific coordinate values in-place.

Strategy: 
1. Parse the SVG text
2. For the outer contour (path 0, stroke-width 5.0): insert ahoge spike and vary bottom scallops
3. For the inner boundary (path 1, stroke-width 3.0): vary bottom scallops to match
4. Write back without breaking the path structure
"""
import re
from pathlib import Path

ART_DIR = Path(__file__).parent / "Art" / "HairFront"


def extract_paths(content):
    """Extract path elements with their attributes."""
    # Find all <path ... /> elements
    pattern = re.compile(r'(<path\s+)(.*?)(/>)', re.DOTALL)
    return list(pattern.finditer(content, content))


def extract_d_attr(path_text):
    """Extract the d attribute value from a path element."""
    m = re.search(r'd="([^"]*)"', path_text)
    if m:
        return m.group(1), m.start(1), m.end(1)
    return None, None, None


def parse_coords(d_str):
    """Parse SVG path d into a list of coordinate pairs and commands.
    Returns list of (cmd, [float_values]).
    """
    # Tokenize: commands are single letters, numbers are floats
    tokens = re.findall(r'[MmZzLlHhVvCcSsQqTtAa]|[-+]?(?:\d+\.?\d*|\.\d+)', d_str)
    
    result = []
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
            result.append((cmd, vals))
        else:
            i += 1
    return result


def cmds_to_d(cmds):
    """Convert parsed commands back to d string, preserving float precision."""
    parts = []
    for cmd, vals in cmds:
        if cmd in ('Z', 'z'):
            parts.append(cmd)
        else:
            nums = ' '.join(f'{v:.3f}' for v in vals)
            parts.append(f'{cmd} {nums}')
    return ' '.join(parts)


def add_ahoge_front(d_str):
    """Front view: insert ahoge spike at crown, off-center-right (~x=520).
    
    The ahoge is a sharp upward spike breaking centerline symmetry.
    We insert it by adding two extra C segments that form a V-shape
    going upward from the crown area.
    
    Strategy: find the C segment whose control points are in the crown
    region (y < 40), and insert the ahoge BEFORE that segment.
    """
    cmds = parse_coords(d_str)
    new_cmds = []
    ahoge_inserted = False
    
    for i, (cmd, vals) in enumerate(cmds):
        if not ahoge_inserted and cmd == 'C':
            # Check if this C's endpoint is in the crown region (y < 50)
            if len(vals) >= 6:
                y_end = vals[5]
                y_cp2 = vals[3]
                if y_end < 50 or y_cp2 < 30:
                    # Insert ahoge: two C segments forming a sharp V spike
                    # From current position, spike up to y=-10, then back down
                    new_cmds.append(('C', [
                        520.000, 22.000,   # CP1: start of spike
                        517.000, -5.000,   # CP2: near peak
                        517.000, -10.000   # endpoint: spike tip
                    ]))
                    new_cmds.append(('C', [
                        517.000, -15.000,  # CP1: past peak (sharp corner)
                        521.000, -5.000,   # CP2: descending
                        524.000, 20.000    # endpoint: back at crown level
                    ]))
                    ahoge_inserted = True
        new_cmds.append((cmd, vals[:]))
    
    if not ahoge_inserted:
        # Fallback: insert after the M command
        for i, (cmd, vals) in enumerate(cmds):
            if cmd == 'M':
                new_cmds.append((cmd, vals[:]))
                new_cmds.append(('C', [
                    520.000, 22.000, 517.000, -5.000, 517.000, -10.000
                ]))
                new_cmds.append(('C', [
                    517.000, -15.000, 521.000, -5.000, 524.000, 20.000
                ]))
                new_cmds.extend(cmds[i+1:])
                break
        else:
            return d_str  # Can't insert
    
    return cmds_to_d(new_cmds)


def add_ahoge_3q(d_str):
    """3Q view: ahoge on near side, spikes upward."""
    cmds = parse_coords(d_str)
    new_cmds = []
    ahoge_inserted = False
    
    for i, (cmd, vals) in enumerate(cmds):
        if not ahoge_inserted and cmd == 'C' and len(vals) >= 6:
            y_end = vals[5]
            if y_end < 50:
                new_cmds.append(('C', [
                    532.000, 18.000, 528.000, -2.000, 528.000, -6.000
                ]))
                new_cmds.append(('C', [
                    528.000, -10.000, 534.000, -1.000, 537.000, 16.000
                ]))
                ahoge_inserted = True
        new_cmds.append((cmd, vals[:]))
    
    return cmds_to_d(new_cmds) if ahoge_inserted else d_str


def add_ahoge_profile(d_str):
    """Profile: ahoge at top of crown."""
    cmds = parse_coords(d_str)
    new_cmds = []
    ahoge_inserted = False
    
    for i, (cmd, vals) in enumerate(cmds):
        if not ahoge_inserted and cmd == 'C' and len(vals) >= 6:
            y_end = vals[5]
            if y_end < 65:
                new_cmds.append(('C', [
                    632.000, 52.000, 628.000, 38.000, 628.000, 36.000
                ]))
                new_cmds.append(('C', [
                    628.000, 34.000, 636.000, 40.000, 638.000, 56.000
                ]))
                ahoge_inserted = True
        new_cmds.append((cmd, vals[:]))
    
    return cmds_to_d(new_cmds) if ahoge_inserted else d_str


def add_ahoge_back3q(d_str):
    """Back3Q: ahoge on the turning side."""
    cmds = parse_coords(d_str)
    new_cmds = []
    ahoge_inserted = False
    
    for i, (cmd, vals) in enumerate(cmds):
        if not ahoge_inserted and cmd == 'C' and len(vals) >= 6:
            y_end = vals[5]
            if y_end < 70:
                new_cmds.append(('C', [
                    618.000, 52.000, 614.000, 40.000, 614.000, 38.000
                ]))
                new_cmds.append(('C', [
                    614.000, 36.000, 622.000, 42.000, 624.000, 56.000
                ]))
                ahoge_inserted = True
        new_cmds.append((cmd, vals[:]))
    
    return cmds_to_d(new_cmds) if ahoge_inserted else d_str


def add_ahoge_back(d_str):
    """Back: subtle bump."""
    cmds = parse_coords(d_str)
    new_cmds = []
    ahoge_inserted = False
    
    for i, (cmd, vals) in enumerate(cmds):
        if not ahoge_inserted and cmd == 'C' and len(vals) >= 6:
            y_end = vals[5]
            if y_end < 45:
                new_cmds.append(('C', [
                    520.000, 22.000, 518.000, 14.000, 518.000, 12.000
                ]))
                new_cmds.append(('C', [
                    518.000, 10.000, 522.000, 14.000, 524.000, 24.000
                ]))
                ahoge_inserted = True
        new_cmds.append((cmd, vals[:]))
    
    return cmds_to_d(new_cmds) if ahoge_inserted else d_str


def add_ahoge_top(d_str):
    """Top: ahoge visible from above."""
    cmds = parse_coords(d_str)
    new_cmds = []
    ahoge_inserted = False
    
    for i, (cmd, vals) in enumerate(cmds):
        if not ahoge_inserted and cmd == 'C' and len(vals) >= 6:
            y_end = vals[5]
            if y_end < 35:
                new_cmds.append(('C', [
                    530.000, 26.000, 527.000, 16.000, 527.000, 14.000
                ]))
                new_cmds.append(('C', [
                    527.000, 12.000, 533.000, 18.000, 535.000, 28.000
                ]))
                ahoge_inserted = True
        new_cmds.append((cmd, vals[:]))
    
    return cmds_to_d(new_cmds) if ahoge_inserted else d_str


def add_ahoge_underplane(d_str):
    """UnderPlane: ahoge barely visible."""
    cmds = parse_coords(d_str)
    new_cmds = []
    ahoge_inserted = False
    
    for i, (cmd, vals) in enumerate(cmds):
        if not ahoge_inserted and cmd == 'C' and len(vals) >= 6:
            y_end = vals[5]
            if y_end < 55:
                new_cmds.append(('C', [
                    522.000, 38.000, 520.000, 28.000, 520.000, 26.000
                ]))
                new_cmds.append(('C', [
                    520.000, 24.000, 524.000, 30.000, 526.000, 40.000
                ]))
                ahoge_inserted = True
        new_cmds.append((cmd, vals[:]))
    
    return cmds_to_d(new_cmds) if ahoge_inserted else d_str


def vary_bottom_scallops(d_str, amount=2.5):
    """Vary the bottom hem scallops by small amounts for visual rhythm.
    
    Finds C/L segments where the endpoint y > threshold and shifts them
    by a pseudo-random pattern.
    """
    cmds = parse_coords(d_str)
    pattern = [0, 2.0, -1.2, 3.0, -0.8, 1.5, -2.0, 0.6, 1.0, -1.5, 2.5, -0.6, 1.2, -2.2, 0.4, 1.8, -1.0]
    
    new_cmds = []
    idx = 0
    for cmd, vals in cmds:
        if cmd in ('C', 'c') and len(vals) >= 6:
            y_end = vals[5]
            if y_end > 220:  # Bottom hem region
                v = pattern[idx % len(pattern)] * (amount / 2.0)
                new_vals = vals[:]
                new_vals[1] += v * 0.3   # CP1 y
                new_vals[3] += v * 0.5   # CP2 y
                new_vals[5] += v          # endpoint y
                new_cmds.append((cmd, new_vals))
                idx += 1
            else:
                new_cmds.append((cmd, vals[:]))
        elif cmd in ('L', 'l') and len(vals) >= 2:
            y = vals[1]
            if y > 220:
                v = pattern[idx % len(pattern)] * (amount / 2.0)
                new_cmds.append((cmd, [vals[0], vals[1] + v]))
                idx += 1
            else:
                new_cmds.append((cmd, vals[:]))
        else:
            new_cmds.append((cmd, vals[:]))
    
    return cmds_to_d(new_cmds)


def process_svg(filepath, view_type):
    """Process a single HairFront SVG file."""
    content = filepath.read_text(encoding='utf-8')
    
    # Find path elements
    path_matches = list(re.finditer(r'(<path\s[^>]*?)d="([^"]*)"', content))
    
    if len(path_matches) < 2:
        print(f"  WARNING: found {len(path_matches)} paths in {filepath.name}")
        return
    
    new_content = content
    
    for idx, m in enumerate(path_matches):
        full_match = m.group(0)
        d_value = m.group(2)
        
        if idx == 0:
            # Outer contour: add ahoge + vary bottom scallops
            if view_type == "front":
                new_d = add_ahoge_front(d_value)
            elif view_type == "3q":
                new_d = add_ahoge_3q(d_value)
            elif view_type == "profile":
                new_d = add_ahoge_profile(d_value)
            elif view_type == "back3q":
                new_d = add_ahoge_back3q(d_value)
            elif view_type == "back":
                new_d = add_ahoge_back(d_value)
            elif view_type == "top":
                new_d = add_ahoge_top(d_value)
            elif view_type == "underplane":
                new_d = add_ahoge_underplane(d_value)
            else:
                new_d = d_value
            
            new_d = vary_bottom_scallops(new_d, amount=3.0)
            new_full = full_match.replace(f'd="{d_value}"', f'd="{new_d}"')
            new_content = new_content.replace(full_match, new_full, 1)
        
        elif idx == 1:
            # Inner boundary: vary bottom scallops
            new_d = vary_bottom_scallops(d_value, amount=2.0)
            new_full = full_match.replace(f'd="{d_value}"', f'd="{new_d}"')
            new_content = new_content.replace(full_match, new_full, 1)
    
    filepath.write_text(new_content, encoding='utf-8')
    print(f"  Fixed: {filepath.name} (view={view_type})")


def main():
    print("=== HairFront (Bangs) Surgical Remediation ===")
    
    view_map = {
        "HairFront_Front_Y00_P00": "front",
        "HairFront_Front_Y00_P45": "front",
        "HairFront_Front_Y00_Pn45": "front",
        "HairFront_3Q_Y45_P00": "3q",
        "HairFront_3Q_Y45_P45": "3q",
        "HairFront_3Q_Y45_Pn45": "3q",
        "HairFront_Profile_Y90_P00": "profile",
        "HairFront_Profile_Y90_P45": "profile",
        "HairFront_Profile_Y90_Pn45": "profile",
        "HairFront_Back3Q_Y135_P00": "back3q",
        "HairFront_Back3Q_Y135_P45": "back3q",
        "HairFront_Back3Q_Y135_Pn45": "back3q",
        "HairFront_Back_Y180_P00": "back",
        "HairFront_Back_Y180_P45": "back",
        "HairFront_Back_Y180_Pn45": "back",
        "HairFront_Top_Y00_P90": "top",
        "HairFront_UnderPlane_Y00_Pn45": "underplane",
    }
    
    svg_files = sorted(ART_DIR.glob("*.svg"))
    print(f"Found {len(svg_files)} SVGs")
    
    for svg in svg_files:
        stem = svg.stem
        vt = view_map.get(stem)
        if vt is None:
            print(f"  SKIP: {stem}")
            continue
        process_svg(svg, vt)
    
    print("\nDone. Re-render to evaluate.")


if __name__ == "__main__":
    main()
