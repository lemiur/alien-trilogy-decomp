"""Whole-executable byte gate for SLUS_000.07.

Compiles every unit registered in ``UNITS`` (tools/verify.py) into a fresh
directory, links the matched functions and the initialized data that
``config/data.json`` places from source over fallback bytes of the original
executable, writes the complete PS-X EXE (header included) together with a
per-byte provenance record, and requires the SHA-256 of the result to equal the
contract in ``config/gate.json``. The only success line is

    GATE OK sha256=<hash> source_bytes=N/514048 ... generated_bytes=G asm_bytes=A library_asm_bytes=L ...

where N counts function, data-definition and generated-data bytes (G of them generated).
A counts the bytes of hand-written assembly units (``ASM_UNITS`` in tools/verify.py, kind
"asm") and L those of library assembly units (``LIBRARY_ASM_UNITS``, kind "library_asm": Sony
Runtime Library variant modules), both assembled by tools/build_asm.py; neither is ever part
of N, and L is never part of A. AGENTS.md admits each category only inside its ranges in
``config/asm_regions.json`` (tools/asm_units.py): the gate refuses an asm unit with a routine
outside its category's text ranges or a data definition outside its data ranges, and refuses
data directives in an asm unit's ``.text`` (emitted bytes are not instructions) outside the
category's table windows. Inline asm in a C unit stays "verbatim" (below).

Linker: this tool is its own linker, written in Python. It places each mapped
function and data definition at its retail address and applies every PsyQ
relocation of the object (HI16, LO16, MIPS26, GP-relative, word, and the split
HI16/LO16 halves of asm/lib/hilo.h, types 26/30: ``split_relocation``) against the
link's symbol table: symbols defined by placed objects take their placed
address, every other name takes the address the unit maps give it (it then
names a fallback byte range). PSYLINK is not used because the registered units
are recovery-round groupings, not the original translation units: one unit's
``.text`` holds functions that are discontiguous in the retail image (``seeds``
spans 0x80013a1c..0x80015d50 with unrelated code between), so PSYLINK, which
places a section contiguously, cannot put them at their retail addresses. What
PSYLINK needs, and what the evidence already pins, is in
``docs/notes/psylink_notes.md``.

Compiler-generated data (string pools, jump tables, constants: labelled items of
``.rdata``/``.data``/``.sdata`` that no data definition places) is linked from
source, as kind "generated", when its address is fixed: every relocation from the
unit's placed code, and every word relocation from its placed data definitions, into
the item must imply one address (read from the retail operands of that code, or the
retail word at the relocated data field), the address must lie in the retail section of the same
name (``config/section_layout.json``, from ``tools/section_layout.py``), and no
other placement may cover the item. Item extents come from the unit's assembly
listing and must add up to the object's section sizes (``tools/data_extent.py``,
the sizer data_check.py shares): an item is the bytes its directives emit, the
``.align`` padding after it belongs to no item and comes from the fallback, and a
data definition must place exactly one item. A relocation into an item at an offset
(``$LC0+4``) is an interior reference: it implies the item's address as its retail
target minus that offset, and never starts a new item. Anything else stays with the
fallback and is listed in ``provenance.json`` (``generated_data_findings``), which
also records each data definition that the relocations confirm, and fails the gate
on one they contradict. ``tools/generated_witness.py`` re-checks every generated
claim from verify.py's objects and results. ``provenance.json`` also lists
placements whose object section is not the retail section at their address
(``section_conformance``). Uninitialised data (.sbss, .bss, COMMON) is not in the
payload; its layout is ``config/bss_layout.json``.

Static functions and static data (tools/lnk_symbols.py). The object locates an exported
function by its XDEF and a ``static`` one (``"static": true`` in the unit map) by its
local-symbol record; either way the map's address and size place it, its relocations are
resolved and its bytes linked like any function, and it counts as source. Every ``.text``
byte must belong to one mapped function and every ``.sbss``/``.bss`` byte to one
``static_data`` entry whose address is the unit's section base plus its offset. A static in
``.sdata``/``.data``/``.rdata`` (an unexported data label of the listing; ASPSX writes no
local-symbol record for it) is a ``static_data`` entry too, placed by the data definition of
its name (which the entry must repeat), linked and counted as source data. A static's
name is never bound in the link's global symbol table: references to it are
section-relative and resolve through its own unit's placements, so two units may each
define a static of the same name (as with ``$LC0``).

Blind spots covered (AGENTS.md, "The byte gate"):
- the contract hash must equal the toolchain lock's reference hash and the
  configured original must still hash to it;
- the output directory is deleted and recreated, every unit is rebuilt into it,
  and results are judged from exit codes, never from leftover files;
- functions or units whose assembly contains inline asm (``#APP``) or data
  directives in ``.text`` are linked as "verbatim", never counted as source;
- the tree must be clean (``builds/``, ``reports/`` and ``__pycache__`` aside);
- an empty work list is refused;
- robustness: build.py retries a DOSBox-X crash (no tool result, abnormal host exit code,
  e.g. 0xC0000409 under load) at most twice in a fresh job directory and logs each retry;
  a compiler, assembler or link error is never retried. A retry only repeats a build, so it
  cannot admit a wrong result: the verdict is judged solely from exit codes and the final
  hash. On any exit path, including KeyboardInterrupt, every child (verify.py, build.py,
  DOSBox-X, whole process trees) is killed and reaped and the verify.log handle closed, and
  the next run's deletion of the output directory is retried, so it is always deletable.
  --jobs defaults to min(12, cpu_count) for the unit builds and again for verify.py (up to
  twice that many DOSBox-X at once); five runs at 12, 6 and 4 with five other lanes building
  showed no crash, so the default is unchanged and --jobs is the knob if crashes appear;
- the source byte count from the provenance record is cross-checked against an
  independent ``verify.py`` run (or ``--verify-report``); any disagreement fails;
  generated-data bytes are cross-checked against ``generated_witness.py``; the bytes and
  placements of each asm category are cross-checked against verify.py's results for it the
  same way.
"""
from __future__ import annotations

