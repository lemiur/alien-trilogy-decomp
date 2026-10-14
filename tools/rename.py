"""Apply the naming map (config/style_renames.json) to the source tree, token-wise.

    python tools/rename.py --dry-run          # counts per file and the collision check; writes nothing
    python tools/rename.py                    # applies the map (refuses on a dirty tree)

What is renamed, and where:
  * C (src/*.c, include/*.H): every identifier token outside string and character literals and
    #include lines. A token after struct/union/enum takes the `tags` entry; any other token the
    explicit entry (functions, globals, types, enum values and constants, function-like macros);
    a remaining lower-case snake_case identifier (locals, parameters, struct members, labels)
    becomes lowerCamel by the map's local rule, unless it is an SDK or library name, a lineage
    name, an address placeholder or a member marked ambiguous.
  * Comments in C and assembly: whole words only; explicit names of more than one word (an
    underscore or an inner capital), and identifiers the code pass renamed. One-word names
    (random, Door, Panel) are left in prose, where they read as English.
  * Renderer assembly (asm/*.s, asm/*.h; never asm/lib/): explicit function and global names in code.
  * config/*.json (except the map itself): a JSON string equal to an explicit name, a compiler
    local-static label `name.N`, and whole words inside prose strings as for comments.
  * tools/test_gate.py: some tests read real symbol maps by name (ot_depth); explicit names inside
    its string literals are renamed as whole words, except names the frozen fixtures under
    tools/testdata/ use (those tests compile the frozen copies, which keep their spelling).
  * docs/**/*.md, README.md, AGENTS.md: prose, as for comments.
String literals are never changed: names inside strings reach the binary.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

C_KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern",
    "float", "for", "goto", "if", "int", "long", "register", "return", "short", "signed", "sizeof", "static",
    "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "inline", "asm", "typeof",
    "__inline__", "__asm__", "__attribute__", "__volatile__", "__extension__", "__typeof__", "__const__",
}
TAG_KEYWORDS = {"struct", "union", "enum"}
PLACEHOLDER = re.compile(r"^[fgs]_[0-9a-f]{5,8}$")


def placeholder_patterns():
    """tools/rules.py's R7 placeholder patterns: such names are kept, and no new name may look like one."""
    try:
        import rules
        return tuple(rules.PLACEHOLDER) + (PLACEHOLDER,)
    except ImportError:
        return (PLACEHOLDER,)


PLACEHOLDERS = placeholder_patterns()


def is_placeholder(name: str) -> bool:
    return any(p.match(name) for p in PLACEHOLDERS)
SNAKE = re.compile(r"^[a-z][a-z0-9]*(?:_[a-z0-9]+)+$")
TOKEN = re.compile(r"\d[\w.]*|[A-Za-z_]\w*")
WORD = re.compile(r"[A-Za-z_]\w*")
LOCAL_STATIC = re.compile(r"^([A-Za-z_]\w*)\.(\d+)$")
JSON_STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')


# ------------------------------------------------------------------ the map

class RenameMap:
    def __init__(self, data: dict, sdk_names: set, lib_names: set):
        self.data = data
        rules = data.get("_rules", {})
        self.acronyms = set(rules.get("acronyms", []))
        self.acronym_form = dict(rules.get("acronym_forms", {}))
        self.explicit = {}      # old -> new for non-tag kinds (only entries that change)
        self.kind_of = {}
        self.tags = {}
        self.keep = set(sdk_names) | set(lib_names)
        self.problems = []
        for kind in ("functions", "globals", "types", "enums_constants", "macros_functionlike"):
            for old, e in data.get(kind, {}).items():
                if "keep" in e or e.get("new", old) == old:
                    self.keep.add(old)
                    continue
                if old in self.explicit and self.explicit[old] != e["new"]:
                    self.problems.append(f"{old}: two new names ({self.explicit[old]}, {e['new']})")
                self.explicit[old] = e["new"]
                self.kind_of[old] = kind
        for old, e in data.get("tags", {}).items():
            if "keep" in e or e.get("new", old) == old:
                continue
            self.tags[old] = e["new"]
        self.ambiguous = set()
        member_new = defaultdict(set)
        for owner, block in data.get("members", {}).items():
            for m, e in block.get("members", {}).items():
                if "ambiguous" in e:
                    self.ambiguous.add(m)
                elif "keep" in e:
                    self.keep.add(m)  # a member the map keeps (placeholder, lineage): never touched
                else:
                    member_new[m].add(e["new"])
        for m, news in member_new.items():
            if len(news) > 1:
                self.ambiguous.add(m)
                self.problems.append(f"member {m}: different new names per owner {sorted(news)}; left as ambiguous")
            elif m not in self.ambiguous and self.local_rule(m) != next(iter(news)):
                self.problems.append(f"member {m}: new name {next(iter(news))} differs from the local rule")
            if m in self.explicit:
                self.problems.append(f"member {m}: same spelling as the {self.kind_of[m]} entry; manual handling")
                self.ambiguous.add(m)

    def cap(self, w: str) -> str:
        if w in self.acronym_form:
            return self.acronym_form[w]
        if w in self.acronyms:
            return w.upper()
        return w[:1].upper() + w[1:]

    def local_rule(self, name: str) -> str:
        words = name.split("_")
        return words[0] + "".join(w if w[:1].isdigit() else self.cap(w) for w in words[1:])

    def mechanical(self, name: str):
        """New name of a local/parameter/member token by the local rule, or None."""
        if (not SNAKE.match(name) or name in self.keep or name in self.ambiguous or name in C_KEYWORDS
                or is_placeholder(name)):
            return None
        new = self.local_rule(name)
        return new if new != name else None

    def prose_names(self):
        """Explicit names safe to replace as whole words in prose (more than one word)."""
        out = {}
        for old, new in self.explicit.items():
            if "_" in old.strip("_") or re.search(r"[a-z][A-Z]", old):
                out[old] = new
        for old, new in self.tags.items():
            if old not in self.explicit and re.search(r"[a-z][A-Z]", old):
                out[old] = new
        return out


