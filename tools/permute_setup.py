"""Create a decomp-permuter directory for one function: builds/permute/<function>/.

    python tools/permute_setup.py f_40edc --context src/pause_menu.c

The base C is the function's draft (found by searching archive/drafts/src/, or --draft),
appended to --context, a live unit that supplies its types and declarations, when the draft
is not self-contained. The combined source is preprocessed by the project's own CPPPSX
(a DOSBox-X job via tools/permute_compile.py), and every function definition other than the
target and static or inline ones (which may be inlined into it) is reduced to a prototype.

The directory holds what decomp-permuter expects:
  base.c         preprocessed, single-function C
  target.o       retail bytes of the function (ELF, tools/permute_compile.py --target)
  compile.sh     ``compile.sh input.c -o output.o`` -> tools/permute_compile.py
  settings.toml  func_name, compiler_type "gcc" and objdump_command (permute_compile --disassemble)
  symbols.json   merged symbol map: draft map (archive/drafts/config/<draft>.json), the
                 context unit's map (config/<unit>.json) and any --map, later ones winning
  source.c       the combined source before preprocessing, for reference

It then compiles base.c once and prints its word difference from retail. Run the search with
tools/permute_run.py.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import sys

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import permute_compile as pc  # noqa: E402

PROJECT = TOOLS.parent
DRAFTS = PROJECT / "archive" / "drafts"


def find_draft(function: str) -> Path:
    pattern = re.compile(r"^[A-Za-z_][^;{}()]*\b" + re.escape(function) + r"\s*\([^;]*$", re.M)
    hits = [p for p in sorted((DRAFTS / "src").rglob("*.c")) if pattern.search(p.read_text(errors="replace"))]
    if len(hits) != 1:
        raise SystemExit(f"error: {len(hits)} drafts define {function}: {hits}; pass --draft")
    return hits[0]


def merge_maps(paths: list[Path]) -> dict:
    merged: dict = {"functions": {}, "symbols": {}, "sections": {}, "section_starts": {}}
    # Runtime library entry points (SDK names such as CdSearchFile) resolve as a fallback.
    runtime = PROJECT / "analysis" / "runtime_symbols.json"
    if runtime.is_file():
        for export in json.loads(runtime.read_text(encoding="utf-8")).get("exports", []):
            merged["symbols"][export["name"]] = export["address"]
    for path in paths:
        m = json.loads(path.read_text(encoding="utf-8"))
        for key in ("functions", "symbols", "sections", "section_starts"):
            merged[key].update(m.get(key, {}))
        if "gp" in m:
            merged["gp"] = m["gp"]
    return merged


def strip_linemarkers(text: str) -> str:
    return "".join(line for line in text.splitlines(keepends=True) if not line.lstrip().startswith("#"))


DIRECTIVE = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", re.M)  # with continuation lines


def bare_header(header: str) -> str:
    """A declaration's text without comments and preprocessor lines."""
    bare = re.sub(r"/\*.*?\*/|//[^\n]*", " ", header, flags=re.S)
    return DIRECTIVE.sub("", bare)


def top_level_functions(text: str) -> list[tuple[int, int, int, str]]:
    """(header start, body start, end, name) of each top-level function definition.

    Skips comments, string and character literals, and preprocessor lines, so it works on
    raw source as well as on CPPPSX output."""
    found, i, depth, last, body = [], 0, 0, 0, None
    n = len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and c == "#":
            while i < n and text[i] != "\n":
                i += 2 if text[i] == "\\" else 1
            continue
        if c == "\n":
            line_start = True
            i += 1
            continue
        if not c.isspace():
            line_start = False
        if text.startswith("/*", i):
            i = text.find("*/", i + 2)
            i = n if i < 0 else i + 2
            continue
        if text.startswith("//", i):
            i = text.find("\n", i)
            i = n if i < 0 else i
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            if depth == 0:
                header = bare_header(text[last:i])
                body = i if header.rstrip().endswith(")") and "=" not in header else None
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                if body is not None:
                    name = re.findall(r"(\w+)\s*\(", bare_header(text[last:body]))
                    if name:
                        found.append((last, body, i + 1, name[0]))
                    body = None
                last = i + 1
        elif c == ";" and depth == 0:
            last = i + 1
        i += 1
    return found


