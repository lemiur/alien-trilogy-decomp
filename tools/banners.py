"""Quake III function banners: turn the comment above each function definition into a box banner.

    /*
    ==================
    Name

    description text
    ==================
    */

Every function definition (a line `{` at column 0 under a clang-formatted signature, K&R parameter
declarations included) gets one. The comment blocks directly above the signature (no blank line between)
are sorted:

* operational comments stay as separate comments between the banner and the definition, in their original
  order: `/* name: ... */` and `/* names: ... */` provenance, MATCH-PENDING, RULE-EXCEPTION, identity-TODO
  and `volatile:` notes. A descriptive comment that carries such a marker part-way is split at the marker:
  the text before it is description, the marker and what follows stay a comment of their own;
* every other block is description. Its text goes into the banner unchanged (the comment's own line breaks
  and paragraphs), except that a line longer than WRAP_COLUMNS is re-wrapped.

A function with no description gets a banner with just the name. A function that already has a box banner
above it is left alone, so a second run changes nothing. The tool only edits comments: the code tokens of
every file stay byte-identical. It refuses a dirty working tree (tracked changes) so that its effect is one
reviewable commit; `--check` lists the files it would change and exits 1 when there are any.

    python tools/banners.py [--check] [--allow-dirty] [FILES ...]     (default: src/*.c include/*.H)
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys
import textwrap

ROOT = Path(__file__).resolve().parent.parent
BAR = "=" * 18
WRAP_COLUMNS = 100

# Comments that must stay a comment of their own beside the definition.
OPERATIONAL_START = re.compile(r"^(names?\s*:|MATCH-PENDING\b|RULE-EXCEPTION\b|identity-TODO\b|volatile\s*:)")
# Markers that start the operational part of an otherwise descriptive comment.
EMBEDDED_MARKER = re.compile(r"(?<![\w-])(MATCH-PENDING|RULE-EXCEPTION|identity-TODO)\s*:"
                             r"|(?:^|(?<=\n)|(?<=[.;] ))names?\s*:")
SIGNATURE_NAME = re.compile(r"\b([A-Za-z_]\w*)\s*\(")


def comment_blocks_above(lines: list[str], sig: int) -> list[tuple[int, int]]:
    """(first, last) line indexes of the comment-only blocks that end directly above line `sig`,
    topmost first. A block is a /* ... */ that starts its line; a blank or code line ends the walk."""
    blocks = []
    end = sig - 1
    while end >= 0 and lines[end].rstrip().endswith("*/"):
        start = end
        while start >= 0 and not lines[start].lstrip().startswith("/*"):
            if start < end and "*/" in lines[start]:
                return blocks       # not a plain comment block
            start -= 1
        if start < 0 or lines[start][:1] != "/":
            return blocks           # a trailing comment of a code line, or an unterminated one
        blocks.insert(0, (start, end))
        end = start - 1
    return blocks


def comment_text(block: list[str]) -> list[str]:
    """The comment's text lines without the `/*`, ` * ` and `*/` decoration."""
    out = []
    for i, raw in enumerate(block):
        s = raw.rstrip()
        if i == 0:
            s = s.lstrip()[2:].strip()
        else:
            t = s.lstrip()
            if t.startswith("*/"):
                s = ""
            elif t.startswith("*"):
                s = t[1:]
                if s.startswith(" "):
                    s = s[1:]
        if i == len(block) - 1:
            s = re.sub(r"\s*\*/\s*$", "", s)
        out.append(s.rstrip())
    while out and not out[0].strip():
        out.pop(0)
    while out and not out[-1].strip():
        out.pop()
    return out


def is_banner(block: list[str]) -> bool:
    return len(block) >= 3 and block[0].strip() == "/*" and set(block[1].strip()) == {"="}


def render_comment(text: list[str]) -> list[str]:
    if len(text) == 1:
        return [f"/* {text[0]} */"]
    return [f"/* {text[0]}"] + [f" * {t}".rstrip() for t in text[1:-1]] + [f" * {text[-1]} */"]


