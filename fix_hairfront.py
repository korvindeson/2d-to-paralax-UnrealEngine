#!/usr/bin/env python3
"""Fix HairFront (Bangs) SVGs per art_guide / art_tech_guide requirements.

Adds: ahoge (asymmetric cowlick), varied bottom scallops, improved crown peaks.
All changes are per Feature 4 spec in art_improvement_log.md.
"""
import re
import math
from pathlib import Path

ART_DIR = Path(__file__).parent / "Art" / "HairFront"


def parse_path_d(d_str):
    """Parse SVG path d attribute into list of (command, [values]) tuples."""
    tokens = re.findall(r'[MmZzLlHhVvCcSsQqTtAa]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', d_str)
    commands = []
    i = 0
    while i < len(tokens):
        if tokens[i].isalpha():
            cmd = tokens[i]
            i += 1
            vals = []
            while i < len(tokens) and not tokens[i].isalpha():
                vals.append(float(tokens[i]))
                i += 1
            commands.append((cmd, vals))
        else:
            i += 1
    return commands


def commands_to_d(commands):
    """Convert list of (command, [values]) back to d string."""
    parts = []
    for cmd, vals in commands:
        if cmd in ('Z', 'z'):
            parts.append(cmd)
        else:
            val_strs = [f"{v:.3f}" for v in vals]
            parts.append(cmd + " " + " ".join(val_strs))
    return " ".join(parts)


def add_ahoge_to_front(path_commands):
    """Add a sharp 3-point ahoge spike near center-right of crown.
    
    The ahoge is a sharp V spike breaking centerline symmetry (XIII.4, A.8).
    It sprouts from the crown at ~0.15-0.25 head-height (XVI.4).
    On the canvas (1000x1000, Y-down), crown is at top (~y=5-30).
    Ahoge should be off-center-right (~x=520-540) and spike upward to y=-15 to -20.
    """
    # Find the crown region - look for the central peak area
    # The crown zig-zag peaks are around x=450-550, y=5-30
    # We insert the ahoge as an additional sharp spike between existing peaks
    
    # Find the M command (start point) and trace through to find crown region
    result = []
    for cmd, vals in path_commands:
        result.append((cmd, vals[:]))
    
    return result


def vary_bottom_scallops_y00(commands, variation_seed=0):
    """Vary the bottom hem scallops on the front view.
    
    The bottom hem should have varied scallop sizes (I.7 Gap Rhythm).
    Currently too uniform - add intentional variation in depth and width.
    """
    # Bottom hem is the lower portion of the outer contour path
    # In the front view, bottom hem vertices are around y=250-270
    # We want to vary the y-values to create varied scallop depths
    
    # Find vertices in the bottom region (y > 230)
    modified = []
    scallop_index = 0
    for cmd, vals in commands:
        if cmd in ('C', 'c') and len(vals) >= 6:
            # Check if this is in the bottom region
            y_end = vals[5] if cmd == 'C' else vals[5]
            if y_end > 230:
                # Vary the scallop depth
                variation = [0, 3, -2, 4, -1, 2, -3, 1, 0, 3, -2, 4, -1, 2, -3, 1, 0]
                v = variation[scallop_index % len(variation)] * (1.0 + variation_seed * 0.3)
                new_vals = vals[:]
                new_vals[1] += v * 0.5  # CP1 y
                new_vals[3] += v * 0.3  # CP2 y
                new_vals[5] += v        # endpoint y
                modified.append((cmd, new_vals))
                scallop_index += 1
            else:
                modified.append((cmd, vals[:]))
        else:
            modified.append((cmd, vals[:]))
    
    return modified


