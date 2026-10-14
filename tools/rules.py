"""Rule checker for the decompiled C (AGENTS.md, "Project profile"; decomp-code-rules).

Usage:
    python tools/rules.py [--files FILE ...] [--report-only] [--summary-only] [--match-only]
                          [--show-excused] [--no-decimal] [--json PATH]

Scans src/*.c and include/*.H (or the files given to --files) with a plain tokenizer
(stdlib only, no compiler) and prints one line per finding:

    file:line: RULE <id> [match|advisory]: message (function)

Each check carries the decomp-code-rules id it tests and a tier. A [match] finding means the
enclosing function does not count as matched, however exact its bytes; advisory findings are
open work. Checks:

  [match]
  R1   the same type name defined in two files with different bodies
  R2   an extern or prototype in a .c file for a symbol whose owner is elsewhere: another
       registered unit defines it, or a project or SDK header already declares it
  R4   byte-pointer (char/u8) or integer arithmetic re-cast to a pointer or dereferenced:
       (T *)((u8 *)p + n), *((char *)p + n), ((u8 *)p)[n], (T *)((u32)p + n);
       a byte (char/u8/void) pointer variable stepped by the sizeof of a struct member or by an
       offsetof-like expression (p += sizeof(((T *)p)->m), p = p + offsetof(T, m)), any pointer
       stepped by sizeof(((T *)x)->m); a byte pointer variable dereferenced through a cast to
       another type (*(int *)p, *(short *)(p + 2), ((T *)p)->m, ((T *)p)[i])
  R17  volatile on a local, a cast to volatile on ordinary data, or a volatile object that is
       neither in volatile_allow (config/exceptions.json) nor carries a "volatile:" note;
       a local that is never used; an empty if/else body; a bare ';' statement;
       do { } while (0) in a function body; self-assignment; the value-neutral operations
       + 0, - 0, * 1, / 1, |= 0, += 0, -= 0, *= 1
  R18  inline assembly (asm, __asm, __asm__)
  advisory
  R1-local   a struct/union/enum/typedef defined in a .c file
  R1-copy    the same type defined identically in two files
  R2-pending an extern or prototype in a .c file for a symbol that has no matched owner and no
             header declaration yet
  R7         placeholder or address-derived identifier (open naming work)
  R11        raw hex literal; R11-dec meaningful decimal literal (tools/lint_literals.py)
  R14        goto
  R19        type pun *(T *)&x
  R21        non-operational comment (round N, BSim, scores, percentages, "not yet matched")
  R25        extern declaration inside a function body
  R31        a do { } while (0) macro used once in its unit
  R37        #if 0

Allow list:
  /* MATCH-PENDING: <rule>, <reason> */ in or just above a function, or an entry in
      config/match_pending.json, marks the function MATCH-PENDING for that rule: its findings
      of that rule are acknowledged, and it is not matched under the rules.
  /* RULE-EXCEPTION: <rule>, <reason> */ in or above a function (or on the line above a
      file-scope finding), or a config/exceptions.json entry with that rules_id, excuses that
      advisory rule there. An exception never excuses a [match] rule: such a marker or entry is
      reported and the function stays failing (record it as MATCH-PENDING instead).
      The one R17 exception: /* RULE-EXCEPTION: R17-dead-local, <evidence> */ directly above
      an unused local's declaration, or in the function's header, excuses that unused-local
      finding (AGENTS.md, "Dead locals"). The evidence must name the local's type/size
      source (a word: type, size, sizeof, byte, bytes) and the alternatives ruled out
      ("ruled out" / "rules out" / "ruling out"), in at least DEAD_LOCAL_MIN_WORDS words;
      otherwise the marker is an invalid exception. A config/exceptions.json entry with
      rules_id R17-dead-local records it but excuses nothing without the source marker.
      Every other R17 finding stays unexcusable.
      exceptions.json entries with status "match_pending" are MATCH-PENDING records.

File-scope [match] findings (a type, extern or volatile object) block every function that
names the symbol: those in the same .c file, or in any .c file for a header finding. A static
function absent from the symbol map (an inline helper) passes its unexcused [match] findings
on to the functions that call it.

Summary: function definitions, byte-matched functions (every function in the symbol maps of
the units registered in tools/verify.py UNITS; only byte-matched functions live in src/),
functions with unexcused [match] findings, MATCH-PENDING functions, and
matched under the rules = byte-matched - (MATCH-PENDING | unexcused [match]).
Exit status 1 on any unexcused [match] finding, unless --report-only.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass, field
import json
from pathlib import Path
import re
import sys

PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import lint_literals  # noqa: E402

MATCH_RULES = {"R1", "R2", "R4", "R13", "R17", "R18"}
ALL_RULES = MATCH_RULES | {"R1-local", "R1-copy", "R2-pending", "R7", "R11", "R11-dec", "R14", "R19",
                           "R21", "R25", "R31", "R37"}

KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum",
    "extern", "float", "for", "goto", "if", "inline", "__inline__", "int", "long", "register", "return",
    "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void",
    "volatile", "while", "asm", "__asm", "__asm__", "__attribute__",
}
TYPE_KEYWORDS = {"char", "short", "int", "long", "unsigned", "signed", "void", "float", "double", "struct",
                 "union", "enum", "const", "volatile", "static", "register", "extern", "auto", "inline",
                 "__inline__"}
TAG_KEYWORDS = {"struct", "union", "enum"}
BYTE_TYPES = {("char",), ("unsigned", "char"), ("signed", "char"), ("u8",), ("s8",), ("u_char",),
              ("uchar",), ("BYTE",), ("UBYTE",)}
INT_TYPES = {("int",), ("unsigned",), ("unsigned", "int"), ("long",), ("unsigned", "long"), ("u32",), ("s32",),
             ("u_long",), ("u_int",), ("uint",), ("ulong",), ("intptr_t",), ("uintptr_t",)}
BYTE_PTR_BASES = BYTE_TYPES | {("void",)}
DECL_QUALIFIERS = {"const", "volatile", "register", "static", "extern", "auto"}
BUILTIN_TYPEDEFS = {"u_char", "u_short", "u_int", "u_long", "ushort", "uint", "ulong", "size_t"}

PLACEHOLDER = (
    re.compile(r"^[fgs]_[0-9a-f]{5,8}$"),
    re.compile(r"^(local|param|var|field|unk|Unk|FUN|DAT|sub|LAB|PTR|UNK|m_unk|m_Unk)_\w*$"),
    re.compile(r"^(UnkType|unknown)_?\w*$"),
    re.compile(r"^var\d+$"),
    re.compile(r"^[A-Za-z_]*?_?(?=[0-9a-fA-F]*[0-9])[0-9a-fA-F]{6,8}$"),
)
COMMENT_HISTORY = re.compile(
    r"\bround[- ]?\d+\b|\bBSim\b|\b\d+(\.\d+)?\s*%\s*(match|exact|fuzzy|score)|"
    r"\b(match\w*|score\w*|fuzzy)\W{1,3}\d+(\.\d+)?\s*%|\bnot yet matched\b|\bmatch(ed)? (score|percent)|"
    r"\bscore[sd]?\b|\bsession\s*\d+\b|\bworker\s*\d+\b|\b20\d\d-\d\d-\d\d\b|\bfuzzy\b", re.I)
MARKER = re.compile(r"\b(MATCH-PENDING|RULE-EXCEPTION)\s*:\s*([A-Za-z0-9-]+)\s*,?\s*(.*)")
VOLATILE_NOTE = re.compile(r"\bvolatile:\s*\S")
DEAD_LOCAL = "R17-dead-local"
DEAD_LOCAL_MIN_WORDS = 8
DEAD_LOCAL_TYPE_EVIDENCE = re.compile(r"\b(type|types|typed|size|sizes|sized|sizeof|bytes?)\b", re.I)
DEAD_LOCAL_RULED_OUT = re.compile(r"\b(ruled|rules|ruling)\s+out\b\s*:?\s*\w", re.I)


def dead_local_evidence_problem(reason):
    """Why a R17-dead-local reason is not evidence, or None when it names both parts."""
    if len(re.findall(r"\w+", reason)) < DEAD_LOCAL_MIN_WORDS:
        return f"evidence has fewer than {DEAD_LOCAL_MIN_WORDS} words"
    if not DEAD_LOCAL_TYPE_EVIDENCE.search(reason):
        return "evidence names no type/size source"
    if not DEAD_LOCAL_RULED_OUT.search(reason):
        return "evidence names no ruled-out alternatives ('ruled out: ...')"
    return None


# ------------------------------------------------------------------ lexing

@dataclass
class Tok:
    kind: str   # id, num, str, op
    text: str
    line: int


@dataclass
class Source:
    path: Path
    name: str                      # display path, relative to the project when possible
    text: str
    toks: list = field(default_factory=list)
    comments: list = field(default_factory=list)    # (line, end_line, text)
    directives: list = field(default_factory=list)  # (line, end_line, text without comments)


OPS = sorted(["...", "<<=", ">>=", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "+=", "-=",
              "*=", "/=", "%=", "&=", "|=", "^=", "##"], key=len, reverse=True)
NUM = re.compile(r"(0[xX][0-9a-fA-F]+|\d+\.?\d*([eE][-+]?\d+)?|\.\d+([eE][-+]?\d+)?)[uUlLfF]*")
IDENT = re.compile(r"[A-Za-z_$][A-Za-z0-9_$]*")


def lex(path: Path, name: str, text: str) -> Source:
    """Strip comments, strings and directives (keeping line numbers) and tokenize the rest."""
    src = Source(path, name, text)
    out = []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            body = text[i:j]
            src.comments.append((line, line + body.count("\n"), body))
            out.append("".join(ch if ch == "\n" else " " for ch in body))
            line += body.count("\n")
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            src.comments.append((line, line, text[i:j]))
            out.append(" " * (j - i))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            body = text[i:j]
            out.append(c + "".join(ch if ch == "\n" else " " for ch in body[1:-1]) + c if len(body) >= 2 else body)
            line += body.count("\n")
            i = j
        else:
            if c == "\n":
                line += 1
            out.append(c)
            i += 1
    stripped = "".join(out)
    # directives: lines starting with '#', continued by a trailing backslash
    lines = stripped.split("\n")
    k = 0
    while k < len(lines):
        if lines[k].lstrip().startswith("#"):
            start = k
            parts = [lines[k]]
            while lines[k].rstrip().endswith("\\") and k + 1 < len(lines):
                k += 1
                parts.append(lines[k])
            src.directives.append((start + 1, k + 1, "\n".join(parts)))
            for m in range(start, k + 1):
                lines[m] = ""
        k += 1
    for lineno, row in enumerate(lines, 1):
        p = 0
        while p < len(row):
            ch = row[p]
            if ch.isspace() or ch == "\\":
                p += 1
                continue
            m = IDENT.match(row, p)
            if m:
                src.toks.append(Tok("id", m.group(), lineno))
                p = m.end()
                continue
            m = NUM.match(row, p)
            if m and (ch.isdigit() or (ch == "." and p + 1 < len(row) and row[p + 1].isdigit())):
                src.toks.append(Tok("num", m.group(), lineno))
                p = m.end()
                continue
            if ch in "\"'":
                q = row.find(ch, p + 1)
                q = len(row) if q < 0 else q + 1
                src.toks.append(Tok("str", row[p:q], lineno))
                p = q
                continue
            for op in OPS:
                if row.startswith(op, p):
                    src.toks.append(Tok("op", op, lineno))
                    p += len(op)
                    break
            else:
                src.toks.append(Tok("op", ch, lineno))
                p += 1
    return src


def num_value(text: str):
    t = text.rstrip("uUlL")
    try:
        if re.fullmatch(r"0[0-7]+", t):
            return int(t, 8)
        return int(t, 0)
    except ValueError:
        return None


def match_close(toks, i):
    """Index of the bracket closing toks[i] ('(', '[' or '{')."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    open_, close = toks[i].text, pairs[toks[i].text]
    depth = 0
    for j in range(i, len(toks)):
        if toks[j].text == open_:
            depth += 1
        elif toks[j].text == close:
            depth -= 1
            if depth == 0:
                return j
    return len(toks) - 1


