"""Report raw hex (or, with --decimal, meaningful decimal) integer literals in C sources
(the "No magic numbers" source rule).

Usage:
    python tools/lint_literals.py [--files FILE ...] [--decimal] [--report-only]

Scans src/*.c and include/*.H (or the files given to --files) and prints one
`file:line:col literal  context` line per finding, then a per-file count. Exit status is 1
when any finding exists, unless --report-only.

A hex literal (0x..., any case, optional u/l suffixes) is allowed only
  - inside a comment, string literal or character literal;
  - on an object-like `#define NAME value` line (continuations included): the hex lives on
    the defining line. A function-like macro (`#define NAME(args) body`) is code, so hex in its
    body is reported, as is hex in any other directive (`#if`, `#pragma`, ...);
  - inside an enum body (`enum [tag] { ... }`), where member initialisers define the constants;
  - inside the brace-enclosed initialiser of a definition at file scope (`T x[] = { ... };`,
    nested braces included): bulk data that reproduces retail values. Brace initialisers inside
    function bodies (local initialisers) and array bounds or other hex outside the braces are
    reported.
Everything else is reported: expressions, masks, arguments, case labels, array indices,
assignments and struct-member stores.

--decimal reports decimal (and octal) integer literals instead of hex ones, under the same
allowed contexts, and additionally allows
  - the values 0, 1 and 2 (so 0, 1, -1 and 2 in arithmetic; a negative literal is a unary
    minus applied to its operand);
  - literals inside the parenthesised operand of `sizeof` (`sizeof(char[16])`). A literal
    multiplying or dividing a sizeof term (`16 * sizeof(T)`) is outside it and is reported.
Floating-point literals are not reported in either mode.

The scanner is a plain tokenizer (comments, strings, character literals, line continuations);
no compiler is needed.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEX = re.compile(r"0[xX][0-9a-fA-F]+[uUlL]*$")
DEC = re.compile(r"([0-9]+)[uUlL]*$")
DECIMAL_ALLOWED = {0, 1, 2}


def _is_ident_start(c: str) -> bool:
    return c.isalpha() or c in "_$"


def _is_ident(c: str) -> bool:
    return c.isalnum() or c in "_$"


def _decimal_value(tok: str) -> int | None:
    """Value of a decimal or octal integer literal, or None for hex, floats and malformed tokens."""
    m = DEC.match(tok)
    if not m:
        return None
    digits = m.group(1)
    if len(digits) > 1 and digits[0] == "0":
        try:
            return int(digits, 8)
        except ValueError:
            return None
    return int(digits)


def scan(text: str, decimal: bool = False) -> list[tuple[int, int, str]]:
    """Return (line, col, literal) for every disallowed literal; line and col are 1-based.

    Hex literals by default; decimal/octal literals other than 0, 1 and 2 when `decimal`."""
    findings: list[tuple[int, int, str]] = []
    n = len(text)
    i = 0
    line = 1
    line_start = 0
    at_line_start = True      # no token seen yet on this logical line
    directive: str | None = None   # None, or 'allow' / 'code' while inside a preprocessor line
    stack: list[str] = []     # brace kinds: 'enum', 'init', 'block'
    prev = prev2 = ""         # last two significant tokens
    parens: list[bool] = []   # open parentheses: True when inside a sizeof operand
    last = ""                 # last token in any context (sizeof detection inside directives)

    def newline(pos: int) -> None:
        nonlocal line, line_start
        line += 1
        line_start = pos + 1

    def push(tok: str) -> None:
        nonlocal prev, prev2
        prev2, prev = prev, tok

    while i < n:
        c = text[i]
        if c == "\\" and text[i + 1:i + 2] == "\n":
            newline(i + 1)
            i += 2
            continue
        if c == "\\" and text[i + 1:i + 3] == "\r\n":
            newline(i + 2)
            i += 3
            continue
        if c == "\n":
            newline(i)
            i += 1
            at_line_start = True
            if directive is not None:
                parens.clear()
            directive = None
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for k in range(i, end):
                if text[k] == "\n":
                    newline(k)
            i = end
            continue
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                if text[i] == "\\" and text[i + 1:i + 2] == "\n":
                    newline(i + 1)
                    i += 1
                elif text[i] == "\\" and text[i + 1:i + 3] == "\r\n":
                    newline(i + 2)
                    i += 2
                i += 1
            continue
        if c in "\"'":
            quote = c
            i += 1
            while i < n and text[i] != quote and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n:
                    if text[i + 1] == "\n":
                        newline(i + 1)
                    i += 1
                i += 1
            if i < n and text[i] == quote:
                i += 1
            at_line_start = False
            if directive is None:
                push("str")
            continue
        if c == "#" and at_line_start:
            at_line_start = False
            j = i + 1
            while j < n and text[j] in " \t":
                j += 1
            k = j
            while k < n and _is_ident(text[k]):
                k += 1
            name = text[j:k]
            directive = "code"
            if name == "define":
                m = k
                while m < n and text[m] in " \t":
                    m += 1
                p = m
                while p < n and _is_ident(text[p]):
                    p += 1
                if p > m and text[p:p + 1] != "(":
                    directive = "allow"
                k = p
            elif name == "include":
                directive = "allow"
            i = k
            continue
        at_line_start = False
        if c.isdigit() or (c == "." and text[i + 1:i + 2].isdigit()):
            j = i
            while j < n:
                d = text[j]
                if _is_ident(d) or d == ".":
                    j += 1
                elif d in "+-" and text[j - 1] in "eE" and not text[i:j].lower().startswith("0x"):
                    j += 1
                else:
                    break
            tok = text[i:j]
            if decimal:
                value = _decimal_value(tok)
                flagged = (value is not None and value not in DECIMAL_ALLOWED
                           and not (parens and parens[-1]))
            else:
                flagged = bool(HEX.match(tok))
            if flagged:
                if directive == "code":
                    findings.append((line, i - line_start + 1, tok))
                elif directive is None and not any(k in ("enum", "init") for k in stack):
                    findings.append((line, i - line_start + 1, tok))
            if directive is None:
                push("num")
            last = "num"
            i = j
            continue
        if _is_ident_start(c):
            j = i
            while j < n and _is_ident(text[j]):
                j += 1
            if directive is None:
                push(text[i:j])
            last = text[i:j]
            i = j
            continue
        if c == "(":
            parens.append(last == "sizeof" or bool(parens and parens[-1]))
        elif c == ")" and parens:
            parens.pop()
        last = c
        if directive is None:
            if c == "{":
                if prev == "enum" or (prev2 == "enum" and (prev[:1].isalpha() or prev[:1] == "_")):
                    stack.append("enum")
                elif prev == "=" and (not stack or stack[-1] == "init"):
                    stack.append("init")
                elif stack and stack[-1] == "init":
                    stack.append("init")
                else:
                    stack.append("block")
            elif c == "}":
                if stack:
                    stack.pop()
            push(c)
        i += 1
    return findings


def default_files() -> list[Path]:
    files = set((ROOT / "src").glob("*.c")) | set((ROOT / "include").glob("*.[hH]"))
    return sorted(files)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--files", nargs="+", type=Path, help="scan only these files")
    ap.add_argument("--decimal", action="store_true",
                    help="report decimal/octal literals other than 0, 1 and 2 instead of hex")
    ap.add_argument("--report-only", action="store_true", help="always exit 0")
    args = ap.parse_args(argv)
    files = args.files if args.files else default_files()
    counts: dict[str, int] = {}
    total = 0
    for path in files:
        text = path.read_text(encoding="latin-1")
        lines = text.splitlines()
        try:
            shown = path.resolve().relative_to(ROOT).as_posix()
        except ValueError:
            shown = path.as_posix()
        for ln, col, lit in scan(text, decimal=args.decimal):
            ctx = lines[ln - 1].strip() if ln <= len(lines) else ""
            print(f"{shown}:{ln}:{col} {lit}  {ctx[:100]}")
            counts[shown] = counts.get(shown, 0) + 1
            total += 1
    print()
    for name in sorted(counts):
        print(f"{counts[name]:6d}  {name}")
    print(f"{total:6d}  total in {len(files)} files")
    return 0 if args.report_only or total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
