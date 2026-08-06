#!/usr/bin/env python3
"""Surgically fix HairFront SVGs - v3 (coordinate-level edits only).

Approach: parse the d attribute, modify specific coordinate values, 
rebuild the d string. No path restructuring.
"""
import re
from pathlib import Path

ART_DIR = Path(__file__).parent / "Art" / "HairFront"


def parse_d(d_str):
    """Parse SVG path d into list of (cmd, [floats])."""
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
    """Rebuild d string from parsed commands."""
    parts = []
    for cmd, vals in cmds:
        if cmd in ('Z', 'z'):
            parts.append(cmd)
        else:
            parts.append(cmd + ' ' + ' '.join(f'{v:.3f}' for v in vals))
    return ' '.join(parts)


def add_ahoge_by_index(cmds, idx_before, spike_height=20):
    """Insert ahoge spike (2 C commands) before the given index.
    
    The ahoge is a sharp upward V-spike. We insert it by adding two 
    cubic bezier segments: one going up to the peak, one coming back down.
    The current point at idx_before-1's endpoint becomes the starting point.
    """
    # Get current position (endpoint of the command before insertion)
    prev_cmd, prev_vals = cmds[idx_before - 1]
    if prev_cmd == 'C' and len(prev_vals) >= 6:
        start_x, start_y = prev_vals[4], prev_vals[5]
    elif prev_cmd == 'M' and len(prev_vals) >= 2:
        start_x, start_y = prev_vals[0], prev_vals[1]
    else:
        return cmds  # Can't determine start point
    
    # Ahoge: sharp spike going up from current position
    # Peak at start_x - 2, start_y - spike_height
    peak_x = start_x - 2
    peak_y = start_y - spike_height
    
    ahoge_cmds = [
        ('C', [
            start_x + 2, start_y - spike_height * 0.3,  # CP1: slight right, partially up
            peak_x - 1, peak_y + spike_height * 0.15,    # CP2: left of peak
            peak_x, peak_y                                 # endpoint: spike tip
        ]),
        ('C', [
            peak_x + 1, peak_y - spike_height * 0.1,     # CP1: past tip (sharp corner)
            start_x + 5, start_y - spike_height * 0.25,   # CP2: descending right
            start_x + 4, start_y - 2                       # endpoint: back near crown
        ])
    ]
    
    new_cmds = cmds[:idx_before] + ahoge_cmds + cmds[idx_before:]
    return new_cmds


def vary_bottom_scallops(cmds, amount=2.5, y_threshold=240):
    """Vary bottom hem coordinates (indices 19-25 typically)."""
    pattern = [0, 2.0, -1.5, 3.0, -0.8, 1.5, -2.0, 0.6, 1.0, -1.5, 2.5]
    new_cmds = []
    idx = 0
    for cmd, vals in cmds:
        if cmd == 'C' and len(vals) >= 6:
            y_end = vals[5]
            if y_end > y_threshold:
                v = pattern[idx % len(pattern)] * (amount / 2.5)
                new_vals = vals[:]
                new_vals[1] += v * 0.3   # CP1 y
                new_vals[3] += v * 0.5   # CP2 y
                new_vals[5] += v          # endpoint y
                new_cmds.append((cmd, new_vals))
                idx += 1
            else:
                new_cmds.append((cmd, vals[:]))
        else:
            new_cmds.append((cmd, vals[:]))
    return new_cmds


def find_crown_insertion_point(cmds, x_center):
    """Find the best index to insert the ahoge near x_center in the crown region.
    
    Look for the C command whose endpoint x is closest to x_center and y < 30.
    Insert BEFORE that command.
    """
    best_idx = None
    best_dist = 999999
    for i, (cmd, vals) in enumerate(cmds):
        if cmd == 'C' and len(vals) >= 6:
            x, y = vals[4], vals[5]
            if y < 35:  # Crown region
                dist = abs(x - x_center)
                if dist < best_dist:
                    best_dist = dist
                    best_idx = i
    return best_idx


