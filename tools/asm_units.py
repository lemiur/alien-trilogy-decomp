"""Policy for assembly units, shared by tools/verify.py, tools/gate.py and tools/progress.py.

AGENTS.md admits assembly in two categories, each confined to the ranges that
``config/asm_regions.json`` lists for it:

- ``asm`` ("assembly"): the hand-written GTE/renderer code (AGENTS.md, "Hand-written
  assembly"); registry ``ASM_UNITS`` in tools/verify.py;
- ``library_asm`` ("library assembly"): Sony Runtime Library modules that retail links as
  rebuilds matching no archive in ../sdk (AGENTS.md, "Library-variant assembly"); registry
  ``LIBRARY_ASM_UNITS`` in tools/verify.py, one unit per library module.

Both are real .s files assembled by ASPSX (tools/build_asm.py). Their bytes are counted per
category, never as matched C, and library assembly never as certified library.

Rules enforced here:
- an asm unit is a .s file, a C unit a .c file, and no unit name is registered twice;
- every function (routine or table) of an asm unit lies wholly inside one ``text`` range of
  its category, and every ``config/data.json`` definition of it wholly inside one ``data``
  range (.rdata/.data/.sdata tables of library modules); a category without data ranges
  admits no data definitions;
- an asm unit's .text holds instructions only: data directives there (``.word`` and the
  like) would be pasted bytes, which match trivially, so they are refused, except for the
  data tables that retail keeps in .text (the category's ``text_data`` windows: the renderer
  tables after the last routine). Those are written as annotated tables and admitted only for
  labels placed wholly inside a window. Their bytes count like the code's.
Inline asm in a C unit is not affected: the gate keeps linking it as "verbatim".
"""
from __future__ import annotations

import json
from pathlib import Path
import re

PROJECT = Path(__file__).resolve().parents[1]
REGIONS_CONFIG = PROJECT / "config/asm_regions.json"
CATEGORIES = ("asm", "library_asm")   # also the gate's provenance kinds and verify.py's check kinds
DATA_DIRECTIVES = (".word", ".half", ".short", ".byte", ".ascii", ".asciiz", ".space")
ASM_ASSEMBLER = "A234"                 # ASPSX 2.34, the project's assembler
# verify.py report keys per category: <prefix>_bytes, <prefix>_routines, ...
REPORT_PREFIX = {"asm": "byte_matched_asm", "library_asm": "byte_matched_library_asm"}


class AsmPolicyError(RuntimeError):
    pass


def _num(value):
    return int(value, 0) if isinstance(value, str) else int(value)


class Category:
    """One category's ranges, each a tuple of half-open (lo, hi) pairs. (Not a dataclass:
    the tools load this file by path, outside sys.modules.)"""

    def __init__(self, name, label, text, text_data, data):
        self.name, self.label, self.text, self.text_data, self.data = name, label, text, text_data, data

    def holds(self, ranges, address, size):
        return any(lo <= address and address + size <= hi for lo, hi in ranges)


def _ranges(value, where):
    out = []
    for pair in value:
        first, last = _num(pair[0]), _num(pair[1])
        if last < first:
            raise AsmPolicyError(f"{where}: range {first:#x}..{last:#x} is empty")
        out.append((first, last + 1))
    out.sort()
    for (a, b), (c, d) in zip(out, out[1:]):
        if c < b:
            raise AsmPolicyError(f"{where}: ranges {a:#x}.. and {c:#x}.. overlap")
    return tuple(out)


def load_policy(path=REGIONS_CONFIG):
    """{category: Category} from the regions config (ranges [first, last], inclusive), after
    checking it is consistent: exactly the known categories, ordered disjoint ranges, text_data
    windows inside text, and no byte in two categories or in both text and data."""
    raw = json.loads(Path(path).read_text(encoding="utf-8"))["categories"]
    if set(raw) != set(CATEGORIES):
        raise AsmPolicyError(f"{path}: categories must be exactly {list(CATEGORIES)}, not {sorted(raw)}")
    policy = {}
    for name in CATEGORIES:
        entry = raw[name]
        category = Category(name, entry["label"], _ranges(entry["text"], f"{name}.text"),
                            _ranges(entry.get("text_data", []), f"{name}.text_data"),
                            _ranges(entry.get("data", []), f"{name}.data"))
        for lo, hi in category.text_data:
            if not category.holds(category.text, lo, hi - lo):
                raise AsmPolicyError(f"{name}: text_data window {lo:#x}.. is not inside its text ranges")
        policy[name] = category
    every = sorted((lo, hi, name, kind) for name, c in policy.items()
                   for kind, ranges in (("text", c.text), ("data", c.data)) for lo, hi in ranges)
    for (a, b, n, k), (c, d, m, j) in zip(every, every[1:]):
        if c < b:
            raise AsmPolicyError(f"{n}.{k} range {a:#x}.. overlaps {m}.{j} range {c:#x}..")
    return policy