def sdk_declared_names():
    """Names the SDK headers declare (macros, types, members, functions, globals; not prototype parameters)."""
    import rules  # tools/rules.py
    names = set()
    root = rules.sdk_include_dir()
    if root is None:
        raise SystemExit("rename: the Runtime 3.3 include directory (config/toolchain.lock.json) is missing")
    for p in sorted(root.rglob("*")):
        if not p.is_file():
            continue
        src = rules.lex(p, p.name, p.read_text(encoding="utf-8", errors="replace"))
        depth = 0
        for t in src.toks:
            if t.text == "(":
                depth += 1
            elif t.text == ")":
                depth -= 1
            elif t.kind == "id" and depth == 0:
                names.add(t.text)
        for _, _, d in src.directives:
            m = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)", d)
            if m:
                names.add(m.group(1))
    return names


def library_names(project: Path):
    names = set()
    for p in sorted((project / "config").glob("lib*.json")):
        d = json.loads(p.read_text(encoding="utf-8"))
        for k in ("functions", "symbols", "static_data"):
            names |= set(d.get(k, {}))
    lib = project / "asm" / "lib"
    if lib.is_dir():
        for p in sorted(lib.iterdir()):
            if p.is_file():
                txt = p.read_text(encoding="utf-8", errors="replace")
                names |= set(re.findall(r"^\s*\.(?:globl|global|extern|comm|lcomm)\s+([A-Za-z_]\w*)", txt, re.M))
                names |= set(re.findall(r"^([A-Za-z_]\w*):", txt, re.M))
    return names


# ------------------------------------------------------------------ scanning

def segments(text: str):
    """Split C/CPP-assembly text into (kind, text): code, comment, string, char, include."""
    out, i, n = [], 0, len(text)
    code_start = 0
    line_start = True

    def flush(upto):
        if upto > code_start:
            out.append(("code", text[code_start:upto]))

    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            flush(i)
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(("comment", text[i:j]))
            i = code_start = j
            line_start = False
            continue
        if text.startswith("//", i):
            flush(i)
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(("comment", text[i:j]))
            i = code_start = j
            continue
        if c in "\"'":
            flush(i)
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(("string" if c == '"' else "char", text[i:j]))
            i = code_start = j
            line_start = False
            continue
        if line_start and c == "#" and re.match(r"#\s*include\b", text[i:i + 40]):
            flush(i)
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(("include", text[i:j]))
            i = code_start = j
            continue
        if c == "\n":
            line_start = True
        elif not c.isspace():
            line_start = False
        i += 1
    flush(n)
    return out


def replace_words(text: str, table: dict, counter: Counter, key: str, left: Counter = None, one_word=()):
    def sub(m):
        w = m.group(0)
        if w in table:
            counter[key] += 1
            return table[w]
        if left is not None and w in one_word:
            left[w] += 1
        return w
    return WORD.sub(sub, text)


# ------------------------------------------------------------------ transforms

