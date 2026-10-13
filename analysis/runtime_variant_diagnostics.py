"""Reproduce runtime archive and supplemental exact-module diagnostics.

This deliberately does not resolve ambiguous symbols from an individual
reference operand. It records archive variant evidence and independently
matched unrelocated module text omitted by the primary scanner's size filter.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import struct
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ANALYSIS = Path(__file__).resolve().parent
REPORT = Path(__file__).resolve().parents[1] / "reports" / "luna_runtime_next" / "runtime_variant_diagnostics.json"
SPEC = importlib.util.spec_from_file_location("psyqobj", ROOT / "shared-tools" / "psyqobj.py")
psyqobj = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(psyqobj)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_exe():
    inventory = json.loads((ANALYSIS / "inventory.json").read_text(encoding="utf-8"))
    path = ROOT / inventory["inputs"]["executable"]
    raw = path.read_bytes()
    load_address, size = struct.unpack_from("<II", raw, 0x18)
    return inventory, raw, load_address, raw[0x800:0x800 + size]


def lib_modules(path: Path):
    return [o for o in psyqobj.load(str(path)) if not isinstance(o, Exception)]


def module_fingerprint(path: Path, module_name: str, payload: bytes, load: int,
                       address: int):
    obj = next((o for o in lib_modules(path) if o.name == module_name), None)
    if obj is None:
        return {"present": False}
    text_id = next((sid for sid, name in obj.sections.items() if name == ".text"), None)
    text = bytes(obj.code.get(text_id, b"")) if text_id is not None else b""
    patches = obj.patches.get(text_id, []) if text_id is not None else []
    mask = {i for _, off, _ in patches for i in range(off & ~3, min(len(text), (off & ~3) + 4))}
    start = address - load
    reference = payload[start:start + len(text)]
    unmasked_equal = len(reference) == len(text) and all(
        text[i] == reference[i] for i in range(len(text)) if i not in mask
    )
    imported = sorted(set(obj.xrefs.values()))
    return {
        "present": True,
        "text_size": len(text),
        "text_sha256": sha(text),
        "relocation_count": len(patches),
        "unmasked_equal_at_3_3_candidate": unmasked_equal,
        "imports": imported,
        "definitions": [
            {"name": name, "section": obj.sections.get(sec), "offset": off}
            for name, sec, off in obj.xdefs
            if name in ("_spu_RXX", "_spu_keystat")
        ],
    }


def spu_rxx_layout(lib33, runtime, payload, load, gp):
    """Test each operand-implied _spu_RXX against independently placed SPU data.

    The 3.3 archive defines _spu_RXX once, at SPU .data+8, and both S_GKS and
    S_SCA import it with no addend, so one link gives both the same address.
    This places the neighbouring SPU data sections from bindings that other
    certified modules resolved, and lists every text reference to a word
    holding 0x1f801c00, to show which modules use which copy.
    """
    spec = importlib.util.spec_from_file_location("runtime_data_refs", ANALYSIS / "runtime_data.py")
    data_analyzer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(data_analyzer)
    objects = {o.name: o for o in lib_modules(lib33)}
    bound = {b["name_or_section"][0]: int(b["address"], 0) for b in runtime["symbol_bindings"]
             if b["kind"] == "symbol" and b["status"] == "resolved"}
    placements = {}
    for module in ("SPU", "S_M", "S_INI"):
        obj = objects[module]
        sid = next(s for s, n in obj.sections.items() if n == ".data")
        size = len(bytes(obj.code.get(sid, b"")))
        bases = sorted({bound[name] - off for name, sec, off in obj.xdefs
                        if sec == sid and name in bound})
        placements[module] = {
            "data_size": size,
            "bound_symbols": sorted(name for name, sec, _ in obj.xdefs if sec == sid and name in bound),
            "implied_bases": [f"0x{b:08x}" for b in bases],
        }
    modules = sorted((int(m["address"], 0), int(m["address"], 0) + m["text_size"], m["module"], m["status"])
                     for m in runtime["module_results"])
    text_lo, text_hi = modules[0][0], modules[-1][1]
    refs = data_analyzer._absolute_references(payload, load, gp, 0x80010000, text_hi)
    rxx_word = bytes.fromhex("001c801f")
    copies = [load + i for i in range(0, len(payload) - 3, 4) if payload[i:i + 4] == rxx_word]

    def owner(address):
        for start, end, name, status in modules:
            if start <= address < end:
                return f"{name} ({status})"
        return "unidentified text"

    words = []
    for copy in copies:
        sites = refs.get(copy, [])
        by_owner = defaultdict(list)
        for site in sites:
            by_owner[owner(site)].append(site)
        words.append({"address": f"0x{copy:08x}", "reference_count": len(sites),
                      "referencing_text": {k: [f"0x{min(v):08x}", f"0x{max(v):08x}", len(v)]
                                           for k, v in sorted(by_owner.items())}})
    s_m = placements["S_M"]["implied_bases"]
    s_ini = placements["S_INI"]["implied_bases"]
    checks = []
    for operand in (0x80073AFC, 0x80073CF0):
        base = operand - 8
        end = base + placements["SPU"]["data_size"]
        overlaps = []
        for module in ("S_M", "S_INI"):
            for value in placements[module]["implied_bases"]:
                other = int(value, 16)
                if base < other + placements[module]["data_size"] and other < end:
                    overlaps.append(module)
        checks.append({"operand": f"0x{operand:08x}", "implied_spu_data": [f"0x{base:08x}", f"0x{end:08x}"],
                       "overlaps_placed_data_of": overlaps})
    return {
        "placed_spu_data_sections": placements,
        "operand_checks": checks,
        "words_holding_0x1f801c00": words,
        "verdict": "unresolved",
        "conclusion": (
            "Neither operand is the 3.3 SPU module's _spu_RXX. 0x80073cf0 is a lone word between "
            "S_M .data (0x80073ce4..0x80073cf0) and S_INI .data (0x80073cf4..), both placed by "
            "certified relocations; a 96-byte SPU .data at 0x80073ce8 would overlap both. "
            "The bindings that certified S_R, S_W and S_M_* resolved for other SPU .data symbols imply "
            "two different SPU .data bases (0x80073c84 and 0x80073c8c), so the linked SPU module is "
            "not the 3.3 object either. 0x80073afc sits beside 0x80073af8, the word used by the unidentified SpuSetVoiceAttr "
            "text before S_GKS, and the main SPU text uses 0x80073c90 instead. Each word appears "
            "to be private to the module that uses it, in text order. The 3.3 objects import one "
            "global _spu_RXX, so no single link of these archive members reproduces both modules. "
            "Choosing either binding would be operand-selected, so both stay uncertified until a "
            "library build with module-local SPU base pointers is identified."),
    }


def spu_variant_build(payload, load):
    """Compare unidentified SPU text with every LIBSPU archive in the SDK tree.

    Scores are instruction-skeleton matches (opcode and function field), so
    they survive register, offset and relocation differences. A module with
    a near-complete skeleton match but no byte match is a different build of
    the same source, not a missing object.
    """
    import difflib
    spec = importlib.util.spec_from_file_location("runtime_data_skeleton", ANALYSIS / "runtime_data.py")
    data_analyzer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(data_analyzer)
    regions = {"SpuSetVoiceAttr text": (0x80058980, 0x800590A8, ("S_SVA",)),
               "main SPU text": (0x80059984, 0x8005BF54, ("SPU", "SPU_IO", "S_INI", "S_SR", "S_SRD", "S_SK"))}
    seen = set()
    libraries = []
    for lib in sorted((ROOT / "sdk").rglob("*")):
        if lib.name.upper() != "LIBSPU.LIB":
            continue
        digest = sha(lib.read_bytes())
        if digest not in seen:
            seen.add(digest)
            libraries.append((lib, digest))
    results = {}
    for label, (lo, hi, names) in regions.items():
        target = data_analyzer._skeleton(payload[lo - load:hi - load])
        rows = []
        for lib, digest in libraries:
            for obj in lib_modules(lib):
                if obj.name not in names:
                    continue
                text_id = next((sid for sid, n in obj.sections.items() if n == ".text"), None)
                source = data_analyzer._skeleton(bytes(obj.code.get(text_id, b"")))
                if not source:
                    continue
                matcher = difflib.SequenceMatcher(None, source, target, autojunk=False)
                matched = sum(b.size for b in matcher.get_matching_blocks())
                rows.append({"library": str(lib.relative_to(ROOT)), "library_sha256": digest,
                             "module": obj.name, "object_words": len(source),
                             "matched_skeleton_words": matched,
                             "object_fraction": round(matched / len(source), 4)})
        best = {}
        for row in rows:
            if row["module"] not in best or row["object_fraction"] > best[row["module"]]["object_fraction"]:
                best[row["module"]] = row
        results[label] = {"range": [f"0x{lo:08x}", f"0x{hi:08x}"], "region_words": len(target),
                          "best_by_module": [best[k] for k in sorted(best)]}
    return {
        "libraries_scanned": len(libraries),
        "regions": results,
        "conclusion": (
            "The SpuSetVoiceAttr text at 0x80058980 is Runtime 3.3 S_SVA source (416 of 420 "
            "skeleton words) built differently: the two _spu_FsetRXXa calls are expanded inline "
            "(divide by _spu_mem_mode_unit at 0x80073cb4, shift by _spu_mem_mode_plus at "
            "0x80073cb8, store through the private base word 0x80073af8), and the SPU .data "
            "layout puts mem_mode_unit before mem_mode_plus, unlike 3.3 (plus, then unit) and "
            "3.6 and later (plus at +40, unit at +44 after a moved _spu_RXX). The main SPU text "
            "is the same family (SPU_IO 97% and SPU 90% of their skeleton words, best against 3.3 "
            "and LIB34). No PsyQ-format LIBSPU.LIB in the SDK tree (all 16 distinct archives, "
            "including LIB34 and the 3.5/3.6/3.7 OLD_LIBS) has these bytes. The COFF .A archives "
            "(CodeWarrior and the snd403/snd404 patches) are a different compiler and are not "
            "parsed. The build is not available, so nothing here can be certified."),
    }


def main():
    inventory, raw, load, payload = load_exe()
    base = ROOT / inventory["inputs"]["runtime_root"]
    selected = {
        "3.0": next(base.parent.glob("Programmer Tool - Runtime Library Version 3.0*"))
              / "LZH" / "LZH_PSX0314.LZH" / "PSX" / "LIB" / "LIBSPU.LIB",
        "3.3": base / "LZH" / "PSX1020.LZH" / "PSX" / "LIB" / "LIBSPU.LIB",
        "3.5": next(base.parent.glob("Programmer Tool - Runtime Library Version 3.5*"))
              / "LZH" / "PSX35.LZH" / "PSX" / "LIB" / "LIBSPU.LIB",
    }
    candidate_addresses = {"S_GKS": 0x800590A8, "S_SCA": 0x8005BF9C}
    variants = {}
    for version, lib in selected.items():
        variants[version] = {
            "library": str(lib.relative_to(ROOT)),
            "library_sha256": sha(lib.read_bytes()),
            "modules": {
                name: module_fingerprint(lib, name, payload, load, address)
                for name, address in candidate_addresses.items()
            },
        }

    # The complete Runtime 3.3 archive's SPU member independently documents
    # that _spu_RXX is at .data+8. This is object evidence, not a linked base.
    lib33 = selected["3.3"]
    spu = next(o for o in lib_modules(lib33) if o.name == "SPU")
    spu_data_id = next(sid for sid, name in spu.sections.items() if name == ".data")
    spu_data = bytes(spu.code.get(spu_data_id, b""))
    rxx_definition = next(
        {"name": name, "section": spu.sections.get(sec), "offset": off}
        for name, sec, off in spu.xdefs if name == "_spu_RXX"
    )

    symbols = json.loads((ANALYSIS / "runtime_symbols.json").read_text(encoding="utf-8"))
    supplemental = [m for m in symbols["modules"]
                    if m.get("discovery") == "supplemental_unique_full_text_scan"]

    runtime = json.loads((ANALYSIS / "runtime_linked.json").read_text(encoding="utf-8"))
    conflict = next(b for b in runtime["symbol_bindings"]
                    if b["name_or_section"] == ["_spu_RXX"])
    module_results = {(m["library"], m["module"], m["address"]): m
                      for m in runtime["module_results"]}
    supplemental_relocated = []
    for candidate in symbols["modules"]:
        if candidate.get("discovery") != "supplemental_unique_relocation_masked_text_scan":
            continue
        result = module_results[(candidate["library"], candidate["module"], candidate["address"])]
        supplemental_relocated.append({
            "candidate": candidate,
            "resolution": {
                "status": result["status"],
                "text_size": result["text_size"],
                "relocation_count": result["relocation_count"],
                "mismatch_count": result["mismatch_count"],
                "unresolved_relocations": result["unresolved_relocations"],
                "imported_symbols": result["imported_symbols"],
                "reconstructed_sha256": result["reconstructed_sha256"],
                "reference_sha256": result["reference_sha256"],
                "hilo_pairs": result["hilo_pairs"],
            },
        })
    rxx_byte_checks = []
    for evidence in conflict["evidence"]:
        address = evidence["address"]
        if f"0x{address:08x}" in {x["inferred_symbol_address"] for x in rxx_byte_checks}:
            continue
        data_base = address - 8
        offset = data_base - load
        reference_window = payload[offset:offset + len(spu_data)]
        reference_word = payload[address - load:address - load + 4]
        rxx_byte_checks.append({
            "inferred_symbol_address": f"0x{address:08x}",
            "implied_spu_data_base": f"0x{data_base:08x}",
            "archive_data_plus_8": spu_data[8:12].hex(),
            "reference_word_at_symbol": reference_word.hex(),
            "word_matches_archive_data_plus_8": reference_word == spu_data[8:12],
            "identical_bytes_in_full_spu_data_window": sum(
                a == b for a, b in zip(spu_data, reference_window)
            ),
            "full_spu_data_window_size": len(spu_data),
            "full_spu_data_window_matches": reference_window == spu_data,
        })
    result = {
        "executable_sha256": sha(raw),
        "runtime_3_3_primary_module_results": {
            "full_match_modules": runtime["counts"].get("exact_module_matches"),
            "relocated_exact_modules": runtime["counts"].get("relocated_exact_modules"),
            "unresolved_modules": sum(m["status"] == "unresolved" for m in runtime["module_results"]),
        },
        "runtime_symbol_inventory": {
            "module_records": len(symbols["modules"]),
            "byte_exact_unrelocated_module_records": symbols["byte_exact_unrelocated_modules"],
            "unique_byte_exact_unrelocated_text_bytes": symbols["byte_exact_unrelocated_module_bytes"],
        },
        "supplemental_relocated_module_candidates": supplemental_relocated,
        "spu_object_definition": {
            "library": str(lib33.relative_to(ROOT)),
            "library_sha256": sha(lib33.read_bytes()),
            "module": "SPU",
            "data_section_size": len(spu_data),
            "data_section_sha256": sha(spu_data),
            "_spu_RXX": rxx_definition,
            "interpretation": "The archive establishes the symbol's object offset only; it does not establish the linked section base.",
        },
        "conflict": {
            "status": conflict["status"],
            "evidence": [
                {"address": f"0x{item['address']:08x}", **item["evidence"]}
                for item in conflict["evidence"]
            ],
            "certification_decision": "unresolved; no per-module override or operand-selected binding",
        },
        "spu_rxx_archive_data_cross_check": {
            "object_definition": rxx_definition,
            "object_data_sha256": sha(spu_data),
            "candidate_windows": rxx_byte_checks,
            "conclusion": "Both operand-implied addresses contain the same archive .data+8 word, 0x1f801c00. Neither surrounding 96-byte window matches the full SPU .data section, so object contents do not independently select a linked base.",
        },
        "spu_rxx_layout": spu_rxx_layout(
            lib33, runtime, payload, load, int(inventory["inputs"]["startup_gp_observed"], 0)),
        "spu_variant_build": spu_variant_build(payload, load),
        "archive_variants": variants,
        "supplemental_small_exact_module_matches": supplemental,
        "small_module_scan_policy": "Runtime 3.3 library .text lengths 1..48 bytes were checked for unique, aligned, full-byte occurrences. Records overlapping any inventory candidate or already certified exact range were excluded; same-range duplicate objects are grouped as aliases and count once.",
        "method_note": "Variant comparison checks complete text sizes, object hashes, relocation counts, and unmasked bytes at the 3.3 candidate placements. Supplemental unrelocated matches compare every text byte and require alignment and a unique occurrence. Supplemental relocated candidates require a unique aligned whole-text masked match, then runtime_relocations must resolve all relocation bytes to an exact full match.",
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {REPORT}; supplemental modules={len(supplemental)}")


if __name__ == "__main__":
    main()