# ------------------------------------------------------------------ structure

@dataclass
class Function:
    name: str
    src: Source
    first: int        # token index of the first token of the definition
    open: int         # token index of the body '{'
    close: int        # token index of the body '}'
    static: bool
    line: int = 0
    end_line: int = 0
    decl_line: int = 0  # first line after the previous top-level item (header comments belong here)


@dataclass
class Decl:
    kind: str          # typedef, extern, prototype, static_prototype, data, tagdef
    names: list        # declared names
    toks: list
    line: int
    static: bool = False


def split_declarators(toks):
    """Names declared by one declaration (tokens without the final ';'), and whether each is a function."""
    flat, i = [], 0
    while i < len(toks):
        if toks[i].text == "{":
            j = match_close(toks, i)
            flat.append(Tok("op", "{}", toks[i].line))
            i = j + 1
        else:
            flat.append(toks[i])
            i += 1
    parts, cur, depth = [], [], 0
    for t in flat:
        if t.text in "([":
            depth += 1
        elif t.text in ")]":
            depth -= 1
        if t.text == "," and depth == 0:
            parts.append(cur)
            cur = []
        else:
            cur.append(t)
    parts.append(cur)
    out = []
    for part in parts:
        depth, cut = 0, len(part)
        for k, t in enumerate(part):
            if t.text in "([":
                depth += 1
            elif t.text in ")]":
                depth -= 1
            elif t.text == "=" and depth == 0:
                cut = k
                break
        part = part[:cut]
        name, is_func = None, False
        for k, t in enumerate(part):
            if t.text == "(" and k + 1 < len(part) and part[k + 1].text == "*":
                inner = [x for x in part[k + 1:] if x.kind == "id" and x.text not in KEYWORDS]
                name = inner[0].text if inner else None
                break
            if t.text in "([":
                is_func = t.text == "("
                break
            if t.kind == "id" and t.text not in KEYWORDS and t.text != "__attribute__":
                if k > 0 and part[k - 1].text in TAG_KEYWORDS:
                    continue  # a tag, not a declarator
                name = t.text
        if name:
            out.append((name, is_func))
    return out


def tag_definitions(toks):
    """(tag, keyword, body tokens) for every struct/union/enum TAG { ... } in toks."""
    out = []
    for i in range(len(toks) - 2):
        if toks[i].text in TAG_KEYWORDS and toks[i + 1].kind == "id" and toks[i + 2].text == "{":
            j = match_close(toks, i + 2)
            out.append((toks[i + 1].text, toks[i].text, toks[i + 2:j + 1]))
    return out