def process_front(filepath):
    content = filepath.read_text(encoding='utf-8')
    path_matches = list(re.finditer(r'(<path\s[^>]*?)d="([^"]*)"', content))
    
    new_content = content
    for idx, m in enumerate(path_matches):
        full = m.group(0)
        d_val = m.group(2)
        cmds = parse_d(d_val)
        
        if idx == 0:  # Outer contour
            # Add ahoge near x=510 (slightly right of center)
            ins = find_crown_insertion_point(cmds, 510)
            if ins is not None:
                cmds = add_ahoge_by_index(cmds, ins, spike_height=22)
            # Vary bottom scallops
            cmds = vary_bottom_scallops(cmds, amount=3.0, y_threshold=240)
            new_d = d_str(cmds)
        elif idx == 1:  # Inner boundary
            cmds = vary_bottom_scallops(cmds, amount=2.0, y_threshold=230)
            new_d = d_str(cmds)
        else:
            continue
        
        new_full = full.replace(f'd="{d_val}"', f'd="{new_d}"', 1)
        new_content = new_content.replace(full, new_full, 1)
    
    filepath.write_text(new_content, encoding='utf-8')
    print(f"  Front: {filepath.name}")


def process_3q(filepath):
    content = filepath.read_text(encoding='utf-8')
    path_matches = list(re.finditer(r'(<path\s[^>]*?)d="([^"]*)"', content))
    
    new_content = content
    for idx, m in enumerate(path_matches):
        full = m.group(0)
        d_val = m.group(2)
        cmds = parse_d(d_val)
        
        if idx == 0:
            # Ahoge near x=535 (near side of crown in 3Q)
            ins = find_crown_insertion_point(cmds, 540)
            if ins is not None:
                cmds = add_ahoge_by_index(cmds, ins, spike_height=18)
            cmds = vary_bottom_scallops(cmds, amount=3.0, y_threshold=220)
            new_d = d_str(cmds)
        elif idx == 1:
            cmds = vary_bottom_scallops(cmds, amount=2.0, y_threshold=210)
            new_d = d_str(cmds)
        else:
            continue
        
        new_full = full.replace(f'd="{d_val}"', f'd="{new_d}"', 1)
        new_content = new_content.replace(full, new_full, 1)
    
    filepath.write_text(new_content, encoding='utf-8')
    print(f"  3Q: {filepath.name}")


def process_profile(filepath):
    content = filepath.read_text(encoding='utf-8')
    path_matches = list(re.finditer(r'(<path\s[^>]*?)d="([^"]*)"', content))
    
    new_content = content
    for idx, m in enumerate(path_matches):
        full = m.group(0)
        d_val = m.group(2)
        cmds = parse_d(d_val)
        
        if idx == 0:
            # Ahoge near x=635 (top of profile crown)
            ins = find_crown_insertion_point(cmds, 640)
            if ins is not None:
                cmds = add_ahoge_by_index(cmds, ins, spike_height=15)
            cmds = vary_bottom_scallops(cmds, amount=3.0, y_threshold=280)
            new_d = d_str(cmds)
        elif idx == 1:
            cmds = vary_bottom_scallops(cmds, amount=2.0, y_threshold=270)
            new_d = d_str(cmds)
        else:
            continue
        
        new_full = full.replace(f'd="{d_val}"', f'd="{new_d}"', 1)
        new_content = new_content.replace(full, new_full, 1)
    
    filepath.write_text(new_content, encoding='utf-8')
    print(f"  Profile: {filepath.name}")


def process_back3q(filepath):
    content = filepath.read_text(encoding='utf-8')
    path_matches = list(re.finditer(r'(<path\s[^>]*?)d="([^"]*)"', content))
    
    new_content = content
    for idx, m in enumerate(path_matches):
        full = m.group(0)
        d_val = m.group(2)
        cmds = parse_d(d_val)
        
        if idx == 0:
            ins = find_crown_insertion_point(cmds, 620)
            if ins is not None:
                cmds = add_ahoge_by_index(cmds, ins, spike_height=16)
            cmds = vary_bottom_scallops(cmds, amount=3.0, y_threshold=300)
            new_d = d_str(cmds)
        elif idx == 1:
            cmds = vary_bottom_scallops(cmds, amount=2.0, y_threshold=290)
            new_d = d_str(cmds)
        else:
            continue
        
        new_full = full.replace(f'd="{d_val}"', f'd="{new_d}"', 1)
        new_content = new_content.replace(full, new_full, 1)
    
    filepath.write_text(new_content, encoding='utf-8')
    print(f"  Back3Q: {filepath.name}")