import argparse
from array import array
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import threading
import time

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import proctree  # noqa: E402

WORKSPACE = PROJECT.parent
TOOLS = PROJECT / "tools"
CONTRACT = PROJECT / "config/gate.json"
DATA_MANIFEST = PROJECT / "config/data.json"
IGNORED_DIRTY = ("builds/", "reports/")


def _load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


verify = _load_module("verify_registry", TOOLS / "verify.py")
section_layout = _load_module("section_layout", TOOLS / "section_layout.py")
generated_witness = _load_module("generated_witness", TOOLS / "generated_witness.py")
data_extent = _load_module("data_extent", TOOLS / "data_extent.py")
lnk_symbols = verify.lnk_symbols  # one module instance, so its SymbolError is verify's


class GateError(Exception):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def num(value):
    return int(value, 0) if isinstance(value, str) else int(value)


# ---------------------------------------------------------------- preflight

def load_contract(path=CONTRACT):
    """Return (contract, original bytes) after proving they belong together."""
    contract = json.loads(Path(path).read_text(encoding="utf-8"))
    lock = json.loads((PROJECT / "config/toolchain.lock.json").read_text(encoding="utf-8"))
    if contract["sha256"] != lock["reference_sha256"]:
        raise GateError("contract hash differs from the toolchain lock's reference hash")
    reference = WORKSPACE / contract["reference"]
    raw = reference.read_bytes()
    if sha256(raw) != contract["sha256"]:
        raise GateError(f"configured original {reference} no longer has the contract hash")
    if len(raw) != contract["file_size"] or raw[:8] != b"PS-X EXE":
        raise GateError("configured original is not the PS-X EXE the contract describes")
    load, size = struct.unpack_from("<II", raw, 0x18)
    if (load != num(contract["load_address"]) or size != contract["payload_size"]
            or contract["header_size"] + size != len(raw)
            or struct.unpack_from("<I", raw, 0x10)[0] != num(contract["entry"])):
        raise GateError("original header disagrees with the contract")
    for name, digest in lock["files"].items():
        if sha256((WORKSPACE / name).read_bytes()) != digest:
            raise GateError(f"toolchain file differs from lock: {name}")
    return contract, raw


def dirty_paths(repo):
    """Tracked or untracked changes, ignoring build output and generated reports."""
    result = subprocess.run(["git", "-C", str(repo), "status", "--porcelain=v1", "-z",
                             "--untracked-files=all"], capture_output=True)
    if result.returncode:
        raise GateError(f"git status failed: {result.stderr.decode(errors='replace').strip()}")
    entries = result.stdout.decode("utf-8", errors="replace").split("\0")
    dirty, i = [], 0
    while i < len(entries):
        entry = entries[i]
        i += 1
        if not entry:
            continue
        status, path = entry[:2], entry[3:]
        if "R" in status or "C" in status:
            i += 1  # the rename source follows as its own entry
        if path.startswith(IGNORED_DIRTY) or "__pycache__/" in path or path.endswith(".pyc"):
            continue
        dirty.append(path)
    return dirty


# -------------------------------------------------------------------- build

class Children:
    """Every child process the gate starts, so that any exit path can kill and reap them all."""

    def __init__(self):
        self.lock = threading.Lock()
        self.procs = {}  # Popen -> proctree.Tree
        self.stopped = False
        self.files = []

    def start(self, argv, **kwargs):
        with self.lock:
            if self.stopped:
                raise GateError("gate is shutting down")
            tree = proctree.Tree(argv, **kwargs)
            proc = tree.proc
            self.procs[proc] = tree
        return proc

    def finish(self, proc):
        with self.lock:
            tree = self.procs.pop(proc, None)
        if tree is not None:
            tree.kill()  # leftovers of a finished child (a DOSBox-X that outlived it) die with it
            tree.close()

    def kill_all(self):
        """Refuse new children, then terminate each child's whole tree (verify.py, build.py, DOSBox-X)."""
        with self.lock:
            self.stopped = True
            trees = list(self.procs.values())
            self.procs.clear()
            files, self.files = self.files, []
        for tree in trees:
            tree.kill()
            tree.close()
        for handle in files:
            handle.close()


CHILDREN = Children()


