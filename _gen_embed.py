"""Regenerate EMBEDDED_SOURCES in deploy.py from C++ source files.

Usage:
    python _gen_embed.py

Reads all .h/.cpp files listed below, base64-encodes them, and replaces
the EMBEDDED_SOURCES dictionary in deploy.py in-place.
"""

import base64, os, re, sys

SOURCES = [
    "DepthDebugVisualizerComponent.cpp",
    "DepthDebugVisualizerComponent.h",
    "FaceParallaxComponent.cpp",
    "FaceParallaxComponent.h",
    "FaceParallaxEditorWidget.cpp",
    "FaceParallaxEditorWidget.h",
    "FaceParallaxPreset.cpp",
    "FaceParallaxPreset.h",
    "FaceParallaxPreviewActor.cpp",
    "FaceParallaxPreviewActor.h",
    "FaceParallaxTypes.h",
]

DEPLOY_PY = "deploy.py"

def make_embedded_block():
    this_dir = os.path.dirname(os.path.abspath(__file__))
    lines = []
    for name in SOURCES:
        path = os.path.join(this_dir, name)
        if not os.path.isfile(path):
            print(f"[WARN] {name} not found — skipping")
            continue
        with open(path, "rb") as f:
            data = base64.b64encode(f.read()).decode("ascii")
        lines.append(f'    "{name}": base64.b64decode({repr(data)}),')
    return lines

def patch_deploy_py():
    dl = os.path.join(os.path.dirname(os.path.abspath(__file__)), DEPLOY_PY)
    with open(dl, "r", encoding="utf-8") as f:
        content = f.read()

    # Find the EMBEDDED_SOURCES = { ... } block
    start_marker = "EMBEDDED_SOURCES = {"
    start_idx = content.find(start_marker)
    if start_idx == -1:
        print("[ERROR] Could not find EMBEDDED_SOURCES in deploy.py")
        sys.exit(1)

    # Find end of the dict: matching '}' at correct brace depth
    brace_depth = 0
    end_idx = start_idx + len(start_marker)
    # Seek to first '{' after marker
    brace_start = content.find("{", start_idx)
    if brace_start == -1:
        print("[ERROR] Malformed EMBEDDED_SOURCES")
        sys.exit(1)
    for i in range(brace_start, len(content)):
        c = content[i]
        if c == '{':
            brace_depth += 1
        elif c == '}':
            brace_depth -= 1
            if brace_depth == 0:
                end_idx = i + 1
                break
    if brace_depth != 0:
        print("[ERROR] Unmatched braces in EMBEDDED_SOURCES")
        sys.exit(1)

    new_block_lines = make_embedded_block()
    new_block = "EMBEDDED_SOURCES = {\n" + "\n".join(new_block_lines) + "\n}\n"

    new_content = content[:start_idx] + new_block + content[end_idx:]

    with open(dl, "w", encoding="utf-8") as f:
        f.write(new_content)

    print(f"[OK] Patched EMBEDDED_SOURCES in {DEPLOY_PY} ({len(new_block_lines)} files)")

if __name__ == "__main__":
    patch_deploy_py()