class Renamer:
    def __init__(self, rmap: RenameMap):
        self.m = rmap
        self.code_renamed = {}     # mechanical old -> new, collected over every C file
        self.prose = rmap.prose_names()
        self.one_word = {o for o in list(rmap.explicit) + list(rmap.tags) if o not in self.prose}
        self.left_in_prose = Counter()

    def c_code(self, text: str, counter: Counter, asm: bool = False):
        segs = segments(text)
        out = []
        prev = ""
        for kind, s in segs:
            if kind != "code":
                out.append((kind, s))
                continue
            pieces, last = [], 0
            for tm in re.finditer(r"\d[\w.]*|[A-Za-z_]\w*|->|\S", s):
                tok = tm.group(0)
                new = None
                if WORD.fullmatch(tok):
                    if asm:
                        if tok in self.m.explicit and self.m.kind_of[tok] in ("functions", "globals"):
                            new = self.m.explicit[tok]
                    elif prev in TAG_KEYWORDS:
                        if tok in self.m.tags:
                            new = self.m.tags[tok]
                    elif prev in (".", "->"):
                        new = self.m.mechanical(tok)
                        if new:
                            self.code_renamed[tok] = new
                    elif tok in self.m.explicit:
                        new = self.m.explicit[tok]
                    else:
                        new = self.m.mechanical(tok)
                        if new:
                            self.code_renamed[tok] = new
                if new is not None:
                    pieces.append(s[last:tm.start()])
                    pieces.append(new)
                    last = tm.end()
                    counter["code"] += 1
                prev = tok
            pieces.append(s[last:])
            out.append(("code", "".join(pieces)))
        return out

    def comment(self, s: str, counter: Counter):
        table = dict(self.prose)
        table.update(self.code_renamed)
        return replace_words(s, table, counter, "comment", self.left_in_prose, self.one_word)

    def c_file(self, text: str, counter: Counter, asm: bool = False):
        segs = self.c_code(text, counter, asm)
        return segs

    def finish(self, segs, counter: Counter):
        return "".join(self.comment(s, counter) if k == "comment" else s for k, s in segs)

    def json_text(self, text: str, counter: Counter):
        table = dict(self.prose)
        table.update(self.code_renamed)

        def sub(m):
            body = m.group(1)
            if body in self.m.explicit:
                counter["json"] += 1
                return '"' + self.m.explicit[body] + '"'
            ls = LOCAL_STATIC.match(body)
            if ls and ls.group(1) in self.code_renamed:
                counter["json"] += 1
                return '"' + self.code_renamed[ls.group(1)] + "." + ls.group(2) + '"'
            return '"' + replace_words(body, table, counter, "json") + '"'
        return JSON_STRING.sub(sub, text)

    def python_text(self, text: str, counter: Counter, frozen=frozenset()):
        explicit = {o: n for o, n in self.m.explicit.items() if o not in frozen}

        def sub(m):
            q = m.group(1)
            return q + replace_words(m.group(2), explicit, counter, "python") + q
        return re.sub(r"(\"|')((?:(?!\1)[^\\\n]|\\.)*)\1", sub, text)

    def prose_text(self, text: str, counter: Counter):
        table = dict(self.prose)
        table.update(self.code_renamed)
        return replace_words(text, table, counter, "prose", self.left_in_prose, self.one_word)


# ------------------------------------------------------------------ checks

def collisions(rmap: RenameMap, tree_tokens: dict, renamer: Renamer, sdk: set, lib: set):
    """Problems that make the rename unsafe; empty when it may be applied."""
    out = list(rmap.problems)
    by_new = defaultdict(set)
    for old, new in rmap.explicit.items():
        by_new[new].add(old)
    for old, new in renamer.code_renamed.items():
        by_new[new].add(old)
    for new, olds in by_new.items():
        if len(olds) > 1:
            out.append(f"{new}: several old names {sorted(olds)}")
    old_all = set(rmap.explicit) | set(renamer.code_renamed)
    for old, new in list(rmap.explicit.items()) + list(rmap.tags.items()) + list(renamer.code_renamed.items()):
        if is_placeholder(old):
            out.append(f"{old}: a placeholder name would be renamed (to {new}); placeholders keep their spelling")
        elif is_placeholder(new):
            out.append(f"{new}: reads as a placeholder to tools/rules.py R7 (from {old})")
    for new, olds in by_new.items():
        if new in C_KEYWORDS:
            out.append(f"{new}: a C keyword (from {sorted(olds)})")
        if new in sdk or new in lib:
            out.append(f"{new}: an SDK/library name (from {sorted(olds)})")
        if new in tree_tokens and new not in old_all:
            out.append(f"{new}: already an identifier in {sorted(tree_tokens[new])[:3]} (from {sorted(olds)})")
    tag_new = defaultdict(set)
    for old, new in rmap.tags.items():
        tag_new[new].add(old)
    for new, olds in tag_new.items():
        if len(olds) > 1:
            out.append(f"tag {new}: several old tags {sorted(olds)}")
    return out