POLICY = load_policy()
LABELS = {name: c.label for name, c in POLICY.items()}
# The renderer region and its table window as single ranges (the "asm" category has one each).
ASM_REGION = POLICY["asm"].text[0]
TEXT_DATA_WINDOW = POLICY["asm"].text_data[0]


def inside(address, size, category="asm", text=True, policy=None):
    """Whether a placement lies wholly inside one text (or data) range of the category."""
    c = (policy or POLICY)[category]
    return c.holds(c.text if text else c.data, address, size)


def describe(unit, name, address, size, category="asm", text=True, policy=None):
    c = (policy or POLICY)[category]
    ranges = c.text if text else c.data
    shown = ", ".join(f"{lo:#x}..{hi - 1:#x}" for lo, hi in ranges) or "(none)"
    if category == "asm" and text:
        what = "the hand-written assembly region"
    else:
        what = f"the {c.label} {'text' if text else 'data'} ranges"
    return f"{c.label} unit {unit}: {name} at {address:#x}+{size:#x} lies outside {what} {shown}"


def region_violations(unit, spec, data_defs=None, category="asm", policy=None):
    """Messages for every function of an asm unit outside its category's text ranges and
    every data definition outside its data ranges."""
    out = []
    for name, v in spec.get("functions", {}).items():
        if not inside(_num(v["address"]), int(v["size"]), category, True, policy):
            out.append(describe(unit, name, _num(v["address"]), int(v["size"]), category, True, policy))
    for name, v in (data_defs or {}).items():
        if not inside(_num(v["address"]), int(v["size"]), category, False, policy):
            out.append(describe(unit, name, _num(v["address"]), int(v["size"]), category, False, policy))
    return out


def validate_registry(c_units, asm_units, data_manifest, library_units=(), policy=None):
    """Check unit kinds and asm placement for the registries (name, source, map):
    ``asm_units`` are category "asm", ``library_units`` category "library_asm"."""
    names = [entry[0] for entry in list(c_units) + list(asm_units) + list(library_units)]
    if len(names) != len(set(names)):
        raise AsmPolicyError("a unit name is registered more than once in UNITS/ASM_UNITS/LIBRARY_ASM_UNITS")
    for unit, source, _ in c_units:
        if source.suffix.lower() != ".c":
            raise AsmPolicyError(f"C unit {unit} must be a .c source, not {source.name}")
    problems = []
    for category, registry in (("asm", asm_units), ("library_asm", library_units)):
        for unit, source, mapping in registry:
            if source.suffix.lower() != ".s":
                raise AsmPolicyError(f"asm unit {unit} must be a .s source, not {source.name}")
            spec = json.loads(mapping.read_text(encoding="utf-8"))
            problems += region_violations(unit, spec, data_manifest.get(unit, {}), category, policy)
    if problems:
        raise AsmPolicyError("; ".join(problems))


def text_data_owners(listing):
    """Global labels that own data directives in .text, from an assembled (preprocessed) listing.

    A statement belongs to the last label defined before it ("*" before any label). Statements
    are split on ';' as the preprocessor joins a macro's statements on one line; numeric local
    labels ("1:") and "$" labels do not change the owner.
    """
    owners, section, owner = set(), ".text", "*"
    for line in listing.splitlines():
        if line.lstrip().startswith("#"):
            continue  # preprocessor line markers
        for statement in line.split(";"):
            text = statement.strip()
            match = re.match(r"([A-Za-z_][\w.]*)\s*:(.*)$", text)
            if match:
                owner, text = match.group(1), match.group(2).strip()
            elif re.match(r"(\$\w+|\d+)\s*:", text):
                text = text.split(":", 1)[1].strip()
            words = text.replace(",", " ").split()
            if not words:
                continue
            if words[0] in (".text", ".data", ".sdata", ".rdata", ".bss", ".sbss"):
                section = words[0]
            elif words[0] == ".section" and len(words) > 1:
                section = words[1]
            elif words[0] in DATA_DIRECTIVES and section == ".text":
                owners.add(owner)
    return owners