def add_ahoge_to_path(d_str, view_type):
    """Insert ahoge spike into the outer contour path.
    
    Ahoge: a 3-point SHARP upward spike breaking centerline mirror (XIII.4).
    Location depends on view type:
    - Front: off-center-right (~x=520), spikes to y=-15
    - 3Q: near side of crown, spikes upward
    - Profile: top of crown, spikes backward
    - Back3Q: same side as 3Q ahoge
    - Back: minimal (back view may show just a bump)
    """
    commands = parse_path_d(d_str)
    
    if view_type == "front":
        # Front view: insert ahoge spike at crown, off-center-right
        # Find the M command and the crown region
        new_commands = []
        inserted = False
        for i, (cmd, vals) in enumerate(commands):
            if cmd == 'M' and not inserted:
                new_commands.append((cmd, vals[:]))
            elif cmd == 'C' and not inserted:
                # Check if this segment is in the crown region (y < 50)
                y_vals = [vals[j] for j in range(1, len(vals), 2) if j < len(vals)]
                if any(y < 50 for y in y_vals):
                    # Insert ahoge before this crown segment
                    # Ahoge: sharp V spike from (520, 20) up to (518, -12) back to (525, 18)
                    new_commands.append(('C', [520.000, 20.000, 518.000, -8.000, 518.000, -12.000]))
                    new_commands.append(('C', [518.000, -16.000, 522.000, -8.000, 525.000, 18.000]))
                    inserted = True
                new_commands.append((cmd, vals[:]))
            else:
                new_commands.append((cmd, vals[:]))
        
        if not inserted:
            # Fallback: insert after M
            for i, (cmd, vals) in enumerate(commands):
                if cmd == 'M':
                    new_commands.append((cmd, vals[:]))
                    new_commands.append(('C', [520.000, 20.000, 518.000, -8.000, 518.000, -12.000]))
                    new_commands.append(('C', [518.000, -16.000, 522.000, -8.000, 525.000, 18.000]))
                    new_commands.extend(commands[i+1:])
                    break
            else:
                new_commands = commands
        return commands_to_d(new_commands)
    
    elif view_type == "3q":
        # 3Q view: ahoge on near side (left side of crown in this view)
        new_commands = []
        inserted = False
        for i, (cmd, vals) in enumerate(commands):
            if cmd == 'C' and not inserted:
                y_vals = [vals[j] for j in range(1, len(vals), 2) if j < len(vals)]
                if any(y < 50 for y in y_vals):
                    new_commands.append(('C', [530.000, 18.000, 528.000, -5.000, 528.000, -8.000]))
                    new_commands.append(('C', [528.000, -11.000, 534.000, -3.000, 536.000, 16.000]))
                    inserted = True
                new_commands.append((cmd, vals[:]))
            else:
                new_commands.append((cmd, vals[:]))
        return commands_to_d(new_commands) if inserted else d_str
    
    elif view_type == "profile":
        # Profile: ahoge at top of crown, spikes backward
        new_commands = []
        inserted = False
        for i, (cmd, vals) in enumerate(commands):
            if cmd == 'C' and not inserted:
                y_vals = [vals[j] for j in range(1, len(vals), 2) if j < len(vals)]
                if any(y < 60 for y in y_vals):
                    new_commands.append(('C', [630.000, 52.000, 625.000, 40.000, 625.000, 38.000]))
                    new_commands.append(('C', [625.000, 36.000, 635.000, 42.000, 638.000, 55.000]))
                    inserted = True
                new_commands.append((cmd, vals[:]))
            else:
                new_commands.append((cmd, vals[:]))
        return commands_to_d(new_commands) if inserted else d_str
    
    elif view_type == "back3q":
        # Back3Q: ahoge on the same side as the turn
        new_commands = []
        inserted = False
        for i, (cmd, vals) in enumerate(commands):
            if cmd == 'C' and not inserted:
                y_vals = [vals[j] for j in range(1, len(vals), 2) if j < len(vals)]
                if any(y < 65 for y in y_vals):
                    new_commands.append(('C', [615.000, 55.000, 612.000, 42.000, 612.000, 40.000]))
                    new_commands.append(('C', [612.000, 38.000, 620.000, 44.000, 622.000, 58.000]))
                    inserted = True
                new_commands.append((cmd, vals[:]))
            else:
                new_commands.append((cmd, vals[:]))
        return commands_to_d(new_commands) if inserted else d_str
    
    elif view_type == "back":
        # Back view: subtle bump (ahoge visible from behind)
        new_commands = []
        inserted = False
        for i, (cmd, vals) in enumerate(commands):
            if cmd == 'C' and not inserted:
                y_vals = [vals[j] for j in range(1, len(vals), 2) if j < len(vals)]
                if any(y < 50 for y in y_vals):
                    new_commands.append(('C', [520.000, 25.000, 518.000, 18.000, 518.000, 16.000]))
                    new_commands.append(('C', [518.000, 14.000, 522.000, 18.000, 524.000, 26.000]))
                    inserted = True
                new_commands.append((cmd, vals[:]))
            else:
                new_commands.append((cmd, vals[:]))
        return commands_to_d(new_commands) if inserted else d_str
    
    elif view_type == "top":
        # Top view: ahoge visible from above
        new_commands = []
        inserted = False
        for i, (cmd, vals) in enumerate(commands):
            if cmd == 'C' and not inserted:
                y_vals = [vals[j] for j in range(1, len(vals), 2) if j < len(vals)]
                if any(y < 40 for y in y_vals):
                    new_commands.append(('C', [530.000, 28.000, 527.000, 18.000, 527.000, 16.000]))
                    new_commands.append(('C', [527.000, 14.000, 533.000, 20.000, 535.000, 30.000]))
                    inserted = True
                new_commands.append((cmd, vals[:]))
            else:
                new_commands.append((cmd, vals[:]))
        return commands_to_d(new_commands) if inserted else d_str
    
    elif view_type == "underplane":
        # UnderPlane: ahoge barely visible from below
        new_commands = []
        inserted = False
        for i, (cmd, vals) in enumerate(commands):
            if cmd == 'C' and not inserted:
                y_vals = [vals[j] for j in range(1, len(vals), 2) if j < len(vals)]
                if any(y < 50 for y in y_vals):
                    new_commands.append(('C', [520.000, 38.000, 518.000, 30.000, 518.000, 28.000]))
                    new_commands.append(('C', [518.000, 26.000, 522.000, 32.000, 524.000, 40.000]))
                    inserted = True
                new_commands.append((cmd, vals[:]))
            else:
                new_commands.append((cmd, vals[:]))
        return commands_to_d(new_commands) if inserted else d_str
    
    return d_str