def parse_top(src: Source):
    """Split a file into function definitions and file-scope declarations."""
    toks, funcs, decls = src.toks, [], []
    i, start, prev_end_line, knr_close = 0, 0, 0, None
    while i < len(toks):
        t = toks[i]
        if t.text in "([":
            j = match_close(toks, i)
            # K&R definition: name(a, b) followed by parameter declarations, then the body
            if (t.text == "(" and i > start and toks[i - 1].kind == "id" and toks[i - 1].text not in KEYWORDS
                    and j + 1 < len(toks) and toks[j + 1].kind == "id" and toks[j + 1].text != "__attribute__"
                    and "=" not in [x.text for x in toks[start:i]] and "typedef" not in [x.text for x in toks[start:i]]):
                knr_close = j
            i = j + 1
            continue
        if t.text == "{" and i >= 2 and toks[i - 1].kind == "str" and toks[i - 2].text == "extern":
            i = start = i + 1  # extern "C" { ... }: transparent
            continue
        if t.text == "}":
            i = start = i + 1  # the end of an extern "C" block
            continue
        if t.text == "{":
            seq = toks[start:i]
            if knr_close is not None:
                seq = toks[start:knr_close + 1]
            knr_close = None
            is_func = (seq and seq[-1].text == ")" and "=" not in [x.text for x in seq]
                       and not (len(seq) >= 2 and seq[-2].text in TAG_KEYWORDS))
            j = match_close(toks, i)
            if is_func:
                # parameter list: the '(' matching the final ')'
                depth = 0
                for k in range(len(seq) - 1, -1, -1):
                    if seq[k].text == ")":
                        depth += 1
                    elif seq[k].text == "(":
                        depth -= 1
                        if depth == 0:
                            break
                name = next((x.text for x in reversed(seq[:k]) if x.kind == "id"), "?")
                f = Function(name, src, start, i, j, any(x.text == "static" for x in seq))
                f.line, f.end_line, f.decl_line = toks[start].line, toks[j].line, prev_end_line + 1
                funcs.append(f)
                prev_end_line = toks[j].line
                i = start = j + 1
                continue
            i = j + 1
            continue
        if t.text == ";" and knr_close is None:
            seq = toks[start:i]
            if seq:
                decls.append(classify_decl(seq))
            prev_end_line = t.line
            start = i + 1
        i += 1
    return funcs, decls


def classify_decl(seq):
    texts = [x.text for x in seq]
    static = "static" in texts
    names = split_declarators(seq)
    if "typedef" in texts:
        return Decl("typedef", [n for n, _ in names], seq, seq[0].line, static)
    if "extern" in texts:
        return Decl("extern", [n for n, _ in names], seq, seq[0].line, static)
    if names and all(f for _, f in names):
        return Decl("static_prototype" if static else "prototype", [n for n, _ in names], seq, seq[0].line, static)
    if not names:
        return Decl("tagdef", [], seq, seq[0].line, static)
    return Decl("data", [n for n, f in names if not f], seq, seq[0].line, static)


# ------------------------------------------------------------------ findings

@dataclass
class Finding:
    file: str
    line: int
    rule: str
    message: str
    function: str | None = None    # enclosing function name
    name: str | None = None        # symbol a file-scope [match] finding is about
    state: str = "open"            # open, excused, pending, invalid-exception
    kind: str | None = None        # "dead-local" for R17's unused-local finding
    files: set = field(default_factory=set)    # files involved in a file-scope finding (R1)
    users: set = field(default_factory=set)    # (file, function) keys a file-scope finding blocks

    @property
    def tier(self):
        return "match" if self.rule in MATCH_RULES else "advisory"

    def format(self):
        where = f" ({self.function})" if self.function else ""
        extra = "" if self.state == "open" else f" [{self.state}]"
        return f"{self.file}:{self.line}: RULE {self.rule} [{self.tier}]: {self.message}{where}{extra}"