def process_back(filepath):
    content = filepath.read_text(encoding='utf-8')
    path_matches = list(re.finditer(r'(<path\s[^>]*?)d="([^"]*)"', content))
    
    new_content = content
    for idx, m in enumerate(path_matches):
        full = m.group(0)
        d_val = m.group(2)
        cmds = parse_d(d_val)
        
        if idx == 0:
            # Subtle ahoge bump visible from behind
            ins = find_crown_insertion_point(cmds, 510)
            if ins is not None:
                cmds = add_ahoge_by_index(cmds, ins, spike_height=10)
            cmds = vary_bottom_scallops(cmds, amount=3.0, y_threshold=600)
            new_d = d_str(cmds)
        elif idx == 1:
            cmds = vary_bottom_scallops(cmds, amount=2.0, y_threshold=250)
            new_d = d_str(cmds)
        else:
            continue
        
        new_full = full.replace(f'd="{d_val}"', f'd="{new_d}"', 1)
        new_content = new_content.replace(full, new_full, 1)
    
    filepath.write_text(new_content, encoding='utf-8')
    print(f"  Back: {filepath.name}")


def process_top(filepath):
    content = filepath.read_text(encoding='utf-8')
    path_matches = list(re.finditer(r'(<path\s[^>]*?)d="([^"]*)"', content))
    
    new_content = content
    for idx, m in enumerate(path_matches):
        full = m.group(0)
        d_val = m.group(2)
        cmds = parse_d(d_val)
        
        if idx == 0:
            ins = find_crown_insertion_point(cmds, 525)
            if ins is not None:
                cmds = add_ahoge_by_index(cmds, ins, spike_height=14)
            cmds = vary_bottom_scallops(cmds, amount=3.0, y_threshold=200)
            new_d = d_str(cmds)
        elif idx == 1:
            cmds = vary_bottom_scallops(cmds, amount=2.0, y_threshold=190)
            new_d = d_str(cmds)
        else:
            continue
        
        new_full = full.replace(f'd="{d_val}"', f'd="{new_d}"', 1)
        new_content = new_content.replace(full, new_full, 1)
    
    filepath.write_text(new_content, encoding='utf-8')
    print(f"  Top: {filepath.name}")


def process_underplane(filepath):
    content = filepath.read_text(encoding='utf-8')
    path_matches = list(re.finditer(r'(<path\s[^>]*?)d="([^"]*)"', content))
    
    new_content = content
    for idx, m in enumerate(path_matches):
        full = m.group(0)
        d_val = m.group(2)
        cmds = parse_d(d_val)
        
        if idx == 0:
            ins = find_crown_insertion_point(cmds, 518)
            if ins is not None:
                cmds = add_ahoge_by_index(cmds, ins, spike_height=12)
            cmds = vary_bottom_scallops(cmds, amount=3.0, y_threshold=350)
            new_d = d_str(cmds)
        elif idx == 1:
            cmds = vary_bottom_scallops(cmds, amount=2.0, y_threshold=260)
            new_d = d_str(cmds)
        else:
            continue
        
        new_full = full.replace(f'd="{d_val}"', f'd="{new_d}"', 1)
        new_content = new_content.replace(full, new_full, 1)
    
    filepath.write_text(new_content, encoding='utf-8')
    print(f"  UnderPlane: {filepath.name}")


def main():
    print("=== HairFront Surgical Fix v3 ===")
    
    # Regenerate first from source
    print("Regenerating SVGs from source...")
    import subprocess
    subprocess.run(["python", "generate_art.py"], check=False)
    
    view_processors = {
        "front": process_front,
        "3q": process_3q,
        "profile": process_profile,
        "back3q": process_back3q,
        "back": process_back,
        "top": process_top,
        "underplane": process_underplane,
    }
    
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
    
    for svg in sorted(ART_DIR.glob("*.svg")):
        vt = view_map.get(svg.stem)
        if vt and vt in view_processors:
            view_processors[vt](svg)
    
    print("\nDone.")


if __name__ == "__main__":
    main()