def table_labels(source_text, spec, category="asm"):
    """Labels of an asm unit that are data tables in .text, not routines.

    A table is a mapped label that owns data directives in .text (text_data_owners, run on the
    source with its comments removed) and lies wholly inside one of the category's text_data
    windows, the only place check_text_data admits them. Every other mapped label is a code
    routine. Data definitions (.rdata/.data/.sdata) are not mapped labels and not counted here.
    """
    stripped = re.sub(r"/\*.*?\*/", "", source_text, flags=re.S)
    c = POLICY[category]
    functions = spec.get("functions", {})
    return {name for name in text_data_owners(stripped)
            if name in functions and c.holds(c.text_data, _num(functions[name]["address"]),
                                             int(functions[name]["size"]))}


def check_text_data(unit, listing, spec, category="asm"):
    """Admit data directives in an asm unit's .text only for placements inside a text_data window."""
    c = POLICY[category]
    shown = ", ".join(f"{lo:#x}..{hi - 1:#x}" for lo, hi in c.text_data) or "(none)"
    functions = spec.get("functions", {})
    problems = []
    for owner in sorted(text_data_owners(listing)):
        if owner not in functions:
            problems.append(f"data directives in .text before any mapped label ({owner})")
            continue
        address, size = _num(functions[owner]["address"]), int(functions[owner]["size"])
        if not c.holds(c.text_data, address, size):
            problems.append(f"{owner} at {address:#x}+{size:#x} emits data directives in .text outside "
                            f"the table window {shown}")
    if problems:
        raise AsmPolicyError(f"{c.label} unit {unit}: " + "; ".join(problems) +
                             "; code must be written as instructions")


def cross_check(owners, report, specs, asm_unit_names, kind="asm"):
    """Compare the link's placements of one asm category with verify.py's results for it.

    owners: the gate linker's owner records; report: verify.py's report; specs: unit -> map;
    asm_unit_names: the gated units of this category; kind: "asm" or "library_asm" (the owner
    kind, the check kind and, through REPORT_PREFIX, the report's byte total). Routines and
    data definitions are compared by (unit, symbol, address, size). Returns a list of problems.
    """
    label = LABELS[kind]
    total = f"{REPORT_PREFIX[kind]}_bytes"
    problems = []
    checks = report.get("checks", [])
    matched = {(c["unit"], c["function"], _num(c["address"]), int(c["size"]))
               for c in checks if c.get("status") == "match" and c.get("kind") == kind}
    linked = {(o["unit"], o["symbol"], _num(o["address"]), o["size"]) for o in owners
              if o["kind"] == kind and o["unit"] and o["symbol"] in specs[o["unit"]]["functions"]}
    for extra in sorted(linked - matched):
        problems.append(f"linked {label} routine {extra[0]}:{extra[1]} is not matched by verify.py")
    for extra in sorted(matched - linked):
        problems.append(f"verify.py matched {label} routine {extra[0]}:{extra[1]} but the link did not place it")
    verified_data = {(c["unit"], d["symbol"], _num(d["address"]), int(d["size"]))
                     for c in report.get("data_checks", []) if c.get("status") == "match" and c.get("kind") == kind
                     for d in c.get("definitions", [])}
    linked_data = {(o["unit"], o["symbol"], _num(o["address"]), o["size"]) for o in owners
                   if o["kind"] == kind and o["unit"] and o["symbol"] not in specs[o["unit"]]["functions"]}
    for extra in sorted(linked_data - verified_data):
        problems.append(f"linked {label} datum {extra[0]}:{extra[1]} is not verified by verify.py")
    for extra in sorted(verified_data - linked_data):
        problems.append(f"verify.py verified {label} datum {extra[0]}:{extra[1]} but the link did not place it")
    if any(c.get("kind") == kind and c["unit"] not in asm_unit_names
           for c in checks + report.get("data_checks", [])):
        problems.append(f"verify.py reports a {label} result for a unit that is not a gated {label} unit")
    linked_bytes = sum(o["size"] for o in owners if o["kind"] == kind)
    if linked_bytes != report.get(total, 0):
        problems.append(f"link places {linked_bytes} {label} bytes, verify.py verified {report.get(total, 0)}")
    return problems