class Checker:
    def __init__(self, files, units, include_dir=PROJECT / "include", sdk_dir=None,
                 exceptions=None, match_pending=None, decimal=True):
        self.units = units                    # [(unit, src path, map path)]
        self.unit_of = {Path(s).resolve(): u for u, s, _ in units}
        self.maps = {}
        for u, _, m in units:
            try:
                self.maps[u] = json.loads(Path(m).read_text(encoding="utf-8")).get("functions", {})
            except (OSError, ValueError):
                self.maps[u] = {}
        self.decimal = decimal
        self.findings = []
        self.sources = [self._load(Path(p)) for p in files]
        # every registered unit and project header is indexed, even when --files narrows the scan
        index_paths = {Path(s).resolve() for _, s, _ in units} | {p.resolve() for p in sorted(Path(include_dir).glob("*.H"))}
        scanned = {s.path.resolve() for s in self.sources}
        self.index_sources = self.sources + [self._load(p) for p in sorted(index_paths - scanned) if p.exists()]
        self.parsed = {s.path.resolve(): parse_top(s) for s in self.index_sources}
        self.sdk_names, self.sdk_types = self._sdk_index(sdk_dir)
        self.exceptions = exceptions or {}
        self.pending_records = match_pending or []
        self._build_index()

    def _load(self, path):
        try:
            name = path.resolve().relative_to(PROJECT).as_posix()
        except ValueError:
            name = path.as_posix()
        return lex(path, name, path.read_text(encoding="utf-8", errors="replace"))

    def _sdk_index(self, sdk_dir):
        names, types = {}, set()
        if not sdk_dir or not Path(sdk_dir).is_dir():
            return names, types
        for p in sorted(Path(sdk_dir).rglob("*")):
            if p.suffix.upper() != ".H":
                continue
            src = lex(p, p.name, p.read_text(encoding="latin-1"))
            _, decls = parse_top(src)
            for d in decls:
                if d.kind == "typedef":
                    types.update(d.names)
                elif d.kind in ("extern", "prototype", "data"):
                    for n in d.names:
                        names.setdefault(n, p.name)
        return names, types

    def _build_index(self):
        self.owner = {}        # symbol -> unit (or file) defining it
        self.header_decl = {}  # symbol -> header declaring it
        self.typedef_names = set(BUILTIN_TYPEDEFS) | self.sdk_types
        self.type_defs = defaultdict(list)   # (namespace, name) -> [(file, line, body key)]
        for path, (funcs, decls) in self.parsed.items():
            src_name = next(s.name for s in self.index_sources if s.path.resolve() == path)
            is_header = path.suffix.upper() == ".H"
            owner = self.unit_of.get(path, src_name)
            for f in funcs:
                if not is_header:
                    self.owner.setdefault(f.name, owner)
            for d in decls:
                if d.kind == "typedef":
                    self.typedef_names.update(d.names)
                    for n in d.names:
                        self.type_defs[("typedef", n)].append((src_name, d.line, self._key(d.toks)))
                for tag, kw, body in tag_definitions(d.toks):
                    self.type_defs[("tag", tag)].append((src_name, body[0].line, kw + self._key(body)))
                if d.kind == "data" and not is_header:
                    for n in d.names:
                        self.owner.setdefault(n, owner)
                if is_header and d.kind in ("extern", "prototype"):
                    for n in d.names:
                        self.header_decl.setdefault(n, src_name)

    @staticmethod
    def _key(toks):
        key = " ".join(t.text for t in toks)
        for spelled, canonical in (("signed short int", "short"), ("short int", "short"), ("signed short", "short"),
                                   ("signed int", "int"), ("signed long", "long"), ("long int", "long"),
                                   ("unsigned int", "unsigned"), ("signed char", "signed_char")):
            key = re.sub(r"\b" + spelled + r"\b", canonical, key)
        return key

    # -------------------------------------------------------------- run
    def run(self):
        for src in self.sources:
            funcs, decls = self.parsed[src.path.resolve()]
            self._check_file(src, funcs, decls)
        self._check_duplicate_types()
        self._apply_allow_list()
        self._resolve_file_scope()
        return self

    def add(self, src, line, rule, message, function=None, name=None, kind=None):
        self.findings.append(Finding(src.name, line, rule, message, function, name, kind=kind))

    def _func_at(self, src, line):
        for f in self.parsed[src.path.resolve()][0]:
            if f.line <= line <= f.end_line:
                return f.name
        return None

    def _check_file(self, src, funcs, decls):
        is_c = src.path.suffix.lower() == ".c"
        unit = self.unit_of.get(src.path.resolve())
        local_names = {f.name for f in funcs}
        for d in decls:
            if d.kind == "data":
                local_names.update(d.names)
        for d in decls:
            self._check_volatile_decl(src, d, None)
            if not is_c:
                continue
            if d.kind == "typedef" or tag_definitions(d.toks):
                names = d.names or [t for t, _, _ in tag_definitions(d.toks)]
                self.add(src, d.line, "R1-local", f"type {', '.join(names)} defined in a .c file; "
                         "move it to its owner header")
            if d.kind in ("extern", "prototype", "static_prototype"):
                for n in d.names:
                    self._check_redeclaration(src, d.line, n, local_names, unit, None)
        self.file_vars = defaultdict(list)
        for d in decls:
            if d.kind == "data":
                for n, v in self._declared(d.toks).items():
                    self.file_vars[n].append(v)
        for f in funcs:
            self._check_function(src, f, local_names, unit)
        for line, end, text in src.comments:
            if COMMENT_HISTORY.search(text) and not MARKER.search(text):
                self.add(src, line, "R21", "non-operational comment (history, score or campaign note): "
                         + COMMENT_HISTORY.search(text).group(0), self._func_at(src, line))
        self._check_directives(src)
        self._check_names_file_scope(src, decls)
        for line, col, lit in lint_literals.scan(src.text):
            self.add(src, line, "R11", f"raw hex literal {lit}", self._func_at(src, line))
        if self.decimal:
            for line, col, lit in lint_literals.scan(src.text, decimal=True):
                self.add(src, line, "R11-dec", f"meaningful decimal literal {lit}", self._func_at(src, line))

    def _check_redeclaration(self, src, line, name, local_names, unit, function):
        if name in local_names:
            return  # forward declaration of the unit's own function or datum
        owner = self.owner.get(name)
        if owner is not None:
            self.add(src, line, "R2", f"'{name}' is declared here but owned by {owner}; declare it in "
                     "the owner's header", function, name)
        elif name in self.header_decl:
            self.add(src, line, "R2", f"'{name}' is already declared in {self.header_decl[name]}; include it",
                     function, name)
        elif name in self.sdk_names:
            self.add(src, line, "R2", f"'{name}' is declared by the SDK header {self.sdk_names[name]}; "
                     "include it", function, name)
        else:
            self.add(src, line, "R2-pending", f"'{name}' has no matched owner or header yet; move the "
                     "declaration to its future owner's header", function, name)

    def _has_volatile_note(self, src, line):
        return any(VOLATILE_NOTE.search(text) for start, end, text in src.comments
                   if start <= line <= end + 1 and end >= line - 1)

    def _volatile_allowed(self, src, name):
        for entry in self.exceptions.get("volatile_allow", []):
            where = Path(entry.get("file", "")).as_posix()
            if entry.get("name") == name and (src.name == where or src.name.endswith("/" + where)):
                return True
        return False

    def _check_volatile_decl(self, src, d, function):
        if not any(t.text == "volatile" for t in d.toks):
            return
        for tag, _, body in tag_definitions(d.toks):
            # volatile members inside a type body
            for k, t in enumerate(body):
                if t.text == "volatile":
                    member = next((x.text for x in body[k:] if x.kind == "id" and x.text not in KEYWORDS
                                   and x.text not in self.typedef_names), "?")
                    if not self._has_volatile_note(src, t.line) and not self._volatile_allowed(src, member):
                        self.add(src, t.line, "R17", f"volatile member '{member}' of {tag} without a "
                                 "'volatile: <evidence>' note or volatile_allow entry", function, tag)
        spec, depth = [], 0
        for t in d.toks:
            if t.text in "([{":
                depth += 1
            elif t.text in ")]}":
                depth -= 1
            elif t.text == "=" and depth == 0:
                break
            spec.append(t)
        outer = [t for t in spec if t.text == "volatile"]
        if outer and d.kind != "typedef" and not tag_definitions(d.toks):
            for n in d.names:
                if not self._has_volatile_note(src, d.line) and not self._volatile_allowed(src, n):
                    what = "local" if function else "object"
                    self.add(src, d.line, "R17", f"volatile {what} '{n}' with no hardware/IRQ evidence "
                             "(volatile_allow or 'volatile:' note)", function, None if function else n)

    def _check_directives(self, src):
        macros = {}
        for line, end, text in src.directives:
            body = re.sub(r"\\\n", " ", text).strip()
            if re.match(r"#\s*if\s+0\b", body):
                self.add(src, line, "R37", "#if 0 block: code no build compiles", self._func_at(src, line))
            m = re.match(r"#\s*define\s+(\w+)\(", body)
            if m and re.search(r"\bwhile\s*\(\s*0\s*\)\s*$", body):
                macros[m.group(1)] = line
        if src.path.suffix.lower() == ".c":
            for name, line in macros.items():
                uses = [t for t in src.toks if t.kind == "id" and t.text == name]
                if len(uses) == 1:
                    self.add(src, line, "R31", f"do {{ }} while (0) macro {name} is used once; restore the "
                             "original statement or helper", self._func_at(src, uses[0].line))

    def _placeholder(self, ident):
        return any(p.match(ident) for p in PLACEHOLDER)

    def _check_names_file_scope(self, src, decls):
        seen = set()
        for d in decls:
            for t in d.toks:
                if t.kind == "id" and t.text not in seen and self._placeholder(t.text):
                    seen.add(t.text)
                    self.add(src, t.line, "R7", f"placeholder or address-derived name '{t.text}'")

    # -------------------------------------------------------------- functions
    def _check_function(self, src, f, file_names, unit):
        toks = src.toks
        body = toks[f.open:f.close + 1]
        name = f.name
        # names (R7): function name, parameters, body identifiers
        seen = set()
        for t in toks[f.first:f.close + 1]:
            if t.kind == "id" and t.text not in seen and self._placeholder(t.text):
                seen.add(t.text)
                self.add(src, t.line, "R7", f"placeholder or address-derived name '{t.text}'", name)
        locals_ = self._locals(src, f)
        for decl, scope_open in locals_:
            self._check_volatile_decl(src, decl, name)
            if decl.kind == "extern" or (decl.kind in ("prototype", "static_prototype")):
                self.add(src, decl.line, "R25", f"extern declaration of {', '.join(decl.names)} inside a "
                         "function body", name)
                for n in decl.names:
                    self._check_redeclaration(src, decl.line, n, file_names, unit, name)
                continue
            if decl.kind == "data":
                for n in decl.names:
                    uses = sum(1 for t in body if t.kind == "id" and t.text == n)
                    if uses <= 1:
                        self.add(src, decl.line, "R17", f"local '{n}' is never used (dummy local)", name,
                                 kind="dead-local")
        self._check_body(src, f, body)
        self._check_byte_cursor(src, f, locals_)

    def _is_decl_start(self, toks, i):
        t = toks[i]
        if t.kind != "id":
            return False
        if t.text in TYPE_KEYWORDS or t.text in self.typedef_names:
            return t.text not in ("sizeof",)
        if t.text in KEYWORDS:
            return False
        nxt = toks[i + 1] if i + 1 < len(toks) else None
        if nxt is None:
            return False
        if nxt.kind == "id" and nxt.text not in KEYWORDS:
            return True
        if nxt.text == "*":
            k = i + 1
            while k < len(toks) and toks[k].text == "*":
                k += 1
            return (k + 1 < len(toks) and toks[k].kind == "id" and toks[k].text not in KEYWORDS
                    and toks[k + 1].text in (";", ",", "=", "[", ")"))
        return False

    def _locals(self, src, f):
        """Declarations at the top of every block of a function body: [(Decl, block open index)]."""
        toks, out = src.toks, []
        kinds = self._brace_kinds(toks, f.open, f.close)
        i, at_top, block = f.open + 1, True, f.open
        while i < f.close:
            t = toks[i]
            if t.text == "{" and kinds.get(i) == "block":
                at_top, block = True, i
                i += 1
                continue
            if at_top and self._is_decl_start(toks, i):
                j = i
                while j < f.close and toks[j].text != ";":
                    j = match_close(toks, j) + 1 if toks[j].text in "([{" else j + 1
                out.append((classify_decl(toks[i:j]), block))
                i = j + 1
                continue
            at_top = False
            i += 1
        return out

    @staticmethod
    def _brace_kinds(toks, start, end):
        kinds, stack = {}, []
        for i in range(start, end + 1):
            t = toks[i]
            if t.text == "{":
                prev = toks[i - 1].text if i > 0 else ""
                prev2 = toks[i - 2].text if i > 1 else ""
                if prev == "=" or (stack and stack[-1][1] == "init" and prev in (",", "{")):
                    kind = "init"
                elif prev in TAG_KEYWORDS or prev2 in TAG_KEYWORDS:
                    kind = "type"
                else:
                    kind = "block"
                kinds[i] = kind
                stack.append((i, kind))
            elif t.text == "}" and stack:
                kinds[i] = stack.pop()[1]
        return kinds

    def _check_body(self, src, f, body):
        toks, name = src.toks, f.name
        kinds = self._brace_kinds(toks, f.open, f.close)
        paren_depth = 0
        for i in range(f.open + 1, f.close):
            t = toks[i]
            prev = toks[i - 1]
            nxt = toks[i + 1]
            if t.text == "(":
                paren_depth += 1
            elif t.text == ")":
                paren_depth -= 1
            if t.kind == "id" and t.text in ("asm", "__asm", "__asm__"):
                self.add(src, t.line, "R18", "inline assembly in a C function", name)
            elif t.text == "goto":
                self.add(src, t.line, "R14", f"goto {nxt.text}", name)
            elif t.text == "volatile" and prev.text == "(":
                close = match_close(toks, i - 1)
                after = toks[close + 1] if close + 1 < len(toks) else None
                hardware = after is not None and (after.kind == "num" or re.fullmatch(r"[A-Z0-9_]+", after.text))
                if not hardware:
                    self.add(src, t.line, "R17", "cast to volatile on ordinary data (volatile lever)", name)
            elif t.text == "if" and nxt.text == "(":
                close = match_close(toks, i + 1)
                after = toks[close + 1]
                if after.text == ";" or (after.text == "{" and toks[close + 2].text == "}"):
                    self.add(src, t.line, "R17", "empty if body", name)
            elif t.text == "else":
                if nxt.text == ";" or (nxt.text == "{" and toks[i + 2].text == "}"):
                    self.add(src, t.line, "R17", "empty else body", name)
            elif t.text == ";" and paren_depth == 0:
                if (prev.text == "{" and kinds.get(i - 1) == "block") or \
                        (prev.text == "}" and kinds.get(i - 1) == "block") or prev.text == ";":
                    self.add(src, t.line, "R17", "bare ';' statement", name)
            elif t.text == "while" and prev.text == "}" and toks[i + 1].text == "(" and toks[i + 2].text == "0" \
                    and toks[i + 3].text == ")":
                self.add(src, t.line, "R17", "do { } while (0) in a function body (a macro's expansion?)", name)
            elif t.text == "=":
                self._check_self_assign(src, i, name)
            if t.kind == "num":
                v = num_value(t.text)
                if v == 0 and prev.text in ("|=", "+=", "-=", "<<=", ">>=", "^="):
                    self.add(src, t.line, "R17", f"value-neutral '{prev.text} 0'", name)
                elif v == 0 and prev.text in ("+", "-") and operand_before(toks, i - 1):
                    self.add(src, t.line, "R17", f"value-neutral '{prev.text} 0'", name)
                elif v == 1 and prev.text in ("*", "/") and operand_before(toks, i - 1):
                    self.add(src, t.line, "R17", f"value-neutral '{prev.text} 1'", name)
                elif v == 1 and prev.text == "*=":
                    self.add(src, t.line, "R17", "value-neutral '*= 1'", name)
            if t.text == "(" and self._cast_type(toks, i) is not None:
                self._check_offset_cast(src, i, name)
            if t.text == "*" and nxt.text == "(" and self._cast_type(toks, i + 1) is not None:
                close = match_close(toks, i + 1)
                if toks[close - 1].text == "*" and toks[close + 1].text == "&" and not operand_before(toks, i):
                    self.add(src, t.line, "R19", "type pun *(T *)&x; a named member or helper reads better", name)

    def _cast_type(self, toks, i):
        """Tokens of a cast's type name if toks[i] opens a cast like (T *) or (unsigned int), else None."""
        j = match_close(toks, i)
        inner = toks[i + 1:j]
        if not inner or j + 1 >= len(toks):
            return None
        words = [t.text for t in inner]
        if any(w not in ("*", "const", "volatile") and not (re.fullmatch(r"\w+", w)) for w in words):
            return None
        base = [w for w in words if w not in ("*", "const", "volatile")]
        if not base:
            return None
        known = all(w in TYPE_KEYWORDS or w in self.typedef_names for w in base) or \
            (base[0] in TAG_KEYWORDS and len(base) == 2)
        if not known:
            return None
        after = toks[j + 1]
        if after.text in (")", ";", ",", "]"):
            return None
        return words

    def _check_offset_cast(self, src, i, name):
        toks = src.toks
        words = self._cast_type(toks, i)
        base = tuple(w for w in words if w not in ("const", "volatile", "*"))
        stars = words.count("*")
        byte_ptr = stars == 1 and base in BYTE_TYPES
        int_cast = stars == 0 and base in INT_TYPES
        if not (byte_ptr or int_cast):
            return
        # the innermost '(' enclosing the cast
        depth, parent = 0, None
        for k in range(i - 1, -1, -1):
            if toks[k].text in (")", "]"):
                depth += 1
            elif toks[k].text in ("(", "["):
                if depth == 0:
                    parent = k
                    break
                depth -= 1
        if parent is None or toks[parent].text != "(":
            return
        pclose = match_close(toks, parent)
        cast_end = match_close(toks, i)
        arith, d = False, 0
        for k in range(cast_end + 1, pclose):
            if toks[k].text in "([":
                d += 1
            elif toks[k].text in ")]":
                d -= 1
            elif d == 0 and toks[k].text in ("+", "-") and operand_before(toks, k):
                arith = True
        before = toks[parent - 1] if parent > 0 else None
        outer_ptr_cast = (before is not None and before.text == ")" and toks[parent - 2].text == "*")
        deref = before is not None and before.text == "*" and not operand_before(toks, parent - 1)
        indexed = pclose + 1 < len(toks) and toks[pclose + 1].text == "[" and cast_end + 1 < pclose
        if byte_ptr and ((arith and (outer_ptr_cast or deref)) or (indexed and not arith and
                                                                  toks[parent + 1] is toks[i])):
            self.add(src, toks[i].line, "R4", "byte-offset access through a char/u8 pointer cast; use a typed "
                     "member", name)
        elif int_cast and arith and outer_ptr_cast and constant_term(toks, cast_end + 1, pclose):
            self.add(src, toks[i].line, "R4", "pointer arithmetic through an integer cast; use a typed member",
                     name)

    def _declared(self, toks):
        """{name: (base words, stars)} for the scalar declarators of one declaration or parameter."""
        out, base = {}, ()
        for n, part in enumerate(self._split_commas(toks)):
            for k, t in enumerate(part):
                if t.text == "=":
                    part = part[:k]
                    break
            if not part or part[0].text == "typedef":
                return {}
            texts = [t.text for t in part]
            head = 0
            if n == 0:
                ids = [k for k, t in enumerate(part) if t.kind == "id"]
                head = texts.index("*") if "*" in texts else (ids[-1] if ids else len(part))
                base = tuple(w for w in texts[:head] if w not in DECL_QUALIFIERS)
            rest = part[head:]
            name = next((t for t in rest if t.kind == "id" and t.text not in KEYWORDS), None)
            if name is None:
                continue
            k = rest.index(name)
            if k + 1 < len(rest) and rest[k + 1].text in ("[", "("):
                continue    # an array or a function (pointer): not a scalar pointer
            out[name.text] = (base, sum(1 for t in rest[:k] if t.text == "*"))
        return out

    @staticmethod
    def _split_commas(toks):
        parts, cur, depth = [], [], 0
        for t in toks:
            if t.text in "([":
                depth += 1
            elif t.text in ")]":
                depth -= 1
            if t.text == "," and depth == 0:
                parts.append(cur)
                cur = []
            else:
                cur.append(t)
        parts.append(cur)
        return parts

    def _pointer_names(self, src, f, locals_):
        """(byte pointer names, pointer names) visible in function f: parameters, locals, file scope."""
        header = src.toks[f.first:f.open]
        env = defaultdict(list)
        for n, v in self.file_vars.items():
            env[n].extend(v)
        ni = next((k for k, t in enumerate(header) if t.text == f.name and k + 1 < len(header)
                   and header[k + 1].text == "("), None)
        if ni is not None:
            close = match_close(header, ni + 1)
            for p in self._split_commas(header[ni + 2:close]):
                for n, v in self._declared(p).items():
                    env[n].append(v)
            knr, cur = [], []
            for t in header[close + 1:]:
                if t.text == ";":
                    knr.append(cur)
                    cur = []
                else:
                    cur.append(t)
            for p in knr:
                for n, v in self._declared(p).items():
                    env[n].append(v)
        for decl, _ in locals_:
            if decl.kind == "data":
                for n, v in self._declared(decl.toks).items():
                    env[n].append(v)
        byte = {n for n, vs in env.items() if all(b in BYTE_PTR_BASES and s == 1 for b, s in vs)}
        anyptr = {n for n, vs in env.items() if all(s >= 1 for b, s in vs)}
        return byte, anyptr, self._file_cursors(src, f, byte)

    @staticmethod
    def _file_cursors(src, f, byte):
        """Byte pointers that walk a loaded file's untyped buffer: assigned from '<file>.buffer' or
        '<file>->buffer', or from an expression over such a cursor (q = p + 2)."""
        toks = src.toks
        assigns = []
        for k in range(f.open + 1, f.close - 1):
            if toks[k].kind == "id" and toks[k].text in byte and toks[k + 1].text == "=":
                e = k + 2
                while e < f.close and toks[e].text != ";":
                    e = match_close(toks, e) + 1 if toks[e].text in "([" else e + 1
                assigns.append((toks[k].text, toks[k + 2:e]))
        cursors = {n for n, rhs in assigns
                   if len(rhs) >= 2 and rhs[-1].text == "buffer" and rhs[-2].text in (".", "->")}
        grown = True
        while grown:
            grown = False
            for n, rhs in assigns:
                if n not in cursors and any(t.text in cursors for t in rhs):
                    cursors.add(n)
                    grown = True
        return cursors

    def _check_byte_cursor(self, src, f, locals_):
        """R4 beyond the constant-offset cast: a byte pointer stepped by a member's sizeof or an
        offsetof, and a byte pointer variable dereferenced through a cast to another type."""
        toks, name = src.toks, f.name
        byte, anyptr, cursors = self._pointer_names(src, f, locals_)
        if not anyptr:
            return
        seen = set()

        def hit(line, message):
            if (line, message) not in seen:
                seen.add((line, message))
                self.add(src, line, "R4", message, name)

        unevaluated = 0     # index of the last token of the sizeof/offsetof operand being scanned
        for i in range(f.open + 1, f.close):
            t = toks[i]
            if t.text in ("sizeof", "offsetof") and i + 1 < f.close and toks[i + 1].text == "(":
                unevaluated = match_close(toks, i + 1)
                inner = toks[i + 2:unevaluated]
                texts = [x.text for x in inner]
                if t.text == "sizeof" and "->" not in texts and "." not in texts:
                    continue
                cast_member = t.text == "offsetof" or any(
                    texts[k] == "*" and texts[k + 1] == ")" for k in range(len(texts) - 1))
                self._check_member_step(src, f, i, byte, anyptr, cast_member, hit)
            elif (t.text == "*" and i + 4 < f.close and toks[i + 1].text == ")" and toks[i + 2].text == "0"
                  and toks[i + 3].text == ")" and toks[i + 4].text == "->"):
                self._check_member_step(src, f, i, byte, anyptr, True, hit)     # &((T *)0)->m
            elif t.text == "(" and i > unevaluated:
                self._check_cast_deref(src, f, i, byte - cursors, hit)

    def _check_member_step(self, src, f, i, byte, anyptr, cast_member, hit):
        """toks[i] is a member sizeof or an offsetof: is the statement stepping a pointer by it?"""
        toks = src.toks
        s, depth = i - 1, 0
        while s > f.open:
            x = toks[s].text
            if x == ")":
                depth += 1
            elif x == "(":
                depth -= 1
            elif x in (";", "{", "}") and depth <= 0:
                break
            s -= 1
        s += 1
        first = toks[s]
        step = None
        if first.kind == "id" and first.text in anyptr and toks[s + 1].text in ("+=", "-="):
            step = first.text
        elif (first.kind == "id" and first.text in anyptr and toks[s + 1].text == "="
              and toks[s + 2].text == first.text and toks[s + 3].text in ("+", "-")):
            step = first.text
        if step is not None:
            if step in byte or cast_member:
                hit(toks[i].line, f"pointer '{step}' stepped by a member's size or an offset "
                    "(raw cursor); use a typed member or a typed pointer")
            return
        for k in range(s, i):
            if toks[k].kind == "id" and toks[k].text in byte and toks[k + 1].text in ("+", "-") \
                    and operand_before(toks, k + 1):
                hit(toks[i].line, f"byte pointer '{toks[k].text}' offset by a member's size or an offset; "
                    "use a typed member")
                return
            words = self._cast_type(toks, k) if toks[k].text == "(" else None
            if words is not None:
                base = tuple(w for w in words if w not in ("const", "volatile", "*"))
                if words.count("*") == 1 and base in BYTE_PTR_BASES:
                    d = 0
                    for m in range(match_close(toks, k) + 1, i):
                        if toks[m].text in "([":
                            d += 1
                        elif toks[m].text in ")]":
                            d -= 1
                            if d < 0:
                                break
                        elif d == 0 and toks[m].text in ("+", "-") and operand_before(toks, m):
                            hit(toks[i].line, "byte-pointer cast offset by a member's size or an offset; "
                                "use a typed member")
                            return

    def _check_cast_deref(self, src, f, i, byte, hit):
        """toks[i] opens a cast: is a byte pointer variable dereferenced through it?"""
        toks = src.toks
        words = self._cast_type(toks, i)
        if words is None or not byte:
            return
        base = tuple(w for w in words if w not in ("const", "volatile", "*"))
        stars = words.count("*")
        if stars == 0 or (stars == 1 and base in BYTE_PTR_BASES):
            return
        j = match_close(toks, i)
        op, end, var = toks[j + 1], None, None
        if op.kind == "id" and op.text in byte and toks[j + 2].text not in ("[", ".", "->", "(", "++", "--"):
            end, var = j + 1, op.text
        elif op.text == "(":
            g = match_close(toks, j + 1)
            inner = toks[j + 2:g]
            if inner and inner[0].kind == "id" and inner[0].text in byte and \
                    (len(inner) == 1 or inner[1].text in ("+", "-")):
                end, var = g, inner[0].text
        if end is None:
            return
        before = toks[i - 1]
        deref = before.text == "*" and not operand_before(toks, i - 1)
        member = (before.text == "(" and match_close(toks, i - 1) == end + 1
                  and toks[end + 2].text in ("->", "["))
        if deref and self._word_copy_statement(toks, f, i - 1):
            return
        if deref or member:
            hit(toks[i].line, f"byte pointer '{var}' dereferenced through a cast to another type; use a "
                "typed pointer or member")

    @staticmethod
    def _word_copy_statement(toks, f, star):
        """True when the '*' at toks[star] is the source side of a '*(T *)&x = *(T *)p;' word copy."""
        s, depth = star - 1, 0
        while s > f.open:
            x = toks[s].text
            if x == ")":
                depth += 1
            elif x == "(":
                depth -= 1
            elif x in (";", "{", "}") and depth <= 0:
                break
            s -= 1
        s += 1
        if toks[s].text != "*" or toks[s + 1].text != "(":
            return False
        close = match_close(toks, s + 1)
        return toks[close + 1].text == "&" and any(t.text == "=" for t in toks[close:star])

    def _check_self_assign(self, src, i, name):
        toks = src.toks
        lhs, k = [], i - 1
        while k >= 0:
            t = toks[k]
            if t.text == "]":
                o = k
                depth = 0
                while o >= 0:
                    if toks[o].text == "]":
                        depth += 1
                    elif toks[o].text == "[":
                        depth -= 1
                        if depth == 0:
                            break
                    o -= 1
                lhs = toks[o:k + 1] + lhs
                k = o - 1
                continue
            if t.kind == "id" or t.text in (".", "->"):
                lhs.insert(0, t)
                k -= 1
                continue
            break
        if not lhs or toks[k].text not in (";", "{", "}", ")", ":") :
            return
        rhs, k, depth = [], i + 1, 0
        while k < len(toks):
            t = toks[k]
            if t.text in "([":
                depth += 1
            elif t.text in ")]":
                depth -= 1
            if depth < 0 or (depth == 0 and t.text in (";", ",")):
                break
            rhs.append(t)
            k += 1
        if toks[k].text == ";" and [t.text for t in lhs] == [t.text for t in rhs]:
            self.add(src, toks[i].line, "R17", f"self-assignment {' '.join(t.text for t in lhs)} = itself", name)

    # -------------------------------------------------------------- types
    def _check_duplicate_types(self):
        scanned = {s.name for s in self.sources}
        for (ns, n), defs in sorted(self.type_defs.items()):
            files = {d[0] for d in defs}
            if len(files) < 2 or not (files & scanned):
                continue
            bodies = {d[2] for d in defs}
            where = ", ".join(sorted(f"{f}:{l}" for f, l, _ in defs))
            first = min(defs, key=lambda d: (d[0] not in scanned, d[0], d[1]))
            src = next(s for s in self.sources if s.name in files and s.name == first[0]) \
                if first[0] in scanned else next(s for s in self.sources if s.name in files)
            line = next(l for f, l, _ in defs if f == src.name)
            label = f"{'struct/union/enum tag' if ns == 'tag' else 'typedef'} {n}"
            if len(bodies) > 1:
                self.findings.append(Finding(src.name, line, "R1", f"{label} defined with different bodies in "
                                             f"{where}", None, n, files=files))
            else:
                self.findings.append(Finding(src.name, line, "R1-copy", f"{label} defined identically in {where}",
                                             None, n))

    # -------------------------------------------------------------- allow list
    def _function_markers(self):
        """(file, function) -> [(kind, rule, reason, line)], and file-scope markers per file."""
        per_func, per_file = defaultdict(list), defaultdict(list)
        for src in self.sources:
            funcs = self.parsed[src.path.resolve()][0]
            for line, end, text in src.comments:
                m = MARKER.search(text)
                if not m:
                    continue
                kind, rule, reason = m.group(1), m.group(2), m.group(3).strip().rstrip("*/").strip()
                owner = next((f for f in funcs if f.decl_line <= line <= f.end_line), None)
                entry = (kind, rule, reason, line)
                if owner:
                    per_func[(src.name, owner.name)].append(entry)
                    if kind == "RULE-EXCEPTION" and rule == DEAD_LOCAL:
                        body_line = src.toks[owner.open].line
                        full = " ".join(text[m.start(3):].split()).rstrip("*/").strip()
                        self.dead_local_markers.append((src.name, owner.name, full, line, end, body_line))
                else:
                    per_file[src.name].append((kind, rule, reason, line, end))
        return per_func, per_file

    def _apply_allow_list(self):
        self.dead_local_markers = []
        per_func, per_file = self._function_markers()
        unit_file = {u: Path(s).resolve() for u, s, _ in self.units}
        name_of = {s.path.resolve(): s.name for s in self.index_sources}
        self.pending = defaultdict(set)     # (file, function) -> rules
        excused = defaultdict(set)
        self.invalid = []
        for (fname, func), entries in per_func.items():
            for kind, rule, reason, line in entries:
                if kind == "MATCH-PENDING":
                    self.pending[(fname, func)].add(rule)
                elif rule == DEAD_LOCAL:
                    continue
                elif rule in MATCH_RULES:
                    self.invalid.append(Finding(fname, line, rule, "RULE-EXCEPTION cannot excuse a [match] rule; "
                                                "mark the function MATCH-PENDING", func, state="invalid-exception"))
                else:
                    excused[(fname, func)].add(rule)
        records = list(self.pending_records)
        for e in self.exceptions.get("exceptions", []):
            if e.get("status") == "match_pending":
                records.append({"unit": e["unit"], "function": e["function"], "rule": e.get("rules_id", "R17")})
            elif e.get("rules_id") == DEAD_LOCAL:
                continue    # a record of the source marker, which alone excuses
            elif e.get("rules_id"):
                path = unit_file.get(e["unit"])
                if path is None or path not in name_of:
                    continue
                if e["rules_id"] in MATCH_RULES:
                    self.invalid.append(Finding(name_of[path], 0, e["rules_id"], "config/exceptions.json entry "
                                                "excuses a [match] rule; give it status match_pending",
                                                e["function"], state="invalid-exception"))
                else:
                    excused[(name_of[path], e["function"])].add(e["rules_id"])
        for r in records:
            path = unit_file.get(r["unit"])
            if path is not None and path in name_of:
                self.pending[(name_of[path], r["function"])].add(r["rule"])
        # R17-dead-local: a valid marker in the function's header or directly above the declaration
        dead_ok = defaultdict(list)     # (file, function) -> [(marker line, marker end, body line)]
        self.dead_local_excused = set()
        for fname, func, reason, line, end, body_line in self.dead_local_markers:
            problem = dead_local_evidence_problem(reason)
            if problem:
                self.invalid.append(Finding(fname, line, "R17", f"RULE-EXCEPTION: {DEAD_LOCAL} {problem}; name "
                                            "the type/size evidence and the alternatives ruled out",
                                            func, state="invalid-exception"))
            else:
                dead_ok[(fname, func)].append((line, end, body_line))
        for f in self.findings:
            key = (f.file, f.function)
            if (f.kind == "dead-local" and f.state == "open" and f.rule not in self.pending.get(key, ())
                    and any(line < body_line or end == f.line - 1 for line, end, body_line in dead_ok.get(key, ()))):
                f.state = "excused"
                self.dead_local_excused.add(key)
                continue
            if f.function and f.rule in self.pending.get(key, ()):
                f.state = "pending"
            elif f.function and f.rule in excused.get(key, ()) and f.rule not in MATCH_RULES:
                f.state = "excused"
            elif not f.function and f.rule not in MATCH_RULES:
                for kind, rule, reason, line, end in per_file.get(f.file, ()):
                    if kind == "RULE-EXCEPTION" and rule == f.rule and line <= f.line <= end + 1:
                        f.state = "excused"

    def _bodies(self):
        """(file, function) -> (source, identifiers used in the definition)."""
        bodies = {}
        for src in self.sources:
            for f in self.parsed[src.path.resolve()][0]:
                bodies[(src.name, f.name)] = (src, {t.text for t in src.toks[f.first:f.close + 1] if t.kind == "id"})
        return bodies

    def _resolve_file_scope(self):
        """Give every file-scope [match] finding the functions it blocks: those naming its symbol in the
        files involved (any .c file when a header is involved). A finding whose every user is
        MATCH-PENDING for its rule is acknowledged."""
        bodies = self._bodies()
        for fd in self.findings:
            if fd.function or fd.tier != "match" or not fd.name:
                continue
            files = fd.files or {fd.file}
            anywhere = any(f.upper().endswith(".H") for f in files)
            fd.users = {key for key, (_, idents) in bodies.items()
                        if fd.name in idents and (anywhere or key[0] in files)}
            if fd.state == "open" and fd.users and all(fd.rule in self.pending.get(k, ()) for k in fd.users):
                fd.state = "pending"

    # -------------------------------------------------------------- summary
    def blocked_functions(self):
        """(file, function) -> set of [match] rules with unexcused findings, after propagation."""
        blocked = defaultdict(set)
        bodies = self._bodies()
        for fd in self.findings:
            if fd.tier != "match" or fd.state != "open":
                continue
            if fd.function:
                blocked[(fd.file, fd.function)].add(fd.rule)
            else:
                for key in fd.users:
                    if fd.rule not in self.pending.get(key, ()):
                        blocked[key].add(fd.rule)
        # unmapped static helpers pass their findings to callers
        mapped = self.mapped_keys()
        changed = True
        while changed:
            changed = False
            for (fname, helper), rules in list(blocked.items()):
                if (fname, helper) in mapped:
                    continue
                for (cname, caller), (_, idents) in bodies.items():
                    if cname == fname and caller != helper and helper in idents:
                        before = len(blocked[(cname, caller)])
                        blocked[(cname, caller)] |= rules
                        changed |= len(blocked[(cname, caller)]) != before
        return blocked

    def mapped_keys(self):
        name_of = {s.path.resolve(): s.name for s in self.index_sources}
        keys = set()
        for u, s, _ in self.units:
            fname = name_of.get(Path(s).resolve())
            for func in self.maps.get(u, {}):
                keys.add((fname, func))
        return keys

    def summary(self):
        scanned = {s.name for s in self.sources}
        mapped = {k for k in self.mapped_keys() if k[0] in scanned}
        defined = {(s.name, f.name) for s in self.sources for f in self.parsed[s.path.resolve()][0]}
        blocked = self.blocked_functions()
        pending = {k for k, rules in self.pending.items() if rules and k in defined}
        failing = {k for k in mapped if blocked.get(k)}
        open_match = [f for f in self.findings if f.tier == "match" and f.state == "open"]
        by_rule = defaultdict(int)
        for f in open_match:
            by_rule[f.rule] += 1
        adv = defaultdict(int)
        for f in self.findings:
            if f.tier == "advisory" and f.state == "open":
                adv[f.rule] += 1
        fail_by_rule = defaultdict(int)
        for k in failing - pending:
            for r in blocked[k]:
                fail_by_rule[r] += 1
        return {
            "function_definitions": len(defined),
            "byte_matched_functions": len(mapped),
            "functions_with_unexcused_match_findings": len(failing - pending),
            "match_pending_functions": len(pending & mapped),
            "matched_under_the_rules": len(mapped - pending - failing),
            "unexcused_match_findings": len(open_match) + len(self.invalid),
            "unexcused_match_findings_by_rule": dict(sorted(by_rule.items())),
            "failing_functions_by_rule": dict(sorted(fail_by_rule.items())),
            "invalid_exceptions": len(self.invalid),
            "advisory_findings_by_rule": dict(sorted(adv.items())),
            "mapped_functions_not_found": sorted(f"{a}:{b}" for a, b in mapped - defined),
            "match_pending_without_definition": sorted(f"{a}:{b}" for a, b in self.pending
                                                       if a in scanned and (a, b) not in defined),
            "match_pending": sorted(f"{a}:{b}" for a, b in pending & mapped),
            "dead_local_exceptions": sorted(f"{a}:{b}" for a, b in self.dead_local_excused
                                           & (mapped - pending - failing)),
        }


