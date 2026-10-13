"""Strict Runtime 3.3 initialized-section matching against the original EXE.

This reports complete initialized sections for Runtime modules with certified
linked text, plus strict whole-section discoveries in data-only archive
modules. A section is certified only when its placement/evidence is complete
and the complete section bytes match the EXE.
"""
from __future__ import annotations

import difflib
import hashlib
import importlib.util
import json
import struct
from collections import Counter, defaultdict
from functools import lru_cache
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
PROJECT = Path(__file__).resolve().parents[1]
ANALYSIS = PROJECT / "analysis"
REPORTS = PROJECT / "reports"
CONFIG = PROJECT / "config"
RUNTIME_ANALYZER = ANALYSIS / "runtime_relocations.py"
PARSER_PATH = ROOT / "shared-tools" / "psyqobj.py"
LOCK_PATH = CONFIG / "toolchain.lock.json"
SECTION_NAMES = (".data", ".sdata", ".rdata")
CERTIFIED_STATUSES = {"full_match", "empty_section"}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_digest(path: Path) -> str:
    return digest(path.read_bytes())


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


@lru_cache(maxsize=1)
def load_helpers():
    spec = importlib.util.spec_from_file_location("runtime_relocations_for_data", RUNTIME_ANALYZER)
    if spec is None or spec.loader is None:
        raise ValueError("cannot load runtime_relocations.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _bindings(linked, runtime_symbols):
    """Build only non-contradictory values from the linked evidence reports."""
    rr = load_helpers()
    values = {}
    conflicts = set()
    for record in linked.get("symbol_bindings", []):
        kind = record.get("kind")
        parts = record.get("name_or_section", [])
        if kind == "symbol" and len(parts) == 1:
            key = ("symbol", parts[0])
        elif kind in ("section_base", "section_start", "section_end") and len(parts) == 2:
            key = (kind, parts[0], int(parts[1]))
        elif kind == "local_symbol" and len(parts) == 2:
            key = ("local_symbol", parts[0], int(parts[1]))
        else:
            continue
        if record.get("status") != "resolved" or record.get("address") is None:
            conflicts.add(key)
            values.pop(key, None)
            continue
        address = rr.as_int(record["address"])
        if key in conflicts:
            continue
        if key in values and values[key] != address:
            values.pop(key, None)
            conflicts.add(key)
        else:
            values[key] = address

    # Match runtime_relocations.resolve(): uniquely exported addresses are
    # valid imported-symbol bindings, unless linked evidence marks a conflict.
    exported = defaultdict(set)
    for row in runtime_symbols.get("exports", []):
        exported[row["name"]].add(rr.as_int(row["address"]))
    for name, addresses in exported.items():
        key = ("symbol", name)
        if len(addresses) == 1 and key not in conflicts:
            value = next(iter(addresses))
            if key not in values:
                values[key] = value
            elif values[key] != value:
                values.pop(key, None)
                conflicts.add(key)
    return values, conflicts


def _archive_modules(path, parser):
    """Return parsed objects and raw LNK bytes indexed by exact module name."""
    parsed = parser.load(str(path))
    if Path(path).read_bytes()[:4] == b"LNK\x02":
        for obj in parsed:
            if not isinstance(obj, Exception): obj.name = Path(path).name
    objects = {obj.name: obj for obj in parsed if not isinstance(obj, Exception)}
    raw = dict(load_helpers().library_modules(path))
    return objects, raw


def _module_key(library, module, address):
    return f"{library}::{module}@{address:08x}"


def _reference_slice(payload, load_address, address, size):
    start = address - load_address
    if start < 0 or size < 0 or start + size > len(payload):
        raise ValueError(f"section extent {address:#x}+{size:#x} is outside executable payload")
    return payload[start:start + size]


def _serialized_address(value):
    return f"0x{value:08x}" if value is not None else None


def _relocate_section(code, patches, obj, module_key, address, text_address, bindings,
                      conflicts, gp, imports):
    actual = bytearray(code)
    applied = []
    errors = []
    for typ, off, expr in patches:
        try:
            if off < 0 or off + 4 > len(actual):
                raise ValueError("relocation extent is outside complete section")
            keys = load_helpers().expression_keys(expr, obj, module_key)
            bad = sorted(keys & conflicts, key=repr)
            if bad:
                raise ValueError("contradictory binding: " + repr(bad))
            val, unresolved = load_helpers().linear(
                expr, obj, bindings, module_key, text_address, gp)
            if unresolved:
                raise ValueError("unresolved relocation context: " + repr(unresolved))
            val &= 0xffffffff
            word = struct.unpack_from("<I", actual, off)[0]
            if typ == 16:  # R_MIPS_32 absolute word
                resolved = val
                word = val
            elif typ == 82:
                resolved = ((val + 0x8000) >> 16) & 0xffff
                word = (word & 0xffff0000) | resolved
            elif typ == 84:
                resolved = val & 0xffff
                word = (word & 0xffff0000) | resolved
            elif typ == 74:
                if val & 3:
                    raise ValueError(f"unaligned MIPS26 target {val:#x}")
                if ((val ^ (address + off + 4)) & 0xf0000000):
                    raise ValueError(f"MIPS26 target {val:#x} is outside jump region")
                resolved = val
                word = (word & 0xfc000000) | ((val >> 2) & 0x03ffffff)
            elif typ == 30:
                delta = val if val < 0x80000000 else val - 0x100000000
                if not -32768 <= delta <= 32767:
                    raise ValueError(f"GPREL16 displacement out of range: {delta}")
                resolved = delta
                word = (word & 0xffff0000) | (delta & 0xffff)
            else:
                raise ValueError(f"unsupported relocation type {typ}")
            struct.pack_into("<I", actual, off, word)
            applied.append({"type": int(typ), "offset": int(off), "value": resolved})
        except (ValueError, KeyError, struct.error) as exc:
            errors.append({"type": int(typ), "offset": int(off), "error": str(exc)})
    return bytes(actual), applied, errors


def _exported_definitions(definitions, section_id, section_address):
    exports = []
    for symbol_id, definition in sorted(definitions.items()):
        name, sid, offset = definition[:3]
        if sid != section_id:
            continue
        size = definition[3] if len(definition) > 3 else None
        exports.append({
            "symbol_index": int(symbol_id),
            "name": name,
            "section_id": int(sid),
            "relative_offset": int(offset) if offset is not None else None,
            "address": _serialized_address(section_address + offset)
                if section_address is not None and offset is not None else None,
            "size": int(size) if size is not None else None,
        })
    return exports


def _scan_data_only_modules(library_manifest, library_hashes, loaded,
                            parser, payload, load_address, text_sections):
    """Find unique, aligned, unrelocated whole sections in data-only modules."""
    modules = []
    flat_sections = []
    for library in sorted(library_manifest):
        lib_path = ROOT / library
        current_hash = file_digest(lib_path)
        if current_hash != library_manifest[library]:
            raise ValueError(f"Runtime library changed since linked report: {library}")
        if lib_path not in loaded:
            loaded[lib_path] = _archive_modules(lib_path, parser)
        objects, raw_modules = loaded[lib_path]
        for module, obj in sorted(objects.items()):
            nonempty_text = [
                sid for sid, name in obj.sections.items()
                if name == ".text" and bytes(obj.code.get(sid, b""))
            ]
            if nonempty_text:
                continue
            candidates = []
            for sid, name in sorted(obj.sections.items(), key=lambda item: (item[1], item[0])):
                if name in SECTION_NAMES and len(bytes(obj.code.get(sid, b""))) >= 64:
                    candidates.append((sid, name, bytes(obj.code.get(sid, b"")), obj.patches.get(sid, [])))
            if not candidates:
                continue
            unsupported_nonempty = [
                {"section_id": int(sid), "section": name, "size": len(bytes(obj.code.get(sid, b"")))}
                for sid, name in obj.sections.items()
                if name not in SECTION_NAMES and bytes(obj.code.get(sid, b""))
            ]
            raw_lnk = raw_modules.get(module)
            if raw_lnk is None:
                raise ValueError(f"data-only module {library}::{module} has no raw LNK record")
            definitions, _imports = load_helpers().symbol_ids(raw_lnk)
            object_hash = digest(raw_lnk)
            rows = []
            for sid, name, code, patches in candidates:
                base = {
                    "scope": "supplemental_data_only",
                    "library": library,
                    "module": module,
                    "module_key": f"{library}::{module}@data-only",
                    "section": name,
                    "section_id": int(sid),
                    "address": None,
                    "size": len(code),
                    "raw_sha256": digest(code),
                    "reference_sha256": None,
                    "object_sha256": object_hash,
                    "library_sha256": library_hashes[library],
                    "relocation_count": len(patches),
                    "applied_relocations": [],
                    "unresolved_relocations": [],
                    "whole_section": True,
                    "aligned": True,
                    "complete_extent": True,
                    "unique_occurrence": False,
                    "no_relocations": len(patches) == 0,
                    "no_nonempty_text": True,
                    "no_unsupported_sections": not unsupported_nonempty,
                    "occurrence_count": 0,
                    "candidate_addresses": [],
                    "exported_definitions": _exported_definitions(definitions, sid, None),
                    "overlaps": [],
                }
                if unsupported_nonempty:
                    base["status"] = "unsupported_nonempty_sections"
                    base["unsupported_sections"] = unsupported_nonempty
                elif patches:
                    base["status"] = "has_relocations"
                else:
                    hits = [
                        load_address + offset
                        for offset in range(0, len(payload) - len(code) + 1, 4)
                        if payload[offset:offset + len(code)] == code
                    ]
                    base["occurrence_count"] = len(hits)
                    base["unique_occurrence"] = len(hits) == 1
                    base["candidate_addresses"] = [_serialized_address(hit) for hit in hits]
                    if len(hits) == 1:
                        address = hits[0]
                        base["address"] = _serialized_address(address)
                        base["aligned"] = address % 4 == 0
                        base["reference_sha256"] = digest(_reference_slice(payload, load_address, address, len(code)))
                        base["exported_definitions"] = _exported_definitions(definitions, sid, address)
                        base["status"] = "full_match"
                    elif len(hits) > 1:
                        base["status"] = "ambiguous_occurrences"
                    else:
                        base["status"] = "no_match"
                rows.append(base)
            modules.append({
                "library": library,
                "module": module,
                "module_key": f"{library}::{module}@data-only",
                "text_status": "no_nonempty_text",
                "unsupported_nonempty_sections": unsupported_nonempty,
                "object_sha256": object_hash,
                "library_sha256": library_hashes[library],
                "exported_definitions": [
                    record for row in rows for record in row["exported_definitions"]
                ],
                "discovery_status": "evaluated",
                "sections": rows,
            })
            flat_sections.extend(rows)

    # Exact duplicate/nested archive sections are recorded but do not add new
    # certified bytes. The linked-text section remains the primary certificate.
    prior_ranges = [
        (load_helpers().as_int(row["address"]), int(row["size"]), row)
        for row in text_sections if row.get("status") == "full_match" and row.get("size")
    ]
    for section in flat_sections:
        if section["status"] != "full_match":
            continue
        start = load_helpers().as_int(section["address"])
        end = start + int(section["size"])
        overlaps = []
        for other_start, other_size, other in prior_ranges:
            other_end = other_start + other_size
            lo, hi = max(start, other_start), min(end, other_end)
            if lo < hi:
                overlaps.append({
                    "module": other["module"],
                    "section": other["section"],
                    "address": _serialized_address(lo),
                    "size": hi - lo,
                    "kind": "nested_alias" if (start <= other_start and end >= other_end) or
                        (other_start <= start and other_end >= end) else "partial_overlap",
                })
        if overlaps:
            section["overlaps"] = overlaps
            section["status"] = "alias_overlap_excluded"
            section["byte_match_status"] = "full_match"

    for module in modules:
        statuses = {row["status"] for row in module["sections"]}
        if "full_match" in statuses:
            module["discovery_status"] = "matched"
        elif "alias_overlap_excluded" in statuses:
            module["discovery_status"] = "overlap_alias_excluded"
        elif "ambiguous_occurrences" in statuses:
            module["discovery_status"] = "ambiguous"
        elif "no_match" in statuses:
            module["discovery_status"] = "no_match"
        else:
            module["discovery_status"] = "excluded"
        module["status"] = module["discovery_status"]
    return modules, flat_sections


_LO_OPS = {0x09, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
           0x28, 0x29, 0x2a, 0x2b, 0x2e, 0x32, 0x3a}
_RT_DEST_OPS = {0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26}


def _absolute_references(payload, load_address, gp, start, end):
    """Data addresses formed by `lui`+%lo pairs or $gp-relative accesses.

    Scans only [start, end) of the text region. A `lui` stays live for 64
    bytes until its register is overwritten. Returns {address: [instruction]}.
    """
    refs = defaultdict(list)
    live = {}
    for address in range(start, end, 4):
        word = struct.unpack_from("<I", payload, address - load_address)[0]
        op, rs, rt, rd = word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
        imm = word & 0xffff
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op in _LO_OPS:
            if rs in live and address - live[rs][1] <= 64:
                refs[(live[rs][0] + simm) & 0xffffffff].append(address)
            elif rs == 28:
                refs[(gp + simm) & 0xffffffff].append(address)
        dest = None
        if op == 0:
            dest = rd
        elif op == 3:
            dest = 31
        elif op in _RT_DEST_OPS:
            dest = rt
        elif op in (0x10, 0x12) and rs in (0, 2):
            dest = rt
        if dest:
            live.pop(dest, None)
        if op == 0x0f:
            live[rt] = (imm << 16, address)
    return refs


def _key_address(module_key):
    return int(module_key.rsplit("@", 1)[1], 16)


def _skeleton(code):
    words = struct.unpack("<%dI" % (len(code) // 4), code[:len(code) // 4 * 4])
    return [(w >> 26, w & 0x3f if w >> 26 == 0 else (w >> 16) & 31 if w >> 26 == 1 else 0)
            for w in words]


def _variant_extent(payload, load_address, lo, hi, content, size, loaded, library_manifest,
                    certified_keys, threshold=0.85):
    """The archive module whose skeleton spans exactly [lo, hi), or None.

    Candidates carry exactly one .data section starting with the table. The
    skeleton (opcode and function field) alignment must start and end within
    two words of both extents and match at least `threshold` of the longer
    sequence; a second distinct module above the threshold makes it ambiguous.
    """
    target = _skeleton(_reference_slice(payload, load_address, lo, hi - lo))
    scored = []
    for library in sorted(library_manifest):
        for module, obj in sorted(loaded[ROOT / library][0].items()):
            if any(key.startswith(f"{library}::{module}@") for key in certified_keys):
                continue
            tables = [sid for sid, n in obj.sections.items()
                      if n == ".data" and bytes(obj.code.get(sid, b""))[:size] == content]
            text_id = next((sid for sid, n in obj.sections.items() if n == ".text"), None)
            if len(tables) != 1 or text_id is None:
                continue
            source = _skeleton(bytes(obj.code.get(text_id, b"")))
            if not source or max(len(source), len(target)) * threshold > min(len(source), len(target)):
                continue
            matcher = difflib.SequenceMatcher(None, source, target, autojunk=False)
            blocks = [b for b in matcher.get_matching_blocks() if b.size]
            matched = sum(b.size for b in blocks)
            if not blocks or blocks[0].a > 2 or blocks[0].b > 2:
                continue
            last = blocks[-1]
            if len(source) - (last.a + last.size) > 2 or len(target) - (last.b + last.size) > 2:
                continue
            if matched >= threshold * max(len(source), len(target)):
                scored.append((matched / max(len(source), len(target)), library, module,
                               len(source), len(target), matched))
    scored.sort(reverse=True)
    if not scored or len({row[2] for row in scored}) > 1:
        return None
    ratio, library, module, source_words, target_words, matched = scored[0]
    return {"library": library, "module": module, "object_words": source_words,
            "gap_words": target_words, "matched_skeleton_words": matched,
            "ratio": round(ratio, 4)}


def _resolve_unanchored_by_link_order(section_results, unanchored, full_text, linked,
                                      loaded, parser, library_manifest, payload,
                                      load_address, gp):
    """Place identical unexported .data copies from link order and ownership.

    PsyLink emits each module's .data in the same module order as its .text.
    Separators are (text, data) pairs whose ownership is proven: certified
    .data sections of certified text modules, and copies of the section that
    are referenced from text (an unexported static can only be addressed by its
    own module). Between two separators, every unreferenced copy must belong
    to a module whose text lies between them: certified modules (and archive
    modules placed by a certified import binding) own exactly their known
    copies, and unidentified text owns any number. A placement is certified
    only when every consistent assignment gives the same address.
    """
    rr = load_helpers()
    groups = defaultdict(list)
    for row in section_results:
        if row["status"] == "ambiguous_unanchored" and row["section"] == ".data":
            groups[row["raw_sha256"]].append(row)
    if not groups:
        return
    text_lo = min(rr.as_int(t["address"]) for t in full_text)
    text_hi = max(rr.as_int(t["address"]) + int(t["text_size"]) for t in full_text)
    refs = _absolute_references(payload, load_address, gp, text_lo, text_hi)
    certified_text = sorted((rr.as_int(t["address"]), rr.as_int(t["address"]) + int(t["text_size"]),
                             _module_key(t["library"], t["module"], rr.as_int(t["address"])))
                            for t in full_text)

    # Archive modules whose text is placed by a resolved import binding from
    # certified relocations (an export at a known offset of a unique module).
    exporters = defaultdict(list)
    for library in sorted(library_manifest):
        lib_path = ROOT / library
        if lib_path not in loaded:
            loaded[lib_path] = _archive_modules(lib_path, parser)
        for module, obj in sorted(loaded[lib_path][0].items()):
            text_id = next((s for s, n in obj.sections.items() if n == ".text"), None)
            for name, sid, off in obj.xdefs:
                if sid == text_id:
                    exporters[name].append((library, module, obj, text_id, off))
    certified_keys = {key for _, _, key in certified_text}
    bound_units = []
    for record in linked.get("symbol_bindings", []):
        if record.get("kind") != "symbol" or record.get("status") != "resolved":
            continue
        found = exporters.get(record["name_or_section"][0], [])
        if len(found) != 1:
            continue
        library, module, obj, text_id, off = found[0]
        start = rr.as_int(record["address"]) - off
        if _module_key(library, module, start) in certified_keys:
            continue
        code = bytes(obj.code.get(text_id, b""))
        if not code or any(start < b and a < start + len(code) for a, b, _ in certified_text):
            continue
        mask = {i for _, p, _ in obj.patches.get(text_id, [])
                for i in range(p & ~3, min(len(code), (p & ~3) + 4))}
        reference = _reference_slice(payload, load_address, start, len(code))
        if any(code[i] != reference[i] for i in range(len(code)) if i not in mask):
            continue
        bound_units.append((start, start + len(code), library, module, obj,
                            record["name_or_section"][0]))

    for content_hash, rows in sorted(groups.items()):
        evidence = {"method": "link_order_ownership", "status": "unresolved"}
        copies = [rr.as_int(a) for a in rows[0]["candidate_addresses"]]
        size = int(rows[0]["size"])
        content = None
        reason = None
        for row in rows:
            obj, definitions = unanchored[(row["module_key"], row["section_id"])]
            sid = row["section_id"]
            content = bytes(obj.code.get(sid, b""))
            if row["candidate_addresses"] != rows[0]["candidate_addresses"] or row["relocation_count"]:
                reason = "group members differ or carry relocations"
            if any(d[1] == sid for d in definitions.values()):
                reason = f"{row['module']} exports a symbol in the section"
            for patches in obj.patches.values():
                for _, _, expr in patches:
                    for key in rr.expression_keys(expr, obj, row["module_key"]):
                        if key[0] in ("section_base", "section_start", "section_end") and key[2] == sid:
                            reason = f"{row['module']} relocates against the section"
                        if key[0] == "local_symbol" and definitions.get(key[2], (None, None))[1] == sid:
                            reason = f"{row['module']} relocates against a symbol in the section"
        referenced = {}
        for copy in copies:
            sites = sorted(a for d in range(copy, copy + size) for a in refs.get(d, []))
            if sites:
                referenced[copy] = sites
                if any(a <= s < b for s in sites for a, b, _ in certified_text):
                    reason = reason or f"copy {copy:#x} is referenced from certified module text"
        free = [c for c in copies if c not in referenced]
        owners_known = {}
        for start, end, _ in certified_text:
            owners_known[start] = {"end": end, "copies": 0}
        for row in rows:
            owners_known[_key_address(row["module_key"])]["copies"] += 1
        bound_records = []
        for start, end, library, module, obj, symbol in bound_units:
            count = sum(1 for sid, n in obj.sections.items()
                        if n == ".data" and bytes(obj.code.get(sid, b"")) == content)
            owners_known.setdefault(start, {"end": end, "copies": count})
            bound_records.append({"library": library, "module": module, "symbol": symbol,
                                  "text_address": _serialized_address(start),
                                  "text_size": end - start, "copies_owned": count})
        # A gap of unidentified text that references one copy can be shown to
        # be a single variant build of an archive module whose .data starts
        # with the table: its instruction skeleton must align with the whole
        # gap at both ends. One translation unit defines the static table
        # once, so that module owns the referenced copy and no other.
        variant_records = []
        for copy, sites in sorted(referenced.items()):
            known_spans = sorted((start, info["end"]) for start, info in owners_known.items())
            gap_lo = max([e for s_, e in known_spans if e <= sites[0]], default=None)
            gap_hi = min([s_ for s_, e in known_spans if s_ >= sites[-1] + 4], default=None)
            if gap_lo is None or gap_hi is None:
                continue
            if any(gap_lo <= s_ < gap_hi for c, ss in referenced.items() if c != copy for s_ in ss):
                continue
            best = _variant_extent(payload, load_address, gap_lo, gap_hi, content, size,
                                   loaded, library_manifest, certified_keys)
            if best is None:
                continue
            owners_known[gap_lo] = {"end": gap_hi, "copies": 0}
            variant_records.append(dict(best, text=[_serialized_address(gap_lo), _serialized_address(gap_hi)],
                                        owned_copy=_serialized_address(copy)))
        # Separators: (text_lo, text_hi, data_lo, data_hi, kind).
        separators = []
        for row in section_results:
            if row["status"] == "full_match" and row["section"] == ".data" and row["size"]:
                module_start = _key_address(row["module_key"])
                data = rr.as_int(row["address"])
                separators.append((module_start, owners_known[module_start]["end"],
                                   data, data + int(row["size"]), "certified " + row["module"]))
        for copy, sites in referenced.items():
            separators.append((sites[0], sites[-1] + 4, copy, copy + size, "referenced copy"))
        separators.sort()
        for a, b in zip(separators, separators[1:]):
            if not (a[1] <= b[0] and a[3] <= b[2]):
                reason = reason or f"separators out of order: {a[4]} / {b[4]}"
        evidence.update({
            "copies": [_serialized_address(c) for c in copies],
            "referenced_copies": {_serialized_address(c): [_serialized_address(s) for s in sites]
                                  for c, sites in sorted(referenced.items())},
            "unreferenced_copies": [_serialized_address(c) for c in free],
            "bound_import_units": bound_records,
            "variant_module_extents": variant_records,
            "separators": [{"text": [_serialized_address(s[0]), _serialized_address(s[1])],
                            "data": [_serialized_address(s[2]), _serialized_address(s[3])],
                            "kind": s[4]} for s in separators],
        })
        possible = defaultdict(set)
        if reason is None:
            bounds = ([(None, text_lo, None, 0)] + [s[:4] for s in separators] +
                      [(text_hi, None, 0xffffffff, None)])
            for left, right in zip(bounds, bounds[1:]):
                t0, t1, d0, d1 = left[1], right[0], left[3], right[2]
                window = [c for c in free if d0 <= c and c + size <= d1]
                owners = []
                cursor = max([t0] + [info["end"] for start, info in owners_known.items()
                                     if start < t0 < info["end"]])
                for start in sorted(owners_known):
                    if not t0 <= start < t1:
                        continue
                    if start > cursor:
                        owners.append((None, 0, len(window)))
                    owners.append((start, owners_known[start]["copies"], owners_known[start]["copies"]))
                    cursor = max(cursor, owners_known[start]["end"])
                if cursor < t1:
                    owners.append((None, 0, len(window)))
                solutions = []

                def assign(i, used, picked):
                    if i == len(owners):
                        if used == len(window):
                            solutions.append(dict(picked))
                        return
                    start, lo, hi = owners[i]
                    for n in range(lo, min(hi, len(window) - used) + 1):
                        if start is not None and n:
                            picked[start] = window[used:used + n]
                        assign(i + 1, used + n, picked)
                        if start is not None:
                            picked.pop(start, None)

                assign(0, 0, {})
                if not solutions:
                    reason = f"no consistent assignment in text window {t0:#x}..{t1:#x}"
                    break
                for solution in solutions:
                    for start, picked in solution.items():
                        possible[start].update(picked)
        if reason is not None:
            evidence["reason"] = reason
            for row in rows:
                row["link_order_evidence"] = evidence
            continue
        evidence["status"] = "evaluated"
        for row in rows:
            choices = sorted(possible.get(_key_address(row["module_key"]), set()))
            row["link_order_evidence"] = dict(evidence, possible_addresses=[
                _serialized_address(c) for c in choices])
            if len(choices) != 1:
                continue
            address = choices[0]
            reference = _reference_slice(payload, load_address, address, size)
            if reference != content:
                raise ValueError(f"{row['module_key']}: placed copy differs from section bytes")
            row.update({"address": _serialized_address(address),
                        "reconstructed_sha256": digest(content),
                        "reference_sha256": digest(reference),
                        "mismatch_count": 0, "mismatch_offsets": [],
                        "status": "full_match"})


def _input_snapshot(inventory, runtime_symbols, linked, library_hashes):
    executable_rel = inventory["inputs"]["executable"]
    executable_path = ROOT / executable_rel
    lock = load_json(LOCK_PATH)
    if file_digest(executable_path) != lock.get("reference_sha256"):
        raise ValueError("reference executable hash differs from toolchain lock")
    summary = load_json(REPORTS / "runtime_latest.json")
    if library_hashes != linked.get("runtime_library_sha256"):
        raise ValueError("Runtime archives differ from linked-text evidence")
    if library_hashes != summary.get("runtime_library_sha256"):
        raise ValueError("Runtime archives differ from summary evidence")
    parser_rel = "shared-tools/psyqobj.py"
    parser_hash = file_digest(PARSER_PATH)
    if lock.get("files", {}).get(parser_rel) != parser_hash:
        raise ValueError("PsyQ object parser hash differs from toolchain lock")
    if file_digest(executable_path) != inventory["inputs"]["executable_sha256"]:
        raise ValueError("inventory executable hash does not match current reference")
    if digest(executable_path.read_bytes()) != runtime_symbols.get("executable_sha256"):
        raise ValueError("Runtime symbol report belongs to a different executable")
    if linked.get("executable_sha256") != inventory["inputs"]["executable_sha256"]:
        raise ValueError("Runtime text report belongs to a different executable")
    return {
        "executable": executable_rel,
        "executable_sha256": file_digest(executable_path),
        "inventory_report": "alien_decomp/analysis/inventory.json",
        "inventory_report_sha256": file_digest(ANALYSIS / "inventory.json"),
        "runtime_symbol_report": "alien_decomp/analysis/runtime_symbols.json",
        "runtime_symbol_report_sha256": file_digest(ANALYSIS / "runtime_symbols.json"),
        "runtime_linked_report": "alien_decomp/analysis/runtime_linked.json",
        "runtime_linked_report_sha256": file_digest(ANALYSIS / "runtime_linked.json"),
        "runtime_summary_report": "alien_decomp/reports/runtime_latest.json",
        "runtime_summary_report_sha256": file_digest(REPORTS / "runtime_latest.json"),
        "psyq_parser": parser_rel,
        "psyq_parser_sha256": parser_hash,
        "toolchain_lock": "alien_decomp/config/toolchain.lock.json",
        "toolchain_lock_sha256": file_digest(LOCK_PATH),
        "analyzer": "alien_decomp/analysis/runtime_data.py",
        "analyzer_sha256": file_digest(Path(__file__).resolve()),
        "runtime_analyzer_sha256": file_digest(RUNTIME_ANALYZER),
        "runtime": "3.3",
        "runtime_library_sha256": dict(sorted(library_hashes.items())),
    }


def build_report():
    inventory = load_json(ANALYSIS / "inventory.json")
    runtime_symbols = load_json(ANALYSIS / "runtime_symbols.json")
    linked = load_json(ANALYSIS / "runtime_linked.json")
    rr = load_helpers()
    if runtime_symbols.get("runtime") != "3.3" or linked.get("runtime") != "3.3":
        raise ValueError("reports do not identify Runtime 3.3")
    # Recompute upstream placements, symbol observations, and every text
    # relocation before using their bindings to certify initialized data.
    _, replay_modules, replay_bindings, _, replay_libraries = rr.resolve(
        runtime_symbols, inventory, inventory["inputs"]["runtime_root"])
    if json.loads(json.dumps(replay_modules)) != linked.get("module_results"):
        raise ValueError("Runtime text results differ from independent replay")
    if json.loads(json.dumps(replay_bindings)) != linked.get("symbol_bindings"):
        raise ValueError("Runtime bindings differ from independent replay")
    if replay_libraries != linked.get("runtime_library_sha256"):
        raise ValueError("Runtime archive hashes differ from independent replay")
    payload_raw = (ROOT / inventory["inputs"]["executable"]).read_bytes()
    load_address, text_size = struct.unpack_from("<II", payload_raw, 0x18)
    payload = payload_raw[0x800:0x800 + text_size]
    if len(payload) != text_size:
        raise ValueError("truncated PS-X EXE payload")
    gp = rr.as_int(inventory["inputs"]["startup_gp_observed"])
    bindings, conflicts = _bindings(linked, runtime_symbols)
    parser = rr.psyqobj
    loaded = {}
    library_hashes = {}
    section_results = []
    module_results = []
    unanchored = {}
    full_text = [row for row in linked["module_results"] if row.get("status") == "full_match"]
    library_manifest = linked.get("runtime_library_sha256", {})
    for library, expected_hash in sorted(library_manifest.items()):
        lib_path = ROOT / library
        if not lib_path.is_file():
            raise ValueError(f"missing hash-pinned Runtime library {library}")
        actual_hash = file_digest(lib_path)
        if actual_hash != expected_hash:
            raise ValueError(f"Runtime library hash differs from linked report: {library}")
        library_hashes[library] = actual_hash
    for text in sorted(full_text, key=lambda x: (x["library"], x["module"], rr.as_int(x["address"]))):
        library = text["library"]
        module = text["module"]
        text_address = rr.as_int(text["address"])
        key = _module_key(library, module, text_address)
        lib_path = ROOT / library
        if not lib_path.is_file():
            raise ValueError(f"missing Runtime library {library}")
        library_hashes[library] = file_digest(lib_path)
        if lib_path not in loaded:
            loaded[lib_path] = _archive_modules(lib_path, parser)
        objects, raw_modules = loaded[lib_path]
        obj = objects.get(module)
        if obj is None or module not in raw_modules:
            raise ValueError(f"Runtime module {key} is missing from archive")
        definitions, imports = rr.symbol_ids(raw_modules[module])
        obj.id_definitions = definitions
        obj.xrefs.update(imports)
        module_sections = []
        for sid, name in sorted(obj.sections.items(), key=lambda x: (x[1], x[0])):
            if name not in SECTION_NAMES:
                continue
            code = bytes(obj.code.get(sid, b""))
            patches = obj.patches.get(sid, [])
            sec_key = ("section_base", key, sid)
            address = text_address if name == ".text" else bindings.get(sec_key)
            if not code:
                result = {
                    "library": library, "module": module, "module_key": key,
                    "section": name, "section_id": int(sid), "address": _serialized_address(address),
                    "size": 0, "raw_sha256": digest(code), "reconstructed_sha256": digest(code),
                    "reference_sha256": None, "relocation_count": len(patches),
                    "applied_relocations": [], "unresolved_relocations": [],
                    "status": "empty_section",
                }
                section_results.append(result); module_sections.append(result)
                continue
            if address is None:
                # A content hit alone never supplies a placement; repeated
                # copies go to _resolve_unanchored_by_link_order().
                candidates = []
                for at in range(0, max(0, len(payload) - len(code) + 1), 4):
                    if payload[at:at + len(code)] == code:
                        candidates.append(load_address + at)
                status = "ambiguous_unanchored" if len(candidates) > 1 else "unmapped_section_base"
                result = {
                    "library": library, "module": module, "module_key": key,
                    "section": name, "section_id": int(sid), "address": None,
                    "size": len(code), "raw_sha256": digest(code), "reconstructed_sha256": None,
                    "reference_sha256": None, "relocation_count": len(patches),
                    "applied_relocations": [], "unresolved_relocations": [],
                    "candidate_addresses": [_serialized_address(x) for x in candidates],
                    "status": status,
                }
                unanchored[(key, int(sid))] = (obj, definitions)
                section_results.append(result); module_sections.append(result)
                continue
            try:
                reference = _reference_slice(payload, load_address, address, len(code))
                actual, applied, errors = _relocate_section(
                    code, patches, obj, key, address, text_address, bindings, conflicts, gp, imports)
                diffs = [i for i, (a, b) in enumerate(zip(actual, reference)) if a != b]
                if errors:
                    status = "unresolved"
                elif diffs:
                    status = "relocated_bytes_mismatch"
                else:
                    status = "full_match"
            except ValueError as exc:
                reference = b""
                actual = code
                applied = []
                errors = [{"error": str(exc)}]
                diffs = []
                status = "invalid_extent"
            result = {
                "library": library, "module": module, "module_key": key,
                "section": name, "section_id": int(sid), "address": _serialized_address(address),
                "size": len(code), "raw_sha256": digest(code), "reconstructed_sha256": digest(actual),
                "reference_sha256": digest(reference) if reference else None,
                "relocation_count": len(patches), "applied_relocations": applied,
                "unresolved_relocations": errors,
                "mismatch_count": len(diffs), "mismatch_offsets": diffs[:64],
                "status": status,
            }
            section_results.append(result); module_sections.append(result)
        module_results.append({
            "library": library, "module": module, "module_key": key,
            "text_address": text["address"], "text_status": text["status"],
            "text_size": text["text_size"], "sections": module_sections,
        })

    _resolve_unanchored_by_link_order(
        section_results, unanchored, full_text, linked, loaded, parser,
        library_manifest, payload, load_address, gp)

    supplemental_modules, supplemental_sections = _scan_data_only_modules(
        library_manifest, library_hashes, loaded, parser, payload, load_address,
        section_results)

    input_snapshot = _input_snapshot(inventory, runtime_symbols, linked, library_hashes)
    certified = [x for x in section_results if x["status"] == "full_match"]
    unique_ranges = []
    for row in certified:
        start = rr.as_int(row["address"])
        unique_ranges.append((start, int(row["size"])))
    unique_byte_count = rr.range_union_size(unique_ranges)
    supplemental_certified = [x for x in supplemental_sections if x["status"] == "full_match"]
    supplemental_ranges = [
        (rr.as_int(x["address"]), int(x["size"])) for x in supplemental_certified
    ]
    supplemental_status_counts = Counter(x["status"] for x in supplemental_sections)
    supplemental_unique_bytes = rr.range_union_size(supplemental_ranges)
    status_counts = Counter(x["status"] for x in section_results)
    return {
        "schema_version": 1,
        "inputs": input_snapshot,
        "text_module_count": len(full_text),
        "counts": {
            "section_candidates": len(section_results),
            "certified_sections": len(certified),
            "section_status_counts": dict(sorted(status_counts.items())),
            "relocations": sum(x["relocation_count"] for x in section_results),
            "applied_relocations": sum(len(x["applied_relocations"]) for x in section_results),
            "unresolved_relocations": sum(len(x["unresolved_relocations"]) for x in section_results),
        },
        "unique_bytes": {
            "certified_initialized_section_bytes": unique_byte_count,
            "overlap_deduplicated": True,
        },
        "module_results": module_results,
        "section_results": section_results,
        "supplemental_data_only_module_count": len(supplemental_modules),
        "supplemental_data_only_modules": supplemental_modules,
        "supplemental_data_only_section_results": supplemental_sections,
        "supplemental_data_only_counts": {
            "section_candidates": len(supplemental_sections),
            "matched_sections_excluding_aliases": len(supplemental_certified),
            "section_status_counts": dict(sorted(supplemental_status_counts.items())),
        },
        "supplemental_data_only_unique_bytes": supplemental_unique_bytes,
        "unanchored_identical_sections": [
            x for x in section_results if x["status"] == "ambiguous_unanchored"
        ],
        "classification_note": (
            "The existing section_results/counts/unique_bytes fields cover only complete "
            "initialized sections from full-match text modules. Supplemental data-only archive "
            "matches are separate; they require no nonempty text, no unsupported nonempty sections, "
            "no relocations, a unique aligned whole-section occurrence, and are excluded from the "
            "supplemental byte total when they overlap a text-module section."
        ),
    }


def validate_report(report):
    """Independently replay current evidence and reject any edited/stale report.

    Returns a small validation summary on success. Any key, value, section byte
    hash, address, status, count, source hash, or relocation edit raises ValueError.
    """
    if not isinstance(report, dict):
        raise ValueError("runtime data report must be a JSON object")
    expected = build_report()
    if report != expected:
        # Give callers a useful first divergence without accepting subsets.
        for key in sorted(set(report) | set(expected)):
            if report.get(key) != expected.get(key):
                raise ValueError(f"runtime data report differs from replay at {key}")
        raise ValueError("runtime data report differs from replay")
    return {
        "status": "validated",
        "section_candidates": expected["counts"]["section_candidates"],
        "certified_sections": expected["counts"]["certified_sections"],
        "certified_initialized_section_bytes": expected["unique_bytes"]["certified_initialized_section_bytes"],
    }


def validate_negative_cases(report):
    """Exercise report tampering and stale/malformed-evidence rejection paths."""
    cases = {}
    mutations = {}

    def alter(label, function):
        changed = json.loads(json.dumps(report))
        function(changed)
        try:
            validate_report(changed)
        except ValueError as exc:
            cases[label] = {"status": "rejected", "reason": str(exc)}
        else:
            cases[label] = {"status": "accepted"}

    nonempty_index = next((i for i, x in enumerate(report["section_results"]) if x["size"]), None)
    if nonempty_index is not None:
        alter("modified_section_bytes", lambda r: r["section_results"][nonempty_index].__setitem__("raw_sha256", "0" * 64))
        alter("mismatched_expected_address", lambda r: r["section_results"][nonempty_index].__setitem__("address", "0x80000000"))
        alter("unsupported_relocation_type", lambda r: r["section_results"][nonempty_index]["applied_relocations"].append({"type": 999, "offset": 0, "value": 0}))
        alter("contradictory_section_base", lambda r: r["section_results"][nonempty_index].__setitem__("address", "0x80000004"))
        alter("changed_status_or_count", lambda r: r["counts"].__setitem__("certified_sections", -1))
    else:
        raise ValueError("no nonempty section available for validation mutations")
    # A stale reference or library/source report is rejected by comparing its
    # pinned source evidence to the exact live snapshot.
    for field, label in (("executable_sha256", "stale_reference"),
                         ("runtime_linked_report_sha256", "stale_runtime_report"),
                         ("psyq_parser_sha256", "stale_parser")):
        alter(label, lambda r, f=field: r["inputs"].__setitem__(f, "0" * 64))
    alter("stale_archive", lambda r: r["inputs"]["runtime_library_sha256"].__setitem__(
        next(iter(r["inputs"]["runtime_library_sha256"])), "0" * 64))
    supplemental_index = next((i for i, x in enumerate(
        report.get("supplemental_data_only_section_results", []))
        if x.get("status") == "full_match"), None)
    if supplemental_index is not None:
        alter("supplemental_status_tampering", lambda r: r[
            "supplemental_data_only_section_results"][supplemental_index].__setitem__("status", "unverified"))
        alter("supplemental_address_tampering", lambda r: r[
            "supplemental_data_only_section_results"][supplemental_index].__setitem__("address", "0x80000000"))
        alter("supplemental_extent_tampering", lambda r: r[
            "supplemental_data_only_section_results"][supplemental_index].__setitem__("size", 1))
        alter("supplemental_bytes_hash_tampering", lambda r: r[
            "supplemental_data_only_section_results"][supplemental_index].__setitem__("raw_sha256", "0" * 64))
        alter("supplemental_occurrence_count_tampering", lambda r: r[
            "supplemental_data_only_section_results"][supplemental_index].__setitem__("occurrence_count", 2))
    else:
        raise ValueError("no standalone data-only match available for validation mutations")
    for label, function in cases.items():
        if function["status"] != "rejected":
            raise ValueError(f"negative validation case unexpectedly passed: {label}")
    # Exercise the actual relocation rejection branches on disposable objects;
    # report mutation alone only tests the replay equality guard.
    fixture = SimpleNamespace(xrefs={11: "missing"}, sections={2: ".text"})
    for label, patches, conflicts, reason in (
        ("reject_unsupported_patch", [(999, 0, ("c", 0))], set(), "unsupported relocation"),
        ("reject_unresolved_symbol", [(16, 0, (2, 11))], set(), "unresolved relocation"),
        ("reject_conflicting_symbol", [(16, 0, (2, 11))], {("symbol", "missing")}, "contradictory binding"),
        ("reject_cross_extent_patch", [(16, 2, ("c", 0))], set(), "outside complete section"),
    ):
        actual, applied, errors = _relocate_section(
            bytes(4), patches, fixture, "fixture", 0x80013000, 0x80050000,
            {}, conflicts, 0x8008cd74, fixture.xrefs)
        if actual != bytes(4) or applied or not errors or reason not in errors[0]["error"]:
            raise ValueError(f"relocation rejection branch failed: {label}")
        cases[label] = {"status": "rejected", "reason": errors[0]["error"]}
    return {"status": "validated", "negative_cases": cases}


def main():
    report = build_report()
    REPORTS.mkdir(parents=True, exist_ok=True)
    output = REPORTS / "runtime_data.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    validated = validate_report(load_json(output))
    negative = validate_negative_cases(report)
    validation = {
        "status": "validated",
        "runtime_data_report": "alien_decomp/reports/runtime_data.json",
        "runtime_data_report_sha256": file_digest(output),
        "independent_replay": validated,
        "negative_checks": negative["negative_cases"],
        "inputs": report["inputs"],
    }
    (REPORTS / "runtime_data_validation.json").write_text(
        json.dumps(validation, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "counts": report["counts"],
                      "unique_bytes": report["unique_bytes"],
                      "validation": validated}, indent=2))


if __name__ == "__main__":
    main()
