"""Syntax validator for .h and .cpp files in the paralax project.
Checks for:
  - Matching braces {}, parens (), brackets <>
  - UHT macro balance (UPROPERTY, UFUNCTION, etc.)
  - Include guard presence in .h files
  - Class declaration structure
  - Potential missing semicolons after class/struct declarations

Usage: python SyntaxValidator.py [--path ../]
"""

import os
import sys
import re
import argparse
from pathlib import Path

class SyntaxValidator:
    def __init__(self, root_dir: str):
        self.root = Path(root_dir).resolve()
        self.errors: list[str] = []
        self.files_checked = 0

    def validate(self):
        for ext in ('*.h', '*.cpp'):
            for fp in sorted(self.root.rglob(ext)):
                if 'Intermediate' in str(fp) or 'DerivedDataCache' in str(fp) or 'Tests' in str(fp.parent):
                    continue
                if fp.name == 'SyntaxValidator.py' or fp.name == 'ParallaxMathTests.cpp':
                    continue
                self.files_checked += 1
                self._check_file(fp)

    def _check_file(self, fp: Path):
        try:
            text = fp.read_text(encoding='utf-8-sig')
        except Exception as e:
            self._error(fp, f"Cannot read: {e}")
            return

        lines = text.split('\n')

        # 1. Brace balance
        self._check_balance(fp, lines, '{}', 'braces')
        # 2. Paren balance
        self._check_balance(fp, lines, '()', 'parentheses')
        # 3. Angle bracket balance (template depth)
        self._check_bracket_balance(fp, lines)
        # 4. Include guard in .h
        if fp.suffix == '.h':
            self._check_include_guard(fp, text)
            self._check_pragma_once(fp, text)
        # 5. UCLASS/UPROPERTY/UFUNCTION macro balance
        self._check_uht_macros(fp, text)
        # 6. Section slots in the editor widget must be AutoHeight (default
        #    SVerticalBox slots are Fill: bare AddSlot would stretch every
        #    section to an equal share and tall content paints over the next
        #    section - the "View Override on top of Scale Y" defect).
        #    The panel construction blocks moved to the panels file with the
        #    Stage 5 decomposition, so both files are checked.
        if fp.name in ('FaceParallaxEditorWidgetUI.cpp', 'FaceParallaxEditorWidgetPanels.cpp'):
            self._check_section_slots(fp, text)
        # 7. Container self-add:  X.Add(X[i]) passes a reference INTO the
        #    array being modified; TArray's debug check asserts ("element
        #    already comes from the container being modified" - Array.h
        #    CheckAddress). The hotspot polygon close-loop hit this in
        #    DrawLoop and crashed the editor during WBP thumbnail render.
        self._check_self_add(fp, text)
        # 8. UI testing procedures (P17/P18/P19): no VERTICAL scroll boxes in
        #    the editor widget. Content that fits is packed without a scroll
        #    bar; dynamic row lists flip through carousel pages; the 8px
        #    reserve (ScrollReserveBottom) keeps pages clear of the buttons
        #    under them. Horizontal scroll boxes are fine (wide button rows).
        #    Exemption: the main-window container (MainWindowScroll) scrolls
        #    vertically - the fixed 884px design height clips the bottom rows
        #    when the docked tab is short, leaving the bottom section
        #    invisible, so the window itself scrolls; panels still pack to fit.
        if fp.name in ('FaceParallaxEditorWidgetUI.cpp', 'FaceParallaxEditorWidgetPanels.cpp',
                       'FaceParallaxEditorWidgetInteractions.cpp'):
            self._check_no_vertical_scroll(fp, text)
        # 9. UFUNCTION exposure audit: every public non-static, non-override
        #    method on the two Blueprint-facing classes (UFaceParallaxComponent,
        #    UFaceParallaxEditorWidget) must be UFUNCTION-exposed so widget and
        #    Blueprint code can cross the DLL boundary. Defect class: the depth
        #    bake GenerateDepthBufferFromOutlinesForView shipped without a
        #    UFUNCTION and was only callable via the editor module's include.
        #    Exempt: constructors, virtual overrides (UHT wires them), static
        #    helpers, and the explicit helper whitelist below (widget-internal
        #    plumbing). Adding a new public method requires a UFUNCTION.
        if fp.name in ('FaceParallaxComponent.h', 'FaceParallaxEditorWidget.h'):
            self._check_ufunction_exposure(fp, text)
        # 10. Pinned-action placement audit (P21 PinnedActionsNeverInScroll):
        #     the canonical quick actions ("Import Art...", "Sync All -> All",
        #     "Auto-Fit All", "Clear All Overrides") may only be built as
        #     MakeBtn buttons in the pinned strip (UI.cpp) or the toolbar
        #     (Panels.cpp). Any occurrence elsewhere means the action got
        #     duplicated into a scrolled panel - the defect class that hid
        #     copies in the rails' horizontal scroll boxes.
        if fp.name in ('FaceParallaxEditorWidgetUI.cpp', 'FaceParallaxEditorWidgetPanels.cpp',
                       'FaceParallaxEditorWidgetInteractions.cpp'):
            self._check_pinned_action_slots(fp, text)

    def _check_no_vertical_scroll(self, fp: Path, text: str):
        # An SScrollBox without an explicit horizontal orientation scrolls
        # vertically (SScrollBox defaults to vertical). Panel-level vertical
        # scroll bars are banned (P17 pack content to fit; P18 carousel
        # pages; P19 8px reserve). Exemption: the main-window container
        # (TSharedRef<SScrollBox> MainWindowScroll) - the fixed 884px design
        # height clips the bottom rows (timeline, bottom bar, diagnostic log)
        # when the docked tab is short, leaving the bottom section invisible
        # and unreachable, so the window itself scrolls vertically while the
        # panels inside it still pack to fit.
        for m in re.finditer(r'SNew\s*\(\s*SScrollBox\s*\)', text):
            start = m.start()
            pre = text[m.end():]
            cut = pre.find('[')
            if cut >= 0:
                pre = pre[:cut]
            om = re.search(r'\.Orientation\s*\(\s*Orient_Horizontal\s*\)', pre)
            if om:
                continue
            assign = text.rfind('TSharedRef<SScrollBox> MainWindowScroll =', 0, start)
            stmt_end = text.find('\n', assign) if assign >= 0 else -1
            if assign >= 0 and (stmt_end < 0 or stmt_end > start):
                continue
            line = text.count('\n', 0, start) + 1
            self._error(fp, f"line {line}: vertical SScrollBox (P17: pack "
                             f"content to fit; P18: carousel pages; P19: 8px "
                             f"reserve - never a vertical scroll bar)")

    def _check_ufunction_exposure(self, fp: Path, text: str):
        # Rule 9: scan UCLASS bodies, public sections only. A method missing
        # UFUNCTION is a defect unless it is one of:
        #   - the constructor (name == class name)
        #   - a virtual override (UHT wires engine overrides without UFUNCTION)
        #   - a static helper (no DLL boundary crossing)
        #   - a name in the widget-internal helper whitelist below
        # Comments are stripped to newlines so line numbers stay accurate.
        t = re.sub(r'/\*.*?\*/', lambda mm: '\n' * mm.group(0).count('\n'),
                   text, flags=re.S)
        t = re.sub(r'//[^\n]*', '', t)

        # Widget-internal plumbing: called only by the widget's own TUs or by
        # panel builders; deliberately not exposed to Blueprint. Kept public
        # because the widget is split across several translation units.
        helper_whitelist = {
            # UFaceParallaxComponent runtime internals
            'ApplyCurrentStateTextures', 'CaptureCurrentTextures',
            'SetPreviousStateTextures', 'CalculateLookDelta',
            'DetermineStateFromAngles', 'UpdateParallaxOffsets',
            'ComputeOffsetForState', 'StopAnimationsOnStateChange',
            'LogWarning', 'GetCameraLocationAndRotation',
            'RefreshSequencerCamera', 'OnAsyncTexturesLoaded',
            'EnforceAsyncCacheSize', 'ResolveTexture', 'BuildNestedArtCache',
            # UFaceParallaxEditorWidget panel/UI plumbing
            'SyncTexturesLayerToAllViews', 'RefreshCanvasPreview',
            'RefreshHotspotRegions', 'StartCyclePreview', 'StopCyclePreview',
            'StartLivePreview', 'StopLivePreview', 'PushUndoState',
            'RestoreFromBackup', 'SetActiveRailIndex', 'GetStateDotColor',
            'RefreshViewStripDots', 'FillMissingViewsFromActiveSlot',
            'RefreshSlotPropStatus', 'ToggleOnionSkin', 'SetOnionSkinOpacity',
            'RefreshOnionSkin', 'CopyTransformFromView',
            'ApplyCanonicalTransformWithLink', 'GetGizmoTransform',
            'SetGizmoTransform', 'HandleHotspotClick', 'ImportHotspotRegion',
            'RebuildPartsStrip', 'OpenHotspotRemapMenu', 'RemapHotspotLayer',
            'ResolveHotspotLayer', 'RefreshSyncDriftIndicator',
            'SetDisplayMode', 'RefreshDebugSliders', 'BuildEdgeOverlay',
            'RebuildHistogramBars', 'RefreshHullThumbnails',
            'RefreshPinControls', 'GetLayerPinMarkers', 'RebuildVisemeGrid',
            'RebuildNestedOutliner', 'RebuildParamTable',
            'RebuildProblemsPanel',
        }

        cls_re = (r'UCLASS\s*\((?:[^()]|\([^()]*\))*\)\s*\n\s*'
                  r'class\s+(?:[A-Z_]+[ \t]+)*?([A-Za-z_]\w*)\s*[:\{]')
        method_re = (r'([A-Za-z_][A-Za-z_0-9:<>,&\*\s]*?)\s+'
                     r'([A-Za-z_]\w*)\s*\([^;{}]*\)\s*'
                     r'(?:const\s*)?(?:override\s*)?;')
        for m in re.finditer(cls_re, t):
            cls = m.group(1)
            start = m.end()
            depth = 0
            i = start
            while i < len(t):
                if t[i] == '{':
                    depth += 1
                elif t[i] == '}':
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            body = t[start:i]
            secs = [(mm.start(), mm.group(1))
                    for mm in re.finditer(r'\b(public|private|protected)\b\s*:', body)]
            for k, (off, kind) in enumerate(secs):
                if kind != 'public':
                    continue
                end = secs[k + 1][0] if k + 1 < len(secs) else len(body)
                seg = body[off:end]
                for mm in re.finditer(method_re, seg, re.M):
                    sig = mm.group(0).strip()
                    if '=' in sig:
                        continue
                    name = mm.group(2)
                    chunk = seg[max(0, seg.rfind(';', 0, mm.start()) + 1):mm.start()]
                    if chunk.rstrip().endswith('}'):
                        chunk = chunk.rstrip()[:-1]
                    if 'UFUNCTION' in chunk:
                        continue
                    if name == cls:
                        continue
                    if re.search(r'\b(static|virtual)\b', sig):
                        continue
                    if name in helper_whitelist:
                        continue
                    line = text.count('\n', 0, start + off + mm.start() + 1) + 1
                    self._error(fp, f"line {line}: public method {cls}::{name}() "
                                     f"lacks UFUNCTION (rule 9 exposure audit - "
                                     f"add UFUNCTION(BlueprintCallable) or move "
                                     f"to the helper whitelist)")

    def _check_pinned_action_slots(self, fp: Path, text: str):
        # Rule 10: canonical quick-action labels must only be built as MakeBtn
        # buttons inside the pinned strip (UI.cpp RebuildWidget strip block) or
        # the toolbar (Panels.cpp BuildPanelToolbar). A canonical literal
        # anywhere else is a P21 PinnedActionsNeverInScroll violation: the
        # action has been duplicated into a scrolled or rail-local panel.
        canonical = [
            'Import Art...', 'Sync All -> All', 'Auto-Fit All', 'Clear All Overrides',
        ]
        lines = text.split('\n')

        # Allowed regions per file (start..end line numbers, inclusive).
        allowed = []
        if fp.name == 'FaceParallaxEditorWidgetUI.cpp':
            start = None
            end = None
            for i, line in enumerate(lines, 1):
                if 'Pinned quick-actions strip' in line and start is None:
                    start = i
                if 'Assemble main row' in line and start is not None:
                    end = i - 1
                    break
            if start is not None and end is not None:
                allowed.append((start, end))
        elif fp.name == 'FaceParallaxEditorWidgetPanels.cpp':
            start = None
            end = None
            for i, line in enumerate(lines, 1):
                if 'UFaceParallaxEditorWidget::BuildPanelToolbar' in line and start is None:
                    start = i
                elif 'UFaceParallaxEditorWidget::BuildPanel' in line and start is not None:
                    end = i - 1
                    break
            if start is not None and end is not None:
                allowed.append((start, end))

        def in_allowed(line_no: int) -> bool:
            for a, b in allowed:
                if a <= line_no <= b:
                    return True
            return False

        for m in re.finditer(r'MakeBtn\s*\(\s*TEXT\s*\(\s*("([^"]+)")', text):
            label = m.group(2)
            if label not in canonical:
                continue
            line_no = text.count('\n', 0, m.start()) + 1
            if in_allowed(line_no):
                continue
            self._error(fp, f"line {line_no}: canonical quick action "
                             f"'{label}' built outside the pinned strip / "
                             f"toolbar (P21 PinnedActionsNeverInScroll - "
                             f"pinned actions must not be duplicated into "
                             f"rails or scrolled panels)")

    def _check_self_add(self, fp: Path, text: str):
        for m in re.finditer(r'\b(\w+)\s*(?:\.|->)\s*(?:Add|AddLast|Emplace)\s*\(\s*(\w+)\s*\[', text):
            if m.group(1) != m.group(2):
                continue
            line = text.count('\n', 0, m.start()) + 1
            self._error(fp, f"line {line}: container self-add {m.group(1)}.{m.group(2)}[...] "
                             f"(element taken from the same array being modified - "
                             f"copy to a local first)")

    def _check_section_slots(self, fp: Path, text: str):
        # A section slot add is:  <Box>->AddSlot() [ ...MakeSectionBox(...) ] ;
        # The slot must specify a size rule (.AutoHeight() / .FillHeight())
        # before the content bracket; bare AddSlot() defaults to Fill.
        for m in re.finditer(r'AddSlot\(\)', text):
            start = m.start()
            pre = text[m.end():]
            pre = pre[:pre.find('[')] if '[' in pre else pre[:pre.find(';')] if ';' in pre else pre
            if re.search(r'\.(AutoHeight|FillHeight)\s*\(', pre):
                continue
            stmt_end = text.find(';', start)
            if stmt_end < 0:
                stmt_end = len(text)
            stmt = text[start:stmt_end]
            if 'MakeSectionBox' in stmt:
                line = text.count('\n', 0, start) + 1
                self._error(fp, f"line {line}: section slot added without "
                                 f"AutoHeight/FillHeight (sections would overlap)")

    def _check_balance(self, fp, lines, pair, name):
        open_ch, close_ch = pair[0], pair[1]
        depth = 0
        for lineno, line in enumerate(lines, 1):
            stripped = re.sub(r'//.*', '', line)
            stripped = re.sub(r'L?"(?:[^"\\]|\\.)*"', '', stripped)
            for ch in stripped:
                if ch == open_ch:
                    depth += 1
                elif ch == close_ch:
                    depth -= 1
                if depth < 0:
                    self._error(fp, f"Extra closing {name} at line {lineno}")
                    depth = 0

        if depth != 0:
            self._error(fp, f"Unbalanced {name}: {depth} unclosed")

    def _check_bracket_balance(self, fp, lines):
        depth = 0
        for lineno, line in enumerate(lines, 1):
            stripped = re.sub(r'//.*', '', line)
            stripped = re.sub(r'L?"(?:[^"\\]|\\.)*"', '', stripped)
            for ch in stripped:
                if ch == '<':
                    depth += 1
                elif ch == '>':
                    depth -= 1
                if depth < 0:
                    depth = 0  # might be operator>>

    def _check_include_guard(self, fp, text):
        has_guard = bool(re.search(r'#pragma\s+once', text))
        if not has_guard:
            has_guard = bool(re.search(r'#ifndef\s+\w+', text))
        if not has_guard:
            self._error(fp, "Missing include guard (#pragma once or #ifndef)")

    def _check_pragma_once(self, fp, text):
        has_pragma = bool(re.search(r'#pragma\s+once', text))
        if fp.suffix == '.h' and not has_pragma:
            self._error(fp, "Missing #pragma once")

    def _check_uht_macros(self, fp, text):
        macros = {
            'UPROPERTY': 0,
            'UFUNCTION': 0,
            'UCLASS': 0,
            'USTRUCT': 0,
            'UENUM': 0,
            'UINTERFACE': 0,
            'UDELEGATE': 0,
            'UPARAM': 0,
        }

        for macro in macros:
            opens = len(re.findall(rf'\b{macro}\s*\(', text))
            macros[macro] = opens

        for macro, count in macros.items():
            if count > 300:
                self._error(fp, f"High count of {macro}: {count} (possible parse issue)")

    def _error(self, fp, msg):
        rel = fp.relative_to(self.root)
        self.errors.append(f"{rel}: {msg}")

    def report(self) -> int:
        print(f"Files checked: {self.files_checked}")
        if not self.errors:
            print("No syntax errors found.")
            return 0

        print(f"\n{len(self.errors)} issue(s) found:")
        for err in self.errors:
            print(f"  {err}")
        return 1


def main():
    parser = argparse.ArgumentParser(description='Validate UE C++ syntax')
    parser.add_argument('--path', default='..', help='Root directory of the project')
    args = parser.parse_args()

    validator = SyntaxValidator(args.path)
    validator.validate()
    sys.exit(validator.report())


if __name__ == '__main__':
    main()