def split_block(text: list[str]) -> tuple[list[str], list[str] | None]:
    """(description lines, operational comment lines or None) of one comment block."""
    if not text:
        return [], None
    first = text[0].strip()
    if OPERATIONAL_START.match(first):
        return [], text
    joined = "\n".join(text)
    m = EMBEDDED_MARKER.search(joined)
    if not m:
        return text, None
    head, tail = joined[:m.start()].rstrip(), joined[m.start():]
    return (head.split("\n") if head else []), tail.split("\n")


def wrap_description(lines: list[str]) -> list[str]:
    """Re-wrap only the paragraphs that hold a line longer than WRAP_COLUMNS."""
    out, para = [], []

    def flush():
        if any(len(p) > WRAP_COLUMNS for p in para):
            out.extend(textwrap.wrap(" ".join(p.strip() for p in para), WRAP_COLUMNS,
                                     break_long_words=False, break_on_hyphens=False))
        else:
            out.extend(para)
        para.clear()

    for line in lines:
        if line.strip():
            para.append(line)
        else:
            flush()
            out.append("")
    flush()
    return out


def function_definitions(lines: list[str]) -> list[tuple[int, str]]:
    """(first line of the signature, name) of every definition: `{` alone at column 0 under a signature."""
    found = []
    for i, line in enumerate(lines):
        if line.rstrip() != "{" or line[:1] != "{" or i == 0:
            continue
        j = i - 1
        # K&R parameter declarations (column 0, ending in `;`) sit between `)` and `{`.
        while j >= 0 and lines[j][:1] not in (" ", "\t", "") and lines[j].rstrip().endswith(";"):
            j -= 1
        if j < 0 or not lines[j].rstrip().endswith(")"):
            continue
        while j > 0 and lines[j][:1] in (" ", "\t"):
            j -= 1
        if lines[j][:1] in ("", "#", "/", "}", "{"):
            continue
        m = SIGNATURE_NAME.search(" ".join(l.strip() for l in lines[j:i]))
        if m:
            found.append((j, m.group(1)))
    return found


def convert(source: str) -> str:
    """The source with a banner above every function definition; comments only."""
    lines = source.split("\n")
    for sig, name in reversed(function_definitions(lines)):
        blocks = comment_blocks_above(lines, sig)
        texts = [lines[a:b + 1] for a, b in blocks]
        if any(is_banner(t) for t in texts):
            continue
        description: list[str] = []
        ops_lines: list[str] = []
        for t in texts:
            text = comment_text(t)
            desc, ops = split_block(text)
            if desc:
                if description:
                    description.append("")
                description.extend(desc)
            if ops is text:
                ops_lines.extend(t)         # wholly operational: keep the comment as written
            elif ops:
                ops_lines.extend(render_comment(ops))
        banner = ["/*", BAR, name]
        if description:
            banner += [""] + wrap_description(description)
        banner += [BAR, "*/"]
        first = blocks[0][0] if blocks else sig
        lines[first:sig] = banner + ops_lines
    return "\n".join(lines)


def dirty_tree() -> list[str]:
    out = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, text=True,
                         capture_output=True, check=True).stdout
    return [l for l in out.splitlines() if l.strip()]


def default_files() -> list[Path]:
    return sorted((ROOT / "src").glob("*.c")) + sorted((ROOT / "include").glob("*.H"))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="*", type=Path)
    ap.add_argument("--check", action="store_true", help="only list the files that would change; exit 1 if any")
    ap.add_argument("--allow-dirty", action="store_true", help="skip the clean-tree check")
    args = ap.parse_args(argv)
    if not args.check and not args.allow_dirty and (dirty := dirty_tree()):
        print("banners: refusing to run on a dirty tree:\n  " + "\n  ".join(dirty), file=sys.stderr)
        return 2
    changed = 0
    for path in args.files or default_files():
        old = path.read_bytes().decode("latin-1")
        new = convert(old)
        if new != old:
            changed += 1
            print(f"{path}: banners {'needed' if args.check else 'written'}")
            if not args.check:
                path.write_bytes(new.encode("latin-1"))
    print(f"banners: {changed} file(s) {'to change' if args.check else 'changed'}")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
