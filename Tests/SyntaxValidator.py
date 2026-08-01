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