def build_unit(entry, build_root, children=None):
    """Compile one unit into build_root; judged from build.py's exit code only.

    build.py retries a DOSBox-X crash (never a compile error) itself; that cannot change
    a verdict, because the exit code and the final hash alone decide.
    """
    children = children or CHILDREN
    unit, source, mapping = entry
    report = build_root / f"{unit}.build.json"
    asm = verify.is_asm_unit(unit) or source.suffix.lower() == ".s"
    tool = "build_asm.py" if asm else "build.py"
    proc = children.start([sys.executable, str(TOOLS / tool), "--source", str(source),
                           "--build-root", str(build_root), "--json-report", str(report)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=WORKSPACE)
    try:
        stdout, stderr = proc.communicate()
    finally:
        children.finish(proc)
    for line in stderr.splitlines():
        if line.startswith("build.py: DOSBox-X crashed"):
            print(f"[{unit}] {line}", file=sys.stderr, flush=True)
    result = subprocess.CompletedProcess(proc.args, proc.returncode, stdout, stderr)
    if result.returncode:
        raise GateError(f"build of {unit} failed (exit {result.returncode}): "
                        f"{(result.stderr or result.stdout).strip()[-400:]}")
    build = json.loads(result.stdout)
    job = Path(build["job"])
    if sha256((job / ("INPUT.S" if asm else "INPUT.C")).read_bytes()) != sha256(source.read_bytes()):
        raise GateError(f"source of {unit} changed during the build")
    # The listing the assembler read: the compiler's output, or the preprocessed hand-written unit.
    return unit, Path(build["object"]), job / ("INPUT.I" if asm else "INPUT.S")


def verbatim_symbols(asm_text):
    """Names whose bytes come from inline asm or emitted data in .text ("*" = whole unit)."""
    verbatim, section, function, in_app, app_outside = set(), ".text", None, False, False
    for line in asm_text.splitlines():
        text = line.strip()
        if text == "#APP":
            in_app = True
            if function:
                verbatim.add(function)
            else:
                app_outside = True
            continue
        if text == "#NO_APP":
            in_app = False
            continue
        words = text.replace(",", " ").split()
        if not words:
            continue
        directive = words[0]
        if directive in (".text", ".data", ".sdata", ".rdata", ".bss", ".sbss"):
            section = directive
        elif directive == ".section" and len(words) > 1:
            section = words[1]
        elif directive == ".ent" and len(words) > 1:
            function = words[1]
        elif directive == ".end":
            function = None
        elif directive in (".word", ".half", ".byte", ".ascii", ".asciiz", ".space") and section == ".text":
            if function or not in_app:  # top-level asm blocks are attributed by their labels
                verbatim.add(function or "*")
        if in_app and not function and text.endswith(":") and " " not in text:
            verbatim.add(text[:-1])
    if app_outside and not verbatim:
        verbatim.add("*")
    return verbatim


# --------------------------------------------------------------------- link

@dataclass
class Placement:
    kind: str          # "function", "data", "verbatim", "generated", "asm" or "library_asm"
    unit: str
    symbol: str
    address: int
    size: int
    section: int
    offset: int


@dataclass
class UnitObject:
    unit: str
    obj: object
    spec: dict
    placements: list = field(default_factory=list)


def asm_unit_object(unit, object_path, spec, data_defs, listing_text, category="asm"):
    """An assembly unit: every placement is of its category's kind, "asm" or "library_asm"
    (policy: tools/asm_units.py and config/asm_regions.json)."""
    try:
        verify.asm_units.check_text_data(unit, listing_text, spec, category)
        problems = verify.asm_units.region_violations(unit, spec, data_defs, category)
    except verify.asm_units.AsmPolicyError as exc:
        raise GateError(str(exc))
    if problems:
        raise GateError("; ".join(problems))
    return unit_object(unit, object_path, spec, data_defs, set(), asm=category, listing=listing_text)


def unit_object(unit, object_path, spec, data_defs, verbatim, asm=None, listing=None):
    """A unit's placements. ``asm`` is the category ("asm" or "library_asm") of an assembly
    unit, whose every placement is of that kind; None for a C unit."""
    try:
        # Exported functions by XDEF, static ones by local symbol (tools/lnk_symbols.py); every
        # .text byte owned by one mapped function and every .sbss/.bss byte by one static_data entry.
        obj = lnk_symbols.load(object_path)
        located = lnk_symbols.audit(obj, spec)
    except (lnk_symbols.SymbolError, AssertionError, ValueError, IndexError, struct.error) as exc:
        raise GateError(f"{unit}: compiled definitions differ from the map: {exc}")
    functions = spec["functions"]
    item = UnitObject(unit, obj, spec)
    for name, (sid, offset) in sorted(located.items(), key=lambda entry: entry[1]):
        kind = asm if asm else "verbatim" if (name in verbatim or "*" in verbatim) else "function"
        item.placements.append(Placement(kind, unit, name, num(functions[name]["address"]),
                                         int(functions[name]["size"]), sid, offset))
    # An initialised static is an unexported data label of the listing, declared in static_data
    # and placed by the data definition of its name (tools/lnk_symbols.py).
    try:
        labels = data_extent.static_labels(listing) if listing is not None else {}
    except data_extent.ExtentError as exc:
        raise GateError(f"{unit}: {exc}")
    problems = lnk_symbols.initialised_statics(spec, data_defs, labels)
    if problems:
        raise GateError(f"{unit}: " + "; ".join(problems))
    exported_at = lnk_symbols.named_data(obj, spec, labels)
    for name, definition in data_defs.items():
        try:  # keyed by exported or declared static name, or by position (".rdata+0x38") for an unnamed item
            section_name, offset = data_extent.definition_position(name, definition, exported_at)
        except data_extent.ExtentError as exc:
            raise GateError(f"{unit}: {exc}")
        sid = next((s for s, n in obj.sections.items() if n == section_name), None)
        if sid is None:
            raise GateError(f"{unit}: data definition {name} has no section in the object")
        if obj.sections[sid] not in (".data", ".sdata", ".rdata"):
            raise GateError(f"{unit}: data definition {name} is not initialized data")
        kind = asm if asm else "verbatim" if (name in verbatim or "*" in verbatim) else "data"
        item.placements.append(Placement(kind, unit, name, num(definition["address"]),
                                         int(definition["size"]), sid, offset))
    return item


# ---------------------------------------------------- compiler-generated data

DATA_SECTIONS = data_extent.DATA_SECTIONS


def _extent(function):
    """tools/data_extent.py, the sizer data_check.py shares, with its errors as GateError."""
    def call(*args):
        try:
            return function(*args)
        except data_extent.ExtentError as exc:
            raise GateError(str(exc))
    call.__doc__ = function.__doc__
    return call


ascii_length = _extent(data_extent.ascii_length)
split_operands = _extent(data_extent.split_operands)
asm_data_items = _extent(data_extent.asm_data_items)


def section_reference(expr):
    """(section id, constant) for a section-relative relocation expression, else None."""
    if expr[0] == 4:
        return expr[1], 0
    if expr[0] == 44:
        for a, b in ((expr[1], expr[2]), (expr[2], expr[1])):
            if a[0] == 4 and b[0] == "c":
                return a[1], b[1]
    return None


def split_relocation(kind, expr):
    """(82, E) / (84, E) for an ASPSX split HI16/LO16 half, else (kind, expr) unchanged.

    asm/lib/hilo.h writes a half that retail separates from its partner as an explicit
    16-bit field expression: ``lui r, HI16(E)`` is patch type 26 with ``(E + 0x8000) >> 16``
    and ``LO16(E)`` is type 30 with ``E - (((E + 0x8000) >> 16) << 16)`` (operands serialized
    right first). PSYLINK writes exactly the HI16/LO16 halves of E for them, so the link treats
    each as that half. Any other type-26 expression stays type 26 and is refused. E is one
    symbol or section plus a constant; ASPSX folds the constants of the sum, so the shapes are
    compared as (base, constant)."""
    def terms(e):
        if e[0] == "c":
            return None, e[1]
        if e[0] in (2, 4):
            return e, 0
        if e[0] == 44:
            a, b = terms(e[1]), terms(e[2])
            if a is None or b is None or (a[0] is not None and b[0] is not None):
                return None
            return (a[0] if a[0] is not None else b[0]), (a[1] + b[1]) & 0xffffffff

    def rounded(e):  # (E + 0x8000) >> 16 -> (base, constant) of E
        if e[0] != 60 or e[1] != ("c", 16):
            return None
        sum_ = terms(e[2])
        if sum_ is None or sum_[0] is None:
            return None
        return sum_[0], (sum_[1] - 0x8000) & 0xffffffff

    target = None
    if kind == 26:
        target = rounded(expr)
    elif kind == 30 and expr[0] == 46 and expr[1][0] == 58 and expr[1][1] == ("c", 16):
        target = rounded(expr[1][2])
        if target != terms(expr[2]):
            target = None
    if target is None:
        return kind, expr
    base, constant = target
    return (82 if kind == 26 else 84), (base if constant == 0 else (44, ("c", constant), base))


def unpaired_split_halves(patches, start, end):
    """Split halves in [start, end) whose partner half (same E) is not in that range."""
    halves = {82: set(), 84: set()}
    for kind, offset, expr in patches:
        if start <= offset < end and split_relocation(kind, expr)[0] != kind:
            normal, target = split_relocation(kind, expr)
            halves[normal].add(repr(target))
    return sorted(halves[82] ^ halves[84])


def observed_targets(item, raw, load, header, gp):
    """{(section id, constant): {value, ...}} read from the retail operands of placed bytes.

    Anchors come from placed code (HI16, LO16, GP-relative and word relocations) and from
    the word relocations of placed data definitions; either way the value is what the retail
    image holds at the relocated field. HI16 and LO16 halves of one expression are combined
    only when each is unique.
    """
    halves, values = {}, {}
    for p in item.placements:
        if p.kind not in ("function", "verbatim", "data"):
            continue  # never generated items; tools/generated_witness.py checks the same set
        code = p.kind != "data" and p.symbol in item.spec["functions"]
        for kind, offset, expr in item.obj.patches.get(p.section, []):
            if not p.offset <= offset < p.offset + p.size:
                continue
            if not code and kind != 16:
                continue  # a data definition anchors only through whole words
            kind, expr = split_relocation(kind, expr)
            ref = None
            if kind == 30 and expr[0] == 46 and expr[1][0] == 12:
                ref = section_reference(expr[2])
            elif kind in (16, 82, 84):
                ref = section_reference(expr)
            if ref is None:
                continue
            word = struct.unpack_from("<I", raw, header + p.address + offset - p.offset - load)[0]
            if kind == 16:
                values.setdefault(ref, set()).add(word)
            elif kind == 30:
                low = word & 0xffff
                values.setdefault(ref, set()).add((gp + low - (0x10000 if low & 0x8000 else 0)) & 0xffffffff)
            else:
                halves.setdefault(ref, {}).setdefault(kind, set()).add(word & 0xffff)
    for ref, parts in halves.items():
        if len(parts.get(82, ())) == 1 and len(parts.get(84, ())) == 1:
            hi, lo = next(iter(parts[82])), next(iter(parts[84]))
            values.setdefault(ref, set()).add(((hi << 16) + lo - (0x10000 if lo & 0x8000 else 0)) & 0xffffffff)
        else:
            values.setdefault(ref, set()).add(None)  # unpaired or inconsistent halves
    return values


def generated_placements(item, asm_text, raw, contract, layout):
    """Place compiler-generated data (string pools, jump tables, constants) whose retail address
    is fixed by relocations from bytes this unit already places (code operands, and the
    words of data definitions such as string-literal initializers).

    An item is placed when every observed relocation into it implies the same address, that
    address lies in the layout region of the item's section, and no other placement covers
    any of its bytes. Returns (placements, findings); findings list every item left to the
    fallback with the reason, and every disagreement with an existing data definition.
    """
    load, header = num(contract["load_address"]), contract["header_size"]
    gp = num(layout["gp"])
    obj = item.obj
    sids = {name: sid for sid, name in obj.sections.items() if name in DATA_SECTIONS}
    present = {name: len(obj.code.get(sid, b"")) for name, sid in sids.items() if obj.code.get(sid)}
    defined = [p for p in item.placements if p.symbol not in item.spec["functions"]]
    if not present:
        return [], []
    items = asm_data_items(asm_text)
    try:
        data_extent.check_sizes(items, present)
    except data_extent.ExtentError as exc:
        return [], [{"unit": item.unit, "status": "unsized", "reason": str(exc)}] + [
            {"unit": item.unit, "status": "extent", "label": p.symbol, "reason": "cannot be sized"}
            for p in defined]
    # A data definition places exactly one item, without the padding after it (data_check.py
    # applies the same rule through the same sizer).
    extent_findings = []
    for p in defined:
        try:
            data_extent.definition_extent(items, p.symbol, obj.sections[p.section], p.offset, p.size)
        except data_extent.ExtentError as exc:
            extent_findings.append({"unit": item.unit, "status": "extent", "label": p.symbol,
                                    "reason": str(exc)})
    named = {(sid, offset): name for name, sid, offset in list(obj.xdefs) + list(obj.locals)}
    observed = observed_targets(item, raw, load, header, gp)
    placed, findings = [], extent_findings
    for section, state in items.items():
        sid = sids[section]
        for entry in state["items"]:
            start, size = entry["offset"], entry["size"]
            if not size:
                continue
            label = entry["label"] or f"{section}+{start:#x}"
            covering = [p for p in item.placements
                        if p.section == sid and p.offset < start + size and start < p.offset + p.size]
            implied = {value - (c - start) if value is not None else None
                       for (rsid, c), vals in observed.items() if rsid == sid and start <= c < start + size
                       for value in vals}
            record = {"unit": item.unit, "section": section, "offset": hex(start), "size": size, "label": label}
            if covering:
                whole = len(covering) == 1 and covering[0].offset == start and covering[0].size == size
                address = covering[0].address
                if implied and (None in implied or implied != {address} or not whole):
                    findings.append({**record, "status": "conflict",
                                     "reason": f"relocations imply {sorted(h for h in map(hex, implied - {None}))} "
                                               f"but {covering[0].symbol} is placed at {address:#x}"})
                elif implied:
                    findings.append({**record, "status": "confirmed", "symbol": covering[0].symbol,
                                     "reason": f"relocations from placed bytes agree with {covering[0].symbol} "
                                               f"at {address:#x}"})
                continue
            if (sid, start) in named:
                findings.append({**record, "status": "named", "reason": "named datum without a data definition"})
                continue
            if not implied:
                findings.append({**record, "status": "unanchored", "reason": "no relocation from placed bytes"})
                continue
            if None in implied or len(implied) != 1:
                findings.append({**record, "status": "ambiguous",
                                 "reason": f"relocations imply {sorted(hex(v) for v in implied if v is not None)}"
                                           + (" and unpaired HI16/LO16 halves" if None in implied else "")})
                continue
            address = next(iter(implied))
            regions = {section_layout.section_of(layout, a) for a in (address, address + size - 1)}
            if regions != {section}:
                findings.append({**record, "status": "outside_section",
                                 "reason": f"implied address {address:#x} is not inside the retail {section}"})
                continue
            placed.append(Placement("generated", item.unit, label, address, size, sid, start))
    return placed, findings


class Linker:
    """Places object bytes at retail addresses and resolves their relocations."""

    def __init__(self, contract, raw):
        self.header = contract["header_size"]
        self.load = num(contract["load_address"])
        self.size = contract["payload_size"]
        self.image = bytearray(raw[:self.header]) + bytearray(raw[self.header:])
        self.owners = [{"kind": "fallback", "unit": None, "symbol": "header"},
                       {"kind": "fallback", "unit": None, "symbol": None}]
        self.provenance = array("I", [0]) * self.header + array("I", [1]) * self.size
        self.symbols = {}

    def define(self, name, address, origin):
        previous = self.symbols.get(name)
        if previous and previous[0] != address:
            raise GateError(f"symbol {name} is {previous[0]:#x} in {previous[1]} and {address:#x} in {origin}")
        self.symbols.setdefault(name, (address, origin))

    def build_symbols(self, units):
        # Placed definitions first: the link's own record of where it put them. Only names the
        # object exports are global; compiler-local labels ($LC0, $L5), statics (functions and
        # data with a local-symbol record) and unexported data definitions are per object,
        # resolved through the unit's own placements and sections, and never bound or compared
        # across units.
        for item in units:
            exported = {name for name, _, _ in item.obj.xdefs}
            for p in item.placements:
                if p.symbol in exported:
                    self.define(p.symbol, p.address, f"{item.unit} placement")
        for item in units:
            for name, value in item.spec["functions"].items():
                if not lnk_symbols.is_static(value):
                    self.define(name, num(value["address"]), f"{item.unit} map")
            for name, value in item.spec.get("symbols", {}).items():
                self.define(name, num(value), f"{item.unit} map")

    def section_address(self, item, sid, offset):
        name = item.obj.sections[sid]
        for p in item.placements:
            if p.section == sid and p.offset <= offset < p.offset + p.size:
                return p.address + offset - p.offset
        if name == ".text" and offset == len(item.obj.code.get(sid, b"")):
            # An end label, one past the last .text byte: the end of the function placed last.
            for p in item.placements:
                if p.section == sid and p.kind != "data" and p.offset + p.size == offset:
                    return p.address + p.size
        if name != ".text":
            base = item.spec.get("sections", {}).get(name)
            if base is not None:
                return num(base) + offset
        raise GateError(f"{item.unit}: no placement for {name}+{offset:#x}")

    def section_start(self, item, sid):
        name = item.obj.sections[sid]
        start = item.spec.get("section_starts", {}).get(name)
        if start is None and name == ".sdata":
            start, base = item.spec.get("gp"), item.spec.get("sections", {}).get(name)
            if start is not None and base is not None and num(start) != num(base):
                raise GateError(f"{item.unit}: .sdata GP differs from its base; add section_starts")
        if start is None:
            raise GateError(f"{item.unit}: no start for section {name}")
        return num(start)

    def evaluate(self, item, expr):
        """Return an int, or (section id, offset) for an object-relative value."""
        tag = expr[0]
        if tag == "c":
            return expr[1]
        if tag == 2 and expr[1] in item.obj.xrefs:
            name = item.obj.xrefs[expr[1]]
            if name not in self.symbols:
                raise GateError(f"{item.unit}: unresolved symbol {name}")
            return self.symbols[name][0]
        if tag == 4 and expr[1] in item.obj.sections:
            return (expr[1], 0)
        if tag == 12 and expr[1] in item.obj.sections:
            return self.section_start(item, expr[1])
        if tag in (44, 46):
            # PsyQ serializes the right operand first.
            right, left = self.evaluate(item, expr[1]), self.evaluate(item, expr[2])
            if tag == 44:
                if isinstance(left, tuple) and isinstance(right, tuple):
                    raise GateError(f"{item.unit}: sum of two section-relative values")
                if isinstance(left, tuple):
                    return (left[0], left[1] + right)
                if isinstance(right, tuple):
                    return (right[0], right[1] + left)
                return (left + right) & 0xffffffff
            if isinstance(right, tuple):
                if isinstance(left, tuple) and left[0] == right[0]:
                    return left[1] - right[1]
                right = self.section_address(item, *right)
            if isinstance(left, tuple):
                # section+offset minus an absolute value (GP-relative: target - sectstart(.sdata)) is
                # the target's placed address minus it, never base+offset-value: the offset must be
                # resolved through the item that holds it before the subtraction.
                left = self.section_address(item, *left)
            return (left - right) & 0xffffffff
        raise GateError(f"{item.unit}: unsupported relocation expression {expr!r}")

    def resolve(self, item, expr):
        value = self.evaluate(item, expr)
        if isinstance(value, tuple):
            value = self.section_address(item, *value)
        return value & 0xffffffff

    def relocated(self, item, p):
        code = bytes(item.obj.code.get(p.section, b""))
        if p.offset + p.size > len(code):
            raise GateError(f"{item.unit}: {p.symbol} extends past its section")
        data = bytearray(code[p.offset:p.offset + p.size])
        patches = item.obj.patches.get(p.section, [])
        unpaired = unpaired_split_halves(patches, p.offset, p.offset + p.size)
        if unpaired:
            raise GateError(f"{item.unit}: split HI16/LO16 halves of {unpaired} in {p.symbol} have no partner there")
        for kind, offset, expr in patches:
            if not p.offset <= offset < p.offset + p.size:
                continue
            kind, expr = split_relocation(kind, expr)
            rel = offset - p.offset
            if rel + 4 > p.size:
                raise GateError(f"{item.unit}: relocation crosses the end of {p.symbol}")
            word = struct.unpack_from("<I", data, rel)[0]
            target = self.resolve(item, expr)
            if kind == 16:
                word = target
            elif kind == 82:
                word = (word & 0xffff0000) | (((target + 0x8000) >> 16) & 0xffff)
            elif kind == 84:
                word = (word & 0xffff0000) | (target & 0xffff)
            elif kind == 74:
                place = p.address + rel
                if target & 3 or (target ^ (place + 4)) & 0xf0000000:
                    raise GateError(f"{item.unit}: MIPS26 target {target:#x} unreachable from {place:#x}")
                word = (word & 0xfc000000) | ((target >> 2) & 0x03ffffff)
            elif kind == 30:
                delta = target - 0x100000000 if target & 0x80000000 else target
                if not -32768 <= delta <= 32767:
                    raise GateError(f"{item.unit}: GP-relative value {delta} out of range in {p.symbol}")
                word = (word & 0xffff0000) | (delta & 0xffff)
            else:
                raise GateError(f"{item.unit}: unsupported relocation type {kind} in {p.symbol}")
            struct.pack_into("<I", data, rel, word)
        return data

    def place(self, item, p):
        if p.kind in verify.asm_units.CATEGORIES:
            text = item.obj.sections[p.section] == ".text"
            if not verify.asm_units.inside(p.address, p.size, p.kind, text):
                raise GateError(verify.asm_units.describe(p.unit, p.symbol, p.address, p.size, p.kind, text))
        start = self.header + p.address - self.load
        if p.address < self.load or start + p.size > self.header + self.size:
            raise GateError(f"{item.unit}: {p.symbol} at {p.address:#x} lies outside the payload")
        occupied = {self.provenance[i] for i in range(start, start + p.size)} - {1}
        if occupied:
            other = self.owners[min(occupied)]
            raise GateError(f"{item.unit}: {p.symbol} overlaps {other['unit']}:{other['symbol']}")
        owner = len(self.owners)
        self.owners.append({"kind": p.kind, "unit": p.unit, "symbol": p.symbol,
                            "address": hex(p.address), "size": p.size,
                            "section": item.obj.sections[p.section], "offset": hex(p.offset)})
        self.image[start:start + p.size] = self.relocated(item, p)
        self.provenance[start:start + p.size] = array("I", [owner]) * p.size

    def link(self, units):
        self.build_symbols(units)
        for item in units:
            for p in item.placements:
                self.place(item, p)
        return self.image

    def counts(self):
        totals = {}
        for index in self.provenance:
            kind = self.owners[index]["kind"]
            totals[kind] = totals.get(kind, 0) + 1
        return totals

    def runs(self):
        out, start, prov = [], 0, self.provenance
        for i in range(1, len(prov) + 1):
            if i == len(prov) or prov[i] != prov[start]:
                owner = self.owners[prov[start]]
                out.append({"file_offset": start, "size": i - start,
                            "address": hex(self.load + start - self.header) if start >= self.header else None,
                            "kind": owner["kind"], "unit": owner["unit"], "symbol": owner["symbol"]})
                start = i
        return out

    def attribute(self, raw):
        """Group every byte that differs from the original by the owner that supplied it."""
        groups = {}
        for i, (a, b) in enumerate(zip(self.image, raw)):
            if a != b:
                groups.setdefault(self.provenance[i], []).append(i)
        return [(self.owners[k], offsets) for k, offsets in sorted(groups.items())]


# -------------------------------------------------------------- cross-check

def cross_check(linker, report, objects, current, raw=None):
    """Compare the link's provenance with verify.py's per-function results, and the
    compiler-generated data claims with tools/generated_witness.py."""
    problems = []
    if report.get("reference_sha256") != current["reference_sha256"]:
        problems.append("verify report was made against a different original")
    if report.get("data_manifest_sha256") != current["data_manifest_sha256"]:
        problems.append("verify report used a different config/data.json")
    if sorted(report.get("units", [])) != sorted(current["units"]):
        problems.append("verify report covers a different unit list")
    by_unit = {}
    for check in report.get("checks", []) + report.get("data_checks", []):
        by_unit.setdefault(check["unit"], set()).add(
            (check.get("source_sha256"), check.get("map_sha256"), check.get("object_file_sha256")))
    for unit, digests in sorted(current["unit_digests"].items()):
        if by_unit.get(unit, {digests}) != {digests}:
            problems.append(f"{unit}: verify report is stale or compiled a different object")
    asm_kinds = verify.asm_units.CATEGORIES
    matched = {(c["unit"], c["function"], num(c["address"]), int(c["size"]))
               for c in report.get("checks", []) if c.get("status") == "match" and c.get("kind") not in asm_kinds}
    for kind in asm_kinds:
        problems += verify.asm_units.cross_check(
            linker.owners, report, {u: o.spec for u, o in objects.items()},
            [u for u in current["units"] if verify.asm_category(u) == kind], kind)
    linked = {(o["unit"], o["symbol"], num(o["address"]), o["size"])
              for o in linker.owners if o["kind"] in ("function", "verbatim") and o["unit"]
              and o["symbol"] in objects[o["unit"]].spec["functions"]}
    for extra in sorted(linked - matched):
        problems.append(f"linked function {extra[0]}:{extra[1]} is not matched by verify.py")
    for extra in sorted(matched - linked):
        problems.append(f"verify.py matched {extra[0]}:{extra[1]} but the link did not place it")
    verified_data = {(c["unit"], d["symbol"], num(d["address"]), int(d["size"]))
                     for c in report.get("data_checks", []) if c.get("status") == "match" and c.get("kind") not in asm_kinds
                     for d in c.get("definitions", [])}
    linked_data = {(o["unit"], o["symbol"], num(o["address"]), o["size"])
                   for o in linker.owners if o["kind"] in ("data", "verbatim") and o["unit"]
                   and o["symbol"] not in objects[o["unit"]].spec["functions"]}
    for extra in sorted(linked_data - verified_data):
        problems.append(f"linked datum {extra[0]}:{extra[1]} is not verified by verify.py")
    for extra in sorted(verified_data - linked_data):
        problems.append(f"verify.py verified datum {extra[0]}:{extra[1]} but the link did not place it")
    counts = linker.counts()
    linked_bytes = counts.get("function", 0) + counts.get("data", 0) + counts.get("verbatim", 0)
    verified_bytes = report.get("byte_matched_function_bytes", 0) + report.get("byte_matched_initialized_data_bytes", 0)
    if linked_bytes != verified_bytes:
        problems.append(f"link places {linked_bytes} source bytes, verify.py verified {verified_bytes}")
    if not report.get("all_selected_checks_pass"):
        problems.append("verify.py reports failing checks")
    claims = [{**o, "label": o["symbol"]} for o in linker.owners if o["kind"] == "generated"]
    if claims:
        witnessed, issues = generated_witness.witness(claims, report, raw, linker.load, linker.header)
        problems += [f"generated-data witness: {issue}" for issue in issues]
        if witnessed != counts.get("generated", 0):
            problems.append(f"link places {counts.get('generated', 0)} generated-data bytes, "
                            f"the witness accepts {witnessed}")
    return problems


def conformance(linker, layout):
    """Placements whose object section is not the retail section at their address."""
    out = []
    for o in linker.owners[2:]:
        start, end = num(o["address"]), num(o["address"]) + o["size"] - 1
        retail = {section_layout.section_of(layout, start), section_layout.section_of(layout, end)}
        if retail != {o["section"]}:
            out.append({"unit": o["unit"], "symbol": o["symbol"], "kind": o["kind"], "address": o["address"],
                        "size": o["size"], "object_section": o["section"],
                        "retail_section": sorted(map(str, retail))})
    return out


# --------------------------------------------------------------------- main

def select_units(names):
    every = verify.UNITS + verify.ASM_UNITS + verify.LIBRARY_ASM_UNITS
    entries = [entry for entry in every if names is None or entry[0] in names]
    unknown = set(names or ()) - {entry[0] for entry in every}
    if unknown:
        raise GateError(f"unknown units: {sorted(unknown)}")
    if not entries:
        raise GateError("empty work list: no registered unit selected")
    try:
        verify.validate_asm_registry()
        verify.validate_function_ownership(entries)
        verify.validate_symbol_bindings(entries)
    except RuntimeError as exc:
        raise GateError(str(exc))
    return entries


def run(args):
    started = time.monotonic()
    contract, raw = load_contract(args.contract)
    if not args.allow_dirty:
        dirty = dirty_paths(PROJECT)
        if dirty:
            raise GateError(f"refusing to gate a dirty tree: {', '.join(dirty[:10])}"
                            + (" ..." if len(dirty) > 10 else ""))
    out = args.output.resolve()
    exe = out / contract["binary"]
    remove_tree(out)
    if exe.exists():
        raise GateError(f"could not clear {out}")
    out.mkdir(parents=True)
    CHILDREN.stopped = False
    try:
        return _run(args, started, contract, raw, out, exe)
    finally:
        CHILDREN.kill_all()  # also on KeyboardInterrupt: nothing may outlive the gate
        CHILDREN.stopped = False


def remove_tree(path):
    """Delete a directory tree, retrying briefly (Windows releases killed processes' handles late)."""
    def writable(func, target, _exc):
        os.chmod(target, 0o700)
        func(target)
    for attempt in range(10):
        if not path.exists():
            return
        try:
            shutil.rmtree(path, onerror=writable)
        except OSError:
            time.sleep(0.5 * (attempt + 1))
    if path.exists():
        raise GateError(f"could not delete {path}; a process may still hold it open")


def _run(args, started, contract, raw, out, exe):
    linker = Linker(contract, raw)
    objects, verifier, findings, layout = {}, None, [], None
    if args.base_case:
        units = []
    else:
        units = select_units(args.units)
        data_manifest = DATA_MANIFEST.read_bytes()
        data_defs = json.loads(data_manifest)
        if args.verify_report is None:
            verify_out = out / "verify.json"
            log = open(out / "verify.log", "w")
            CHILDREN.files.append(log)
            verifier = CHILDREN.start(
                [sys.executable, str(TOOLS / "verify.py"), "--units", *[u for u, _, _ in units],
                 "--build-root", str(out / "verify-builds"), "--output", str(verify_out),
                 "--jobs", str(args.jobs)],
                stdout=log, stderr=subprocess.STDOUT, cwd=WORKSPACE)
        layout = section_layout.load_layout()
        if layout["reference_sha256"] != contract["sha256"]:
            raise GateError("config/section_layout.json belongs to a different original")
        (out / "objects").mkdir()
        pool = ThreadPoolExecutor(max_workers=args.jobs)
        try:
            built = list(pool.map(lambda e: build_unit(e, out / "objects"), units))
        except BaseException:
            # Stop queued units, kill running builds and the verifier, then release the pool.
            pool.shutdown(wait=False, cancel_futures=True)
            CHILDREN.kill_all()
            pool.shutdown(wait=True)  # workers refused new children while stopped, so this returns
            raise
        pool.shutdown()
        for (unit, source, mapping), (_, object_path, asm_path) in zip(units, built):
            spec = json.loads(mapping.read_text(encoding="utf-8"))
            listing = asm_path.read_text(encoding="latin1")
            if verify.is_asm_unit(unit):
                # Assembly: no compiler-generated data to place, and nothing is "verbatim"; the
                # whole unit is of its category ("asm" or "library_asm") and counted as such.
                objects[unit] = asm_unit_object(unit, object_path, spec, data_defs.get(unit, {}), listing,
                                                verify.asm_category(unit))
            else:
                objects[unit] = unit_object(unit, object_path, spec, data_defs.get(unit, {}),
                                            verbatim_symbols(listing), listing=listing)
                extra, found = generated_placements(objects[unit], listing, raw, contract, layout)
                objects[unit].placements += extra
                findings += found
            objects[unit].digests = (sha256(source.read_bytes()), sha256(mapping.read_bytes()),
                                     sha256(object_path.read_bytes()))
        linker.link(list(objects.values()))
    exe.write_bytes(linker.image)
    counts = linker.counts()
    source_bytes = counts.get("function", 0) + counts.get("data", 0) + counts.get("generated", 0)
    record = {"contract": contract, "owners": linker.owners, "counts": counts,
              "source_bytes": source_bytes, "asm_bytes": counts.get("asm", 0),
              "library_asm_bytes": counts.get("library_asm", 0),
              "generated_data_findings": findings,
              "section_conformance": conformance(linker, layout) if layout else [],
              "runs": linker.runs()}
    (out / "provenance.json").write_text(json.dumps(record) + "\n", encoding="utf-8")
    failures = []
    digest = sha256(exe.read_bytes())
    if digest != contract["sha256"]:
        failures.append(f"sha256 {digest} differs from the contract {contract['sha256']}")
        for owner, offsets in linker.attribute(raw):
            first = offsets[0] - linker.header + linker.load if offsets[0] >= linker.header else None
            where = f"{owner['kind']} {owner['unit'] or 'original'}:{owner['symbol'] or 'payload'}"
            failures.append(f"  {len(offsets)} differing bytes from {where}"
                            + (f", first at {first:#010x}" if first is not None else
                               f", first at file offset {offsets[0]:#x}"))
    if not args.base_case:
        if verifier is not None:
            code = verifier.wait()
            CHILDREN.finish(verifier)
            log.close()
            report_path = out / "verify.json"
            if not report_path.is_file():
                raise GateError(f"verify.py exited {code} without a report; see {out / 'verify.log'}")
        else:
            report_path = args.verify_report
        report = json.loads(report_path.read_text(encoding="utf-8"))
        current = {"reference_sha256": contract["sha256"],
                   "data_manifest_sha256": sha256(DATA_MANIFEST.read_bytes()),
                   "units": [u for u, _, _ in units],
                   "unit_digests": {u: o.digests for u, o in objects.items()}}
        for problem in cross_check(linker, report, objects, current, raw):
            failures.append(f"cross-check: {problem}")
    for finding in findings:
        if finding["status"] in ("conflict", "extent"):
            failures.append(f"generated data: {finding['unit']}:{finding['label']} {finding['reason']}")
    left = [f for f in findings if f["status"] in ("ambiguous", "outside_section", "unsized")]
    for finding in left:
        print(f"generated data left to the fallback ({finding['status']}): {finding['unit']}:"
              f"{finding.get('label', '')} {finding['reason']}")
    elapsed = time.monotonic() - started
    if failures:
        print(f"GATE FAILED ({elapsed:.1f}s)", file=sys.stderr)
        for line in failures:
            print(line, file=sys.stderr)
        return 1
    detail = (f"sha256={digest} source_bytes={source_bytes}/{contract['payload_size']} "
              f"verbatim_bytes={counts.get('verbatim', 0)} generated_bytes={counts.get('generated', 0)} "
              f"asm_bytes={counts.get('asm', 0)} library_asm_bytes={counts.get('library_asm', 0)} "
              f"units={len(objects)} wall={elapsed:.1f}s")
    if args.allow_dirty and dirty_paths(PROJECT):
        print(f"GATE UNBANKED (dirty tree) {detail}")
        return 2
    print(f"GATE OK {detail}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--output", type=Path, default=PROJECT / "builds/gate",
                    help="output directory; deleted and recreated on every run")
    ap.add_argument("--contract", type=Path, default=CONTRACT)
    ap.add_argument("--units", nargs="+", help="restrict the link to these registered units")
    ap.add_argument("--jobs", type=int, default=min(12, os.cpu_count() or 1))
    ap.add_argument("--verify-report", type=Path,
                    help="cross-check against this verify.py report instead of running verify.py")
    ap.add_argument("--base-case", action="store_true",
                    help="link from original bytes only (proves the fallback reproduces the hash)")
    ap.add_argument("--allow-dirty", action="store_true",
                    help="development only: run on a dirty tree; never prints GATE OK and exits 2")
    args = ap.parse_args(argv)
    try:
        return run(args)
    except (GateError, OSError, ValueError, KeyError) as exc:
        print(f"GATE FAILED error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
