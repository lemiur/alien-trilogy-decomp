"""Recover the retail section layout and the uninitialised-symbol layout of SLUS_000.07.

    python tools/section_layout.py            # re-derive, compare with config, write reports
    python tools/section_layout.py --write    # re-derive and (re)write the two config files

Outputs are tool inputs, ``config/section_layout.json`` and ``config/bss_layout.json``,
and the report ``reports/section_layout.json``. The default mode re-derives every
boundary and every BSS entry from evidence and exits non-zero when the result differs
from the config, so a config edit without evidence, or an evidence change that moves a
boundary, cannot pass silently.

Evidence, strongest first:
- the PS-X EXE header (load address, payload size, the bss fields, which are zero);
- SN's startup module 2MBYTE.OBJ (Runtime Library 3.3): its relocations name the
  linker-wide ``sectstart(.sdata)`` (loaded into $gp), ``sectstart(.sbss)`` (start of
  the zero-clear loop) and ``sectend(.bss)`` (end of the loop, heap base). The retail
  operands at the certified 2MBYTE address give their values. The object's own text
  must equal the retail bytes outside the relocated fields;
- certified Runtime Library sections (``reports/runtime_data.json`` full matches) and
  section bases and COMMON (XBSS) addresses resolved from certified library relocations
  (``analysis/runtime_linked.json``); sizes of library .sbss/.bss sections and COMMON
  symbols come from the library objects themselves;
- the unit maps: matched function addresses (.text) and referenced global addresses;
- retail operand pairs (``lui`` + same-register low half) are "observed" evidence: they
  support a boundary but never pin one.

A boundary is "pinned" when both sides are evidenced, "bounded" when evidence gives an
interval, and a region is "unknown" when its section is not evidenced. Nothing is
interpolated: an interval is reported as an interval.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import ntpath
from pathlib import Path
import struct
import sys

PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parent
sys.path.insert(0, str(WORKSPACE / "shared-tools"))
import psyqobj  # noqa: E402
sys.path.insert(0, str(PROJECT / "tools"))
import lnk_symbols  # noqa: E402

SECTION_CONFIG = PROJECT / "config/section_layout.json"
BSS_CONFIG = PROJECT / "config/bss_layout.json"
REPORT = PROJECT / "reports/section_layout.json"
RUNTIME_DATA = PROJECT / "reports/runtime_data.json"
RUNTIME_LINKED = PROJECT / "analysis/runtime_linked.json"
LIBRARY_DIR = ("sdk/extracted/Programmer Tool - Runtime Library Version 3.3 (Japan)_DTL-S2190_redump/"
               "LZH/PSX1020.LZH/PSX/LIB")
HEADER = 0x800
SECTION_ORDER = [".rdata", ".text", ".data", ".sdata", ".sbss", ".bss"]


class LayoutError(Exception):
    pass


def h(value):
    return hex(value) if value is not None else None


def num(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


# ------------------------------------------------------------- LNK records

def lnk_records(data):
    """XBSS (COMMON) and local-symbol records, which psyqobj does not keep (tools/lnk_symbols.py).

    Returns {"xbss": {number: (section id, size, name)}, "locals": [(section, offset, name)]}.
    """
    try:
        return lnk_symbols.records(data)
    except lnk_symbols.SymbolError as exc:
        raise LayoutError(str(exc))


def library_modules(path):
    """[(module name, parsed object, raw LNK bytes)] of a PsyQ LIB archive or single object."""
    data = Path(path).read_bytes()
    if data[:4] == b"LNK\x02":
        return [(ntpath.basename(str(path)), psyqobj.parse_lnk(data, str(path)), data)]
    if data[:4] != b"LIB\x01":
        raise LayoutError(f"{path} is not a PsyQ library")
    out, i = [], 4
    while i < len(data):
        name = data[i:i + 8].decode("latin1").strip()
        _, header, total = struct.unpack_from("<III", data, i + 8)
        body = data[i + header:i + total]
        out.append((name, psyqobj.parse_lnk(body, name), body))
        i += total
    return out


# ---------------------------------------------------------------- evidence

class Evidence:
    def __init__(self):
        contract = json.loads((PROJECT / "config/gate.json").read_text(encoding="utf-8"))
        lock = json.loads((PROJECT / "config/toolchain.lock.json").read_text(encoding="utf-8"))
        self.raw = (WORKSPACE / contract["reference"]).read_bytes()
        if sha256(self.raw) != lock["reference_sha256"] or contract["sha256"] != lock["reference_sha256"]:
            raise LayoutError("reference executable does not have the locked hash")
        self.reference_sha256 = lock["reference_sha256"]
        if self.raw[:8] != b"PS-X EXE":
            raise LayoutError("reference is not a PS-X EXE")
        self.entry, self.header_gp, self.load, self.size = struct.unpack_from("<4I", self.raw, 0x10)
        self.header_bss = struct.unpack_from("<II", self.raw, 0x28)
        if HEADER + self.size != len(self.raw):
            raise LayoutError("payload size disagrees with the file size")
        self.payload_end = self.load + self.size
        self.runtime_data = json.loads(RUNTIME_DATA.read_text(encoding="utf-8"))
        self.runtime_linked = json.loads(RUNTIME_LINKED.read_text(encoding="utf-8"))
        for report in (self.runtime_data, self.runtime_linked):
            digest = report.get("executable_sha256") or report.get("inputs", {}).get("executable_sha256")
            if digest != self.reference_sha256:
                raise LayoutError("runtime report was made against a different executable")
        self._libraries = {}

    def word(self, address):
        offset = address - self.load
        if not 0 <= offset <= self.size - 4:
            raise LayoutError(f"{address:#x} lies outside the payload")
        return struct.unpack_from("<I", self.raw, HEADER + offset)[0]

    def module(self, module_key):
        """(parsed object, records) of a module key 'library::NAME@address'."""
        library, rest = module_key.split("::", 1)
        name = rest.rsplit("@", 1)[0]
        path = WORKSPACE / library.replace("\\", "/")
        if path not in self._libraries:
            self._libraries[path] = library_modules(path)
        hits = [(o, body) for n, o, body in self._libraries[path] if n == name]
        if len(hits) != 1:
            raise LayoutError(f"{module_key}: {len(hits)} modules of that name")
        obj, body = hits[0]
        return obj, lnk_records(body)

    # --- startup -------------------------------------------------------
    def startup(self):
        rows = [m for m in self.runtime_linked["module_results"]
                if m["module"] == "2MBYTE.OBJ" and m["status"] == "full_match"]
        if len(rows) != 1:
            raise LayoutError("2MBYTE.OBJ is not certified exactly once")
        address = num(rows[0]["address"])
        library = rows[0]["library"]
        obj, _ = self.module(f"{library}::2MBYTE.OBJ@{address:x}")
        text = next(s for s, n in obj.sections.items() if n == ".text")
        code = bytes(obj.code[text])
        patched = {off for _, off, _ in obj.patches.get(text, [])}
        for off in range(0, len(code), 4):
            mask = 0xffff0000 if off in patched else 0xffffffff
            if off in patched and any(k == 74 for k, o, _ in obj.patches[text] if o == off):
                mask = 0xfc000000
            if (struct.unpack_from("<I", code, off)[0] ^ self.word(address + off)) & mask:
                raise LayoutError(f"2MBYTE.OBJ text differs from the retail bytes at +{off:#x}")
        values, sites = {}, {}
        for kind, off, expr in obj.patches.get(text, []):
            if expr[0] not in (12, 22) or kind not in (82, 84):
                continue
            key = ("sectstart" if expr[0] == 12 else "sectend", obj.sections[expr[1]])
            part = self.word(address + off) & 0xffff
            values.setdefault(key, {}).setdefault(kind, set()).add(part)
            sites.setdefault(key, []).append(hex(address + off))
        out = {}
        for key, parts in values.items():
            if len(parts.get(82, ())) != 1 or len(parts.get(84, ())) != 1:
                raise LayoutError(f"startup {key} has no unique HI16/LO16 operand pair")
            hi, lo = next(iter(parts[82])), next(iter(parts[84]))
            out[key] = ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xffffffff
        need = {("sectstart", ".sdata"), ("sectstart", ".sbss"), ("sectend", ".bss")}
        if not need <= set(out):
            raise LayoutError("startup module no longer names the expected section bounds")
        # The loop and $gp setup that consume the values (startup_layout.py checks their shape).
        gp_hi, gp_lo = self.word(address + 0x88), self.word(address + 0x8c)
        if gp_hi >> 16 != 0x3c1c or gp_lo >> 16 != 0x279c:
            raise LayoutError("startup no longer loads $gp from sectstart(.sdata)")
        return {"address": address, "values": out, "sites": sites, "library": library}

    # --- certified library sections ----------------------------------------
    def library_sections(self):
        """Initialized sections with certified bytes: [(name, address, size, module)]."""
        rows = []
        for row in (self.runtime_data["section_results"]
                    + self.runtime_data.get("supplemental_data_only_section_results", [])):
            if row["status"] == "full_match" and row.get("address") and row["size"]:
                rows.append((row["section"], num(row["address"]), int(row["size"]), row["module"]))
        return sorted(rows, key=lambda r: r[1])

    def library_text(self):
        rows = [(num(m["address"]), int(m["text_size"]), m["module"]) for m in self.runtime_linked["module_results"]
                if m["status"] == "full_match" and m.get("address")]
        return sorted(rows)

    def library_uninitialized(self):
        """Library .sbss/.bss section bases and COMMON symbols resolved from certified relocations."""
        sections, commons = [], []
        for binding in self.runtime_linked["symbol_bindings"]:
            if binding.get("status") != "resolved" or binding["kind"] not in ("section_base", "local_symbol"):
                continue
            module_key, index = binding["name_or_section"]
            module = module_key.split("::", 1)[1]
            address = num(binding["address"])
            obj, records = self.module(module_key)
            if binding["kind"] == "section_base":
                name = obj.sections.get(index)
                if name in (".sbss", ".bss"):
                    sections.append({"kind": "library_section", "section": name, "address": address,
                                     "size": len(obj.code.get(index, b"")), "owner": module,
                                     "evidence": f"certified relocation section base of {module} {name}"})
            elif index in records["xbss"]:
                section, size, name = records["xbss"][index]
                commons.append({"kind": "library_common", "name": name, "address": address,
                                "declared_section": obj.sections.get(section), "declared_size": size,
                                "referenced_by": module})
        return sections, commons


def common_sizes(evidence, names):
    """Largest XBSS size per name over every Runtime Library 3.3 module (the linker keeps the max),
    and the names some module defines outright (then the symbol is not COMMON)."""
    sizes, defined = {}, set()
    for path in sorted((WORKSPACE / LIBRARY_DIR).glob("*.LIB")):
        for _, obj, body in library_modules(path):
            for name, _, _ in obj.xdefs:
                if name in names:
                    defined.add(name)
            for _, size, name in lnk_records(body)["xbss"].values():
                if name in names:
                    sizes[name] = max(size, sizes.get(name, 0))
    return sizes, defined


def map_entries():
    """Unit maps: matched functions, referenced data ({address: {name: [units]}}: globals, and
    initialised statics, whose data definitions verify.py checks at that address) and the units'
    uninitialised statics ([(address, size, section, unit, name)], from ``static_data``;
    verify.py and the gate audit each entry against the unit's object, tools/lnk_symbols.py)."""
    sys.path.insert(0, str(PROJECT / "tools"))
    import verify  # noqa: E402
    functions, symbols, statics = [], {}, []
    for unit, _, mapping in verify.UNITS:
        spec = json.loads(Path(mapping).read_text(encoding="utf-8"))
        for name, f in spec["functions"].items():
            functions.append((num(f["address"]), int(f["size"]), unit, name))
        for name, value in spec.get("symbols", {}).items():
            address = num(value["address"] if isinstance(value, dict) else value)
            symbols.setdefault(address, {}).setdefault(name, []).append(unit)
        for name, value in spec.get("static_data", {}).items():
            if value["section"] in lnk_symbols.INITIALIZED:
                symbols.setdefault(num(value["address"]), {}).setdefault(name, []).append(unit)
            else:
                statics.append((num(value["address"]), int(value["size"]), value["section"], unit, name))
    return sorted(functions), symbols, sorted(statics)


def operand_pairs(evidence, start, end, targets):
    """Retail lui/low-half pairs in [start, end) whose combined value is in targets (observed only)."""
    loads = {0x09, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2a, 0x2b, 0x2e, 0x32, 0x3a}
    hits = {}
    for a in range(start, end, 4):
        w = evidence.word(a)
        if w >> 26 != 0x0f:
            continue
        rt, hi = (w >> 16) & 31, w & 0xffff
        for b in range(a + 4, min(a + 40, end), 4):
            y = evidence.word(b)
            if y >> 26 in loads and (y >> 21) & 31 == rt:
                lo = y & 0xffff
                value = ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xffffffff
                if value in targets:
                    hits.setdefault(value, []).append(hex(b))
    return hits


# ------------------------------------------------------------- derivation

def region(name, section, start, end, status, evidence):
    """status is "unknown" for an unevidenced section; otherwise it follows from the bounds."""
    start, end = [v[0] if isinstance(v, tuple) and v[0] == v[1] else v for v in (start, end)]
    if status != "unknown":
        status = "bounded" if isinstance(start, tuple) or isinstance(end, tuple) else "pinned"
    out = {"name": name, "section": section, "status": status, "evidence": evidence}
    for key, value in (("start", start), ("end", end)):
        if isinstance(value, tuple):
            out[key] = {"min": h(value[0]), "max": h(value[1])}
        else:
            out[key] = h(value)
    return out


def derive(ev=None):
    ev = ev or Evidence()
    startup = ev.startup()
    sdata_start = startup["values"][("sectstart", ".sdata")]
    sbss_start = startup["values"][("sectstart", ".sbss")]
    bss_end = startup["values"][("sectend", ".bss")]
    lib_sections = ev.library_sections()
    lib_text = ev.library_text()
    functions, symbols, statics = map_entries()
    by_name = {}
    for name, address, size, module in lib_sections:
        by_name.setdefault(name, []).append((address, address + size, module))
    rdata_lib = sorted(by_name.get(".rdata", []))
    data_lib = sorted(by_name.get(".data", []))
    sdata_lib = sorted(by_name.get(".sdata", []))
    text_lo = functions[0][0]
    text_lib_end = max(a + n for a, n, _ in lib_text)
    text_hi = max(max(a + n for a, n, _, _ in functions), text_lib_end)
    rdata_end = max(e for _, e, _ in rdata_lib)
    sdata_lib_end = max(e for _, e, _ in sdata_lib)
    data_lib_end = max(e for _, e, _ in data_lib)
    if rdata_end != text_lo:
        raise LayoutError(f".rdata end {rdata_end:#x} is not the first matched function {text_lo:#x}")
    if text_hi != text_lib_end:
        raise LayoutError("a matched function lies above the last certified library text module")
    # Data references only: a mapped symbol that retail code reaches with jal is a call
    # into the leading module, not an .rdata datum.
    called = {((ev.word(a) & 0x03ffffff) << 2) | (a & 0xf0000000) for a in range(text_lo, text_lib_end, 4)
              if ev.word(a) >> 26 == 3}
    first_rdata_ref = min(a for a in symbols if ev.load <= a < rdata_end and a not in called)
    first_data_ref = min(a for a in symbols if text_lib_end <= a < sdata_start)
    sections, commons = ev.library_uninitialized()
    sbss_lib_end = max(s["address"] + s["size"] for s in sections if s["section"] == ".sbss")
    bss_sections = [s for s in sections if s["section"] == ".bss"]
    bss_lib_lo = min(s["address"] for s in bss_sections)
    bss_lib_end = max(s["address"] + s["size"] for s in bss_sections)
    common_sizes_by_name, defined = common_sizes(ev, {c["name"] for c in commons})
    commons = [c for c in commons if c["name"] not in defined]
    common_lo = min(c["address"] for c in commons)
    jal_targets = sorted({((ev.word(a) & 0x03ffffff) << 2) | (a & 0xf0000000) for a in range(text_lo, text_lib_end, 4)
                          if ev.word(a) >> 26 == 3
                          and ev.load <= ((ev.word(a) & 0x03ffffff) << 2) | (a & 0xf0000000) < first_rdata_ref})
    if not jal_targets or jal_targets[0] != ev.load:
        raise LayoutError("the leading module is no longer a call target at the load address")
    observed = operand_pairs(ev, text_lo, text_lib_end, {text_lib_end, sbss_lib_end})
    # Consistency of the evidenced order .rdata < .text < .data < .sdata < .sbss < .bss.
    checks = [("library .data inside (.text end, .sdata start)",
               all(text_lib_end <= a and e <= sdata_start for a, e, _ in data_lib)),
              ("library .sdata inside [.sdata start, .sbss start)",
               all(sdata_start <= a and e <= sbss_start for a, e, _ in sdata_lib)),
              ("library .sbss above .sbss start", all(s["address"] >= sbss_start for s in sections
                                                      if s["section"] == ".sbss")),
              ("library .bss and COMMON below sectend(.bss)",
               bss_lib_end <= bss_end and all(c["address"] < bss_end for c in commons)),
              ("payload ends inside .sbss", sbss_start <= ev.payload_end <= bss_end),
              ("header bss fields are zero", ev.header_bss == (0, 0)),
              ("payload tail after .sdata is zero",
               not any(ev.raw[HEADER + sbss_start - ev.load:]))]
    text_at = {}
    for a, _, module in lib_text:
        text_at.setdefault(module, a)
    for name in (".rdata", ".data"):
        order = [text_at.get(m) for _, _, m in sorted(by_name.get(name, [])) if m in text_at]
        checks.append((f"library {name} sections follow the library .text module order", order == sorted(order)))
    failed = [name for name, ok in checks if not ok]
    if failed:
        raise LayoutError(f"layout evidence is inconsistent: {failed}")
    cert = "certified Runtime Library 3.3 section (reports/runtime_data.json)"
    lead_end = first_rdata_ref
    regions = [
        region("leading module", None, ev.load, lead_end, "unknown",
               [f"payload starts at the header load address {ev.load:#x}",
                f"retail .text calls (jal) {[h(t) for t in jal_targets]}: code, with its tables, ahead of the "
                f"first string referenced by matched code at {first_rdata_ref:#x}",
                f"not .text: PSYLINK concatenates same-named sections and .text starts at {text_lo:#x}; "
                "its section name (.rdata or an earlier-ordered section) is not evidenced"]),
        region(".rdata", ".rdata", (ev.load, first_rdata_ref), rdata_end, "bounded",
               [f"first string referenced by matched code at {first_rdata_ref:#x}, before .text",
                f"last library .rdata {rdata_lib[-1][2]} ends at {rdata_end:#x}: {cert}",
                "start is the leading module's start or its end, depending on that module's section"]),
        region(".text", ".text", text_lo, (text_lib_end, first_data_ref), "bounded",
               [f"first matched function at {text_lo:#x} directly follows the library .rdata end",
                f"last certified library text module {lib_text[-1][2]} ends at {text_lib_end:#x}",
                f"first global referenced by matched code above it is {first_data_ref:#x}",
                f"observed (not certifying) retail operand pairs naming {text_lib_end:#x} as data: "
                f"{observed.get(text_lib_end, [])[:4]}"]),
        region(".data", ".data", (text_lib_end, first_data_ref), sdata_start, "bounded",
               [f"library .data {data_lib[0][2]}..{data_lib[-1][2]} lie in "
                f"[{data_lib[0][0]:#x}, {data_lib_end:#x}): {cert}",
                f"end is sectstart(.sdata) = {sdata_start:#x} from 2MBYTE.OBJ (also its $gp)"]),
        region(".sdata", ".sdata", sdata_start, sbss_start, "pinned",
               [f"sectstart(.sdata) = {sdata_start:#x} and sectstart(.sbss) = {sbss_start:#x}, "
                f"retail operands of 2MBYTE.OBJ at {startup['address']:#x}",
                f"library .sdata ends at {sdata_lib_end:#x} (alignment to {sbss_start:#x})"]),
        region(".sbss", ".sbss", sbss_start, (sbss_lib_end, bss_lib_lo), "bounded",
               ["start: sectstart(.sbss), the startup zero-clear start",
                f"last evidenced library .sbss section ends at {sbss_lib_end:#x}",
                f"first evidenced library .bss section starts at {bss_lib_lo:#x}",
                f"the payload file carries the first {ev.payload_end - sbss_start} bytes of .sbss as zeros "
                f"(payload rounded to 0x800 at {ev.payload_end:#x})"]),
        region(".bss", ".bss", (sbss_lib_end, bss_lib_lo), (bss_lib_end, common_lo), "bounded",
               [f"library .bss sections evidenced in [{bss_lib_lo:#x}, {bss_lib_end:#x})",
                f"the lowest library COMMON symbol is at {common_lo:#x}"]),
        region("COMMON", ".bss", (bss_lib_end, common_lo), bss_end, "bounded",
               ["COMMON (XBSS) symbols allocated by the linker after the .bss sections",
                f"end is sectend(.bss) = {bss_end:#x} (zero-clear end and heap base)"]),
    ]
    layout = {
        "reference_sha256": ev.reference_sha256,
        "load_address": h(ev.load), "payload_end": h(ev.payload_end),
        "gp": h(sdata_start),
        "section_order": SECTION_ORDER,
        "section_order_evidence": "2MBYTE.OBJ and every Runtime Library 3.3 module declare sections in this order; "
                                  "the certified library sections appear in the image in the same order",
        "startup": {"module": "2MBYTE.OBJ", "address": h(startup["address"]),
                    "sectstart(.sdata)": h(sdata_start), "sectstart(.sbss)": h(sbss_start),
                    "sectend(.bss)": h(bss_end),
                    "operand_sites": {f"{k}({s})": v for (k, s), v in startup["sites"].items()}},
        "header": {"bss_address": h(ev.header_bss[0]), "bss_size": ev.header_bss[1],
                   "gp": h(ev.header_gp), "entry": h(ev.entry)},
        "regions": regions,
    }
    bss = derive_bss(ev, layout, sections, commons, common_sizes_by_name, symbols,
                     sbss_start, bss_end, (sbss_lib_end, bss_lib_lo), (bss_lib_end, common_lo), statics)
    return layout, bss


def derive_bss(ev, layout, sections, commons, sizes, symbols, start, end, sbss_end, common_start, statics=()):
    entries = []
    startup = layout["startup"]
    for s in sections:
        entries.append({"address": h(s["address"]), "size": s["size"], "kind": s["kind"], "section": s["section"],
                        "names": [], "owner": s["owner"], "evidence": s["evidence"]})
    seen = {}
    for c in commons:
        key = (c["address"], c["name"])
        if key in seen:
            seen[key]["referenced_by"] = sorted(set(seen[key]["referenced_by"]) | {c["referenced_by"]})
            continue
        entry = {"address": h(c["address"]), "size": sizes[c["name"]], "kind": "library_common",
                 "section": None, "names": [c["name"]], "owner": None, "referenced_by": [c["referenced_by"]],
                 "evidence": f"COMMON symbol {c['name']} (XBSS size {sizes[c['name']]}, largest declaration in "
                             "Runtime Library 3.3) resolved from certified relocations"}
        seen[key] = entry
        entries.append(entry)
    for address, size, section, unit, name in statics:
        entries.append({"address": h(address), "size": size, "kind": "unit_static", "section": section,
                        "names": [name], "owner": unit,
                        "evidence": f"static_data of {unit}: a local symbol of its {section}, whose offset, extent "
                                    "and address (section base + offset) verify.py and the gate audit against the "
                                    "unit's object"})
    sized = sorted(((num(e["address"]), num(e["address"]) + e["size"], e) for e in entries), key=lambda r: r[0])
    for (a, b, e), (c, d, f) in zip(sized, sized[1:]):
        if c < b:
            raise LayoutError(f"BSS entries overlap: {e['owner'] or e['names']} and {f['owner'] or f['names']}")
    for address, names in sorted(symbols.items()):
        if not start <= address < end:
            continue
        inside = next((e for a, b, e in sized if a <= address < b), None)
        entry = {"address": h(address), "size": None, "kind": "game_reference", "section": None,
                 "names": sorted(names), "owner": None,
                 "referenced_by": sorted({u for units in names.values() for u in units}),
                 "evidence": "address of a global named by a matched unit's map"}
        if inside is not None:
            entry["interior_of"] = inside["owner"] or inside["names"][0]
            entry["evidence"] += "; lies inside an evidenced library allocation, so it is not separate storage"
        entries.append(entry)
    entries.sort(key=lambda e: (num(e["address"]), e["kind"]))
    starts = sorted({num(e["address"]) for e in entries if "interior_of" not in e} | {end})
    for e in entries:
        a = num(e["address"])
        if a < sbss_end[0]:
            e["region"] = ".sbss"
        elif a >= common_start[1]:
            e["region"] = "COMMON" if e["kind"] != "library_section" else ".bss"
        elif a >= sbss_end[1] and a < common_start[0]:
            e["region"] = ".bss"
        else:
            e["region"] = "unknown"
        if e["kind"] == "library_section":
            e["region"] = e["section"]
        if e["size"] is None and "interior_of" not in e:
            e["max_size"] = next(s for s in starts if s > a) - a
    sized_bytes = sum(e["size"] for e in entries if e["size"])
    return {"reference_sha256": ev.reference_sha256,
            "range": {"start": h(start), "end": h(end), "size": end - start,
                      "evidence": f"2MBYTE.OBJ zero-clear loop sectstart(.sbss)..sectend(.bss) at {startup['address']}; "
                                  f"header bss fields are {layout['header']['bss_address']}/{layout['header']['bss_size']}"},
            "counts": {"entries": len(entries),
                       "sized_entries": sum(1 for e in entries if e["size"]),
                       "sized_bytes": sized_bytes,
                       "game_references": sum(1 for e in entries if e["kind"] == "game_reference"),
                       "unit_statics": sum(1 for e in entries if e["kind"] == "unit_static"),
                       "interior_references": sum(1 for e in entries if "interior_of" in e)},
            "notes": ["size is exact only for library sections, COMMON symbols and unit statics (from the objects); "
                      "a game reference carries max_size, the distance to the next evidenced address, never a size",
                      "no matched unit defines an uninitialised global (no XBSS records or exported .sbss/.bss "
                      "definitions in the unit objects); the game-owned entries are unit statics (kind "
                      "unit_static, owner = the unit), each the allocation of one local symbol"],
            "entries": entries}


# ------------------------------------------------------------- validation

def validate(layout, bss):
    """Structural checks that hold for any evidence: order, nesting, alignment, non-overlap."""
    problems = []

    def lo(value):
        return num(value["min"]) if isinstance(value, dict) else num(value)

    def hi(value):
        return num(value["max"]) if isinstance(value, dict) else num(value)

    regions = layout["regions"]
    for r in regions:
        if lo(r["start"]) > hi(r["start"]) or lo(r["end"]) > hi(r["end"]) or hi(r["start"]) > lo(r["end"]):
            problems.append(f"region {r['name']} has an empty or inverted interval")
        if r["status"] == "pinned" and (isinstance(r["start"], dict) or isinstance(r["end"], dict)):
            problems.append(f"region {r['name']} is pinned but carries an interval")
    for a, b in zip(regions, regions[1:]):
        if a["end"] != b["start"] and not (lo(a["end"]) <= hi(b["start"]) and hi(a["end"]) >= lo(b["start"])):
            problems.append(f"regions {a['name']} and {b['name']} do not meet")
    order = [r["section"] for r in regions if r["section"]]
    if [s for s in SECTION_ORDER if s in order] != sorted(set(order), key=SECTION_ORDER.index):
        problems.append("regions are not in section order")
    start, end = num(bss["range"]["start"]), num(bss["range"]["end"])
    prev_end = None
    for e in bss["entries"]:
        a = num(e["address"])
        if not start <= a < end:
            problems.append(f"BSS entry {e['names'] or e['owner']} at {a:#x} outside the cleared range")
        if e["size"]:
            if a + e["size"] > end:
                problems.append(f"BSS entry {e['names'] or e['owner']} runs past the cleared range")
            if prev_end is not None and a < prev_end:
                problems.append(f"BSS entry {e['names'] or e['owner']} overlaps its predecessor")
            prev_end = a + e["size"]
            align = 4 if e["size"] >= 4 else e["size"]
            if e["kind"] == "library_common" and a % align:
                problems.append(f"COMMON {e['names']} is misaligned")
        if e["kind"] == "unit_static" and e.get("region") != e["section"]:
            problems.append(f"static {e['names']} of {e['owner']} is declared {e['section']} "
                            f"but lies in the {e.get('region')} region")
    return problems


def section_of(layout, address):
    """Section name of the region holding address, or None when ambiguous/unknown."""
    for r in layout["regions"]:
        start = r["start"]["max"] if isinstance(r["start"], dict) else r["start"]
        end = r["end"]["min"] if isinstance(r["end"], dict) else r["end"]
        if num(start) <= address < num(end):
            return r["section"]
    return None


def load_layout(path=SECTION_CONFIG):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true", help="rewrite the config files from the evidence")
    ap.add_argument("--report", type=Path, default=REPORT)
    args = ap.parse_args(argv)
    try:
        layout, bss = derive()
    except LayoutError as exc:
        print(f"LAYOUT FAILED {exc}", file=sys.stderr)
        return 1
    problems = validate(layout, bss)
    if args.write and not problems:
        SECTION_CONFIG.write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")
        BSS_CONFIG.write_text(json.dumps(bss, indent=2) + "\n", encoding="utf-8")
    for path, derived in ((SECTION_CONFIG, layout), (BSS_CONFIG, bss)):
        if not path.is_file():
            problems.append(f"{path.name} is missing; run with --write")
        elif json.loads(path.read_text(encoding="utf-8")) != derived:
            problems.append(f"{path.name} differs from what the evidence derives")
    report = {"status": "fail" if problems else "pass", "problems": problems,
              "inputs": {str(p.relative_to(PROJECT)): sha256(p.read_bytes())
                         for p in (RUNTIME_DATA, RUNTIME_LINKED, Path(__file__))},
              "regions": [{k: r[k] for k in ("name", "section", "start", "end", "status")}
                          for r in layout["regions"]],
              "bss_counts": bss["counts"]}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if problems:
        print("LAYOUT FAILED", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1
    for r in report["regions"]:
        fmt = lambda v: f"[{v['min']}..{v['max']}]" if isinstance(v, dict) else v
        print(f"{r['name']:<15} {str(r['section']):<7} {fmt(r['start']):>26} - {fmt(r['end']):<26} {r['status']}")
    print(f"LAYOUT OK bss_entries={bss['counts']['entries']} sized_bytes={bss['counts']['sized_bytes']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