def strip_other_functions(text: str, keep: str) -> str:
    """Keep only the definition of keep. Another function that is named outside the other
    definitions (in keep's body, an initialiser or a declaration) is kept whole when it is
    static or inline (GCC may inline it) and reduced to a prototype otherwise; every other
    definition is removed, leaving only the preprocessor lines of its header behind."""
    defs = top_level_functions(text)
    target = [d for d in defs if d[3] == keep]
    if len(target) != 1:
        raise SystemExit(f"error: {len(target)} definitions of {keep}")
    outside, pos = [], 0
    for start, _, end, name in defs:
        if name != keep:
            outside.append(text[pos:start])
            pos = end
    outside.append(text[pos:])
    used = set(re.findall(r"\w+", bare_header("".join(outside))))
    out, pos = [], 0
    for start, body, end, name in defs:
        out.append(text[pos:start])
        header = text[start:body]
        bare = bare_header(header)
        directives = "".join(m.group(0) + "\n" for m in DIRECTIVE.finditer(header))
        if name == keep:
            out.append(text[start:end])
        elif name in used and set(re.findall(r"\w+", bare.split("(", 1)[0])) & {
                "static", "inline", "__inline__", "__inline"}:
            out.append(text[start:end])
        elif name in used:
            out.append("\n" + directives + bare.strip() + ";")
        else:
            out.append("\n" + directives)
        pos = end
    out.append(text[pos:])
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("function")
    ap.add_argument("--draft", type=Path, help="source defining the function (default: search archive/drafts/src)")
    ap.add_argument("--context", type=Path, help="live unit whose declarations the draft needs (src/<unit>.c)")
    ap.add_argument("--map", type=Path, action="append", default=[], help="extra symbol map(s), merged last")
    ap.add_argument("--section", action="append", default=[], metavar="NAME=ADDRESS",
                    help="override a section base, e.g. .rdata=0x800119bc for the function's jump table")
    ap.add_argument("--out", type=Path, help="default: builds/permute/<function>")
    ap.add_argument("--force", action="store_true", help="replace an existing directory")
    a = ap.parse_args()
    fn = a.function
    draft = (a.draft or find_draft(fn)).resolve()
    out = (a.out or PROJECT / "builds" / "permute" / fn).resolve()
    if out.exists():
        if not a.force:
            raise SystemExit(f"error: {out} exists; pass --force to replace it")
        shutil.rmtree(out)
    out.mkdir(parents=True)

    maps = []
    draft_map = DRAFTS / "config" / f"{draft.stem}.json"
    if draft_map.is_file():
        maps.append(draft_map)
    if a.context:
        unit_map = PROJECT / "config" / f"{a.context.stem}.json"
        if unit_map.is_file():
            maps.insert(0, unit_map)
    maps += [p.resolve() for p in a.map]
    if not maps:
        raise SystemExit("error: no symbol map found; pass --map")
    mapping = merge_maps(maps)
    for override in a.section:
        name, _, address = override.partition("=")
        mapping["sections"][name] = address
    if fn not in mapping["functions"]:
        raise SystemExit(f"error: {fn} has no address/size in {[str(p) for p in maps]}")
    for name, entry in mapping["functions"].items():  # the target only; others resolve as symbols
        if name != fn:
            mapping["symbols"].setdefault(name, entry["address"])
    mapping["functions"] = {fn: mapping["functions"][fn]}
    (out / "symbols.json").write_text(json.dumps(mapping, indent=2) + "\n", encoding="utf-8")

    parts = []
    if a.context:
        # The draft's definition is authoritative here: drop the context's own extern for it,
        # whose parameter types may disagree with the draft.
        context, dropped = re.subn(r"^[ \t]*extern\b[^;{}]*\b" + re.escape(fn) + r"\s*\([^;{}]*\)\s*;",
                                   "", a.context.read_text(encoding="utf-8"), flags=re.M)
        if dropped:
            print(f"permute_setup: dropped {dropped} extern declaration(s) of {fn} from {a.context}",
                  file=sys.stderr)
        parts.append(context)
    parts.append(draft.read_text(encoding="utf-8"))
    # Stripped before preprocessing too, so another draft in the same file that needs a
    # different unit's types cannot break the build.
    (out / "source.c").write_text(strip_other_functions("\n".join(parts), fn), encoding="utf-8")
    job = pc.compile_object(out / "source.c", out / "jobs")
    try:
        pre = (job / "INPUT.I").read_text(encoding="latin-1")
    finally:
        shutil.rmtree(job, ignore_errors=True)
    base = strip_other_functions(strip_linemarkers(pre), fn)
    if not re.search(r"\b" + re.escape(fn) + r"\s*\([^;]*\)\s*\{", base):
        raise SystemExit(f"error: {fn} is not defined in the combined source")
    (out / "base.c").write_text(base, encoding="utf-8")
    pc.write_elf(out / "target.o", fn, pc.retail_bytes(mapping, fn))

    py = Path(sys.executable).as_posix()
    tool = (TOOLS / "permute_compile.py").as_posix()
    (out / "compile.sh").write_text(
        "#!/bin/bash\n"
        f'exec "{py}" "{tool}" --map "{(out / "symbols.json").as_posix()}" --function {fn} '
        f'--build-root "{(out / "jobs").as_posix()}" "$@"\n', encoding="ascii", newline="\n")
    (out / "settings.toml").write_text(
        f'func_name = "{fn}"\ncompiler_type = "gcc"\n'
        f"objdump_command = '\"{py}\" \"{tool}\" --disassemble'\n", encoding="ascii", newline="\n")

    job = pc.compile_object(out / "base.c", out / "jobs")
    try:
        code = pc.resolve_function(job / "INPUT.OBJ", mapping, fn)
    finally:
        shutil.rmtree(job, ignore_errors=True)
    ref = pc.retail_bytes(mapping, fn)
    diffs = sum(1 for i in range(0, max(len(code), len(ref)), 4) if code[i:i + 4] != ref[i:i + 4])
    print(json.dumps({"directory": str(out), "draft": str(draft), "maps": [str(p) for p in maps],
                      "base_size": len(code), "retail_size": len(ref), "differing_words": diffs}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