def constant_term(toks, start, end):
    """True when a '+'/'-' term at depth 0 of toks[start:end] is a constant (number, UPPER_CASE or sizeof)."""
    depth = 0
    for k in range(start, end):
        t = toks[k]
        if t.text in "([":
            depth += 1
        elif t.text in ")]":
            depth -= 1
        elif depth == 0 and t.text in ("+", "-") and k + 1 < end:
            n = toks[k + 1]
            if n.kind == "num" or n.text == "sizeof" or re.fullmatch(r"[A-Z][A-Z0-9_]*", n.text):
                return True
    return False


def operand_before(toks, k):
    """True when toks[k] (an operator) has an operand on its left, i.e. it is binary."""
    if k == 0:
        return False
    p = toks[k - 1]
    return p.kind in ("id", "num", "str") and p.text not in KEYWORDS - {"sizeof"} or p.text in (")", "]")


# ------------------------------------------------------------------ entry points

def registered_units():
    import verify  # noqa: E402  (tools/verify.py; the UNITS registry)
    return [(u, Path(s), Path(m)) for u, s, m in verify.UNITS]


def sdk_include_dir():
    try:
        lock = json.loads((PROJECT / "config/toolchain.lock.json").read_text(encoding="utf-8"))
        entry = lock["include_dirs"][lock["selected_include_dir"]]
        path = (WORKSPACE / entry["path"])
        return path if path.is_dir() else None
    except (OSError, KeyError, ValueError):
        return None


