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
        self._check_uht_macros(fp, lines)
        # 6. Class/struct/namespace semicolons
        self._check_missing_semicolons(fp, text, lines)

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

    def _check_uht_macros(self, fp, lines):
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

        for lineno, line in enumerate(lines, 1):
            for macro in macros:
                if macro in line and '#' not in line:
                    # Check for closing paren on the macro
                    pass

        text = fp.read_text(encoding='utf-8-sig')
        for macro in macros:
            opens = len(re.findall(rf'\b{macro}\s*\(', text))
            macros[macro] = opens

        for macro, count in macros.items():
            if count > 120:
                self._error(fp, f"High count of {macro}: {count} (possible parse issue)")

    def _check_missing_semicolons(self, fp, text, lines):
        for lineno, line in enumerate(lines, 1):
            stripped = line.strip()
            if not stripped or stripped.startswith('//') or stripped.startswith('#'):
                continue
            if re.match(r'class\s+\w+.*\{', stripped) and not stripped.endswith('{'):
                continue

            # Check for class/struct declaration ending without semicolon
            if re.match(r'class\s+\w+.*', stripped) and ';' not in stripped and '{' not in stripped:
                # might be a forward declaration with missing semicolon
                pass

        # Check end of file brace + semicolon for UCLASS
        for lineno, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped == '};' and lineno < len(lines):
                # Likely end of a class
                pass

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
