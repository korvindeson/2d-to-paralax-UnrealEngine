"""Render an SVG file to PNG using headless Chromium.
Usage: python render_svg.py <path_to_svg>
Output: <same_name>.png in the same directory as the SVG.
"""
import sys
from pathlib import Path
from playwright.sync_api import sync_playwright


def render_svg_to_png(svg_path: str) -> str:
    src = Path(svg_path).resolve()
    if not src.exists():
        raise FileNotFoundError(f"SVG not found: {src}")
    dst = src.with_suffix(".png")

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1000, "height": 1000})
        page.goto(f"file:///{src.as_posix()}")
        page.wait_for_timeout(200)
        # Use the SVG's own viewBox to size the screenshot
        svg_box = page.evaluate("""() => {
            const svg = document.querySelector('svg');
            if (!svg) return null;
            const r = svg.getBoundingClientRect();
            return {x: r.x, y: r.y, width: r.width, height: r.height};
        }""")
        if svg_box and svg_box["width"] > 0 and svg_box["height"] > 0:
            page.screenshot(path=str(dst), clip=svg_box)
        else:
            page.screenshot(path=str(dst))
        browser.close()
    return str(dst)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python render_svg.py <path_to_svg>")
        sys.exit(1)
    out = render_svg_to_png(sys.argv[1])
    print(f"PNG saved: {out}")