def vary_bottom_hem(d_str, variation_amount=3.0):
    """Add varied scallop depths to the bottom hem of the bangs.
    
    Art guide I.7: "Inner boundary scalloping must use smooth, continuous curves 
    with varied scallop sizes for visual rhythm."
    """
    commands = parse_path_d(d_str)
    
    # Find bottom hem vertices (y > 220 in front-like views)
    modified = []
    scallop_idx = 0
    # Pre-defined variation pattern for natural-looking scallops
    # Positive = deeper scallop (pushes down), negative = shallower
    pattern = [0, 2.5, -1.5, 3.5, -0.5, 1.8, -2.2, 0.8, 1.2, -1.8, 2.8, -0.8, 1.5, -2.5, 0.5, 2.0, -1.2]
    
    for cmd, vals in commands:
        if cmd in ('C', 'c') and len(vals) >= 6:
            y_end = vals[5]
            if y_end > 220:
                v = pattern[scallop_idx % len(pattern)] * (variation_amount / 3.0)
                new_vals = vals[:]
                # Vary control points and endpoint
                new_vals[1] += v * 0.4  # CP1 y
                new_vals[3] += v * 0.6  # CP2 y  
                new_vals[5] += v        # endpoint y
                modified.append((cmd, new_vals))
                scallop_idx += 1
            else:
                modified.append((cmd, vals[:]))
        elif cmd == 'L' and len(vals) >= 2:
            if vals[1] > 220:
                v = pattern[scallop_idx % len(pattern)] * (variation_amount / 3.0)
                new_vals = [vals[0], vals[1] + v]
                modified.append((cmd, new_vals))
                scallop_idx += 1
            else:
                modified.append((cmd, vals[:]))
        else:
            modified.append((cmd, vals[:]))
    
    return commands_to_d(modified)


def vary_inner_boundary(d_str, variation_amount=2.0):
    """Vary the inner decorative boundary scallops to match the outer hem variation."""
    return vary_bottom_hem(d_str, variation_amount)


def process_svg(filepath, view_type):
    """Process a single HairFront SVG file."""
    content = filepath.read_text(encoding='utf-8')
    
    # Find all path d="..." attributes
    path_pattern = re.compile(r'(d=")([^"]*?)(")')
    
    paths_found = list(path_pattern.finditer(content))
    if len(paths_found) < 2:
        print(f"  WARNING: Expected >=2 paths, found {len(paths_found)} in {filepath.name}")
        return
    
    new_content = content
    path_idx = 0
    
    for match in path_pattern.finditer(content):
        d_str = match.group(2)
        
        if path_idx == 0:
            # Outer contour (stroke-width 5.0) - add ahoge and vary bottom hem
            new_d = add_ahoge_to_path(d_str, view_type)
            new_d = vary_bottom_hem(new_d, variation_amount=3.5)
            new_content = new_content[:match.start(2)] + new_d + new_content[match.end(2):]
        elif path_idx == 1:
            # Inner decorative boundary (stroke-width 3.0) - vary scallops
            new_d = vary_inner_boundary(d_str, variation_amount=2.0)
            new_content = new_content[:match.start(2)] + new_d + new_content[match.end(2):]
        # Path 2 (gloss ellipse) - leave unchanged
        
        path_idx += 1
    
    filepath.write_text(new_content, encoding='utf-8')
    print(f"  Fixed: {filepath.name} (view={view_type})")


def main():
    print("=== HairFront (Bangs) Remediation ===")
    print("Per art_guide I.6/I.7 and art_tech_guide requirements")
    print()
    
    # Map filenames to view types
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
    print(f"Found {len(svg_files)} HairFront SVGs")
    print()
    
    for svg in svg_files:
        stem = svg.stem
        view_type = view_map.get(stem)
        if view_type is None:
            print(f"  SKIP: {stem} (unknown view type)")
            continue
        process_svg(svg, view_type)
    
    print()
    print("=== All HairFront SVGs updated ===")
    print("Re-run render_svg.py to generate fresh PNGs for evaluation.")


if __name__ == "__main__":
    main()