def load_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except OSError:
        return {}


def check(files=None, units=None, include_dir=PROJECT / "include", sdk_dir="auto",
          exceptions_path=PROJECT / "config/exceptions.json",
          match_pending_path=PROJECT / "config/match_pending.json", decimal=True) -> Checker:
    units = registered_units() if units is None else units
    if files is None:
        files = sorted((PROJECT / "src").glob("*.c")) + sorted(Path(include_dir).glob("*.H"))
    if sdk_dir == "auto":
        sdk_dir = sdk_include_dir()
    return Checker(files, units, include_dir, sdk_dir, load_json(exceptions_path),
                   load_json(match_pending_path).get("match_pending", []), decimal).run()


def format_summary(s):
    lines = [f"rules: {s['matched_under_the_rules']} of {s['byte_matched_functions']} byte-matched functions "
             "matched under the rules",
             f"  function definitions: {s['function_definitions']}",
             f"  MATCH-PENDING: {s['match_pending_functions']}",
             f"  functions with unexcused [match] findings: {s['functions_with_unexcused_match_findings']}"
             + ("" if not s["failing_functions_by_rule"] else
                " (" + ", ".join(f"{r} {n}" for r, n in s["failing_functions_by_rule"].items()) + ")"),
             f"  unexcused [match] findings: {s['unexcused_match_findings']}"
             + ("" if not s["unexcused_match_findings_by_rule"] else
                " (" + ", ".join(f"{r} {n}" for r, n in s["unexcused_match_findings_by_rule"].items()) + ")"),
             "  advisory findings: " + (", ".join(f"{r} {n}" for r, n in s["advisory_findings_by_rule"].items())
                                        or "none")]
    if s["invalid_exceptions"]:
        lines.append(f"  invalid exceptions ([match] rule excused): {s['invalid_exceptions']}")
    if s["match_pending_without_definition"]:
        lines.append("  MATCH-PENDING records naming no definition: " + ", ".join(s["match_pending_without_definition"]))
    if s["mapped_functions_not_found"]:
        lines.append("  mapped functions without a definition: " + ", ".join(s["mapped_functions_not_found"]))
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--files", nargs="+", type=Path, help="scan only these files")
    ap.add_argument("--report-only", action="store_true", help="always exit 0")
    ap.add_argument("--summary-only", action="store_true", help="print only the summary")
    ap.add_argument("--match-only", action="store_true", help="print only [match] findings")
    ap.add_argument("--show-excused", action="store_true", help="also print excused and MATCH-PENDING findings")
    ap.add_argument("--no-decimal", action="store_true", help="skip the decimal-literal check (R11-dec)")
    ap.add_argument("--json", type=Path, help="write the summary and findings as JSON")
    args = ap.parse_args(argv)
    c = check(files=args.files, decimal=not args.no_decimal)
    s = c.summary()
    if not args.summary_only:
        for f in sorted(c.findings + c.invalid, key=lambda f: (f.file, f.line, f.rule)):
            if args.match_only and f.tier != "match":
                continue
            if f.state in ("excused", "pending") and not args.show_excused:
                continue
            print(f.format())
    print(format_summary(s))
    if args.json:
        args.json.write_text(json.dumps({"summary": s, "findings": [
            {"file": f.file, "line": f.line, "rule": f.rule, "tier": f.tier, "message": f.message,
             "function": f.function, "state": f.state} for f in c.findings + c.invalid]}, indent=2) + "\n",
            encoding="utf-8")
    return 0 if args.report_only or s["unexcused_match_findings"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