def git_dirty(project: Path):
    r = subprocess.run(["git", "status", "--porcelain"], cwd=project, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("rename: git status failed: " + r.stderr.strip())
    return [line for line in r.stdout.splitlines() if line.strip()]


def target_files(project: Path):
    c_files = sorted((project / "src").glob("*.c")) + sorted((project / "include").glob("*.H"))
    asm_files = sorted(p for p in (project / "asm").glob("*") if p.is_file() and p.suffix in (".s", ".h"))
    json_files = sorted(p for p in (project / "config").glob("*.json") if p.name != "style_renames.json")
    py_files = [p for p in [project / "tools" / "test_gate.py"] if p.exists()]
    prose_files = sorted((project / "docs").rglob("*.md")) if (project / "docs").is_dir() else []
    prose_files += [p for p in (project / "README.md", project / "AGENTS.md") if p.exists()]
    return c_files, asm_files, json_files, py_files, prose_files


def run(project: Path, map_path: Path, dry_run: bool, sdk=None, lib=None, check_git=True, out=print):
    data = json.loads(map_path.read_text(encoding="utf-8"))
    sdk = sdk_declared_names() if sdk is None else sdk
    lib = library_names(project) if lib is None else lib
    rmap = RenameMap(data, sdk, lib)
    renamer = Renamer(rmap)
    if check_git and not dry_run:
        dirty = git_dirty(project)
        if dirty:
            out("rename: refusing to run on a dirty tree:\n  " + "\n  ".join(dirty[:20]))
            return 2
    c_files, asm_files, json_files, py_files, prose_files = target_files(project)

    def read(p):
        return p.read_bytes().decode("utf-8", "surrogateescape")

    tree_tokens = defaultdict(set)
    for p in c_files + asm_files:
        for kind, s in segments(read(p)):
            if kind == "code":
                for tok in WORD.findall(s):
                    tree_tokens[tok].add(p.relative_to(project).as_posix())

    results, staged = {}, {}
    c_segs = {}
    for p in c_files:
        cnt = Counter()
        c_segs[p] = (renamer.c_file(read(p), cnt), cnt)
    for p in asm_files:
        cnt = Counter()
        c_segs[p] = (renamer.c_file(read(p), cnt, asm=True), cnt)
    for p, (segs, cnt) in c_segs.items():
        staged[p] = renamer.finish(segs, cnt)
        results[p] = cnt
    for p in json_files:
        cnt = Counter()
        staged[p] = renamer.json_text(read(p), cnt)
        results[p] = cnt
    frozen = set()  # names used by the frozen fixtures under tools/testdata: tests pass them on unchanged
    testdata = project / "tools" / "testdata"
    if testdata.is_dir():
        for q in testdata.rglob("*"):
            if q.is_file() and "rename" not in q.relative_to(testdata).parts[:1]:
                frozen |= set(WORD.findall(read(q)))
    for p in py_files:
        cnt = Counter()
        staged[p] = renamer.python_text(read(p), cnt, frozen)
        results[p] = cnt
    for p in prose_files:
        cnt = Counter()
        staged[p] = renamer.prose_text(read(p), cnt)
        results[p] = cnt

    problems = collisions(rmap, tree_tokens, renamer, sdk, lib)
    total = Counter()
    for p in sorted(results):
        cnt = results[p]
        if sum(cnt.values()):
            out(f"{p.relative_to(project).as_posix():40s} " + " ".join(f"{k}={v}" for k, v in sorted(cnt.items())))
        total.update(cnt)
    out("total " + " ".join(f"{k}={v}" for k, v in sorted(total.items())) +
        f"; explicit entries {len(rmap.explicit)}, tags {len(rmap.tags)}, local-rule names {len(renamer.code_renamed)}")
    if renamer.left_in_prose:
        out(f"one-word names left in comments and docs ({sum(renamer.left_in_prose.values())}): " +
            ", ".join(f"{w} {n}" for w, n in renamer.left_in_prose.most_common()))
    if rmap.ambiguous:
        out("ambiguous (left for manual handling): " + ", ".join(sorted(rmap.ambiguous)))
    if problems:
        out(f"collisions: {len(problems)}")
        for line in problems:
            out("  " + line)
        return 1
    out("collisions: none")
    if dry_run:
        return 0
    for p, text in staged.items():
        if text != read(p):
            p.write_bytes(text.encode("utf-8", "surrogateescape"))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--map", type=Path, default=PROJECT / "config" / "style_renames.json")
    ap.add_argument("--root", type=Path, default=PROJECT, help="project root to rename (default: this checkout)")
    ap.add_argument("--dry-run", action="store_true", help="print counts and collisions; write nothing")
    args = ap.parse_args(argv)
    return run(args.root.resolve(), args.map.resolve(), args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
