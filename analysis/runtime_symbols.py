"""Expand Runtime 3.3 module candidates into evidence-backed exported symbols.

Modules without relocations are compared directly and can be certified as
library object text matches. They are distinct from recovered C functions.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("psyqobj", ROOT / "shared-tools/psyqobj.py")
psyqobj = importlib.util.module_from_spec(spec)
spec.loader.exec_module(psyqobj)


def main():
    inventory = json.loads((OUT / "inventory.json").read_text())
    raw = (ROOT / inventory["inputs"]["executable"]).read_bytes()
    load_address, image_size = struct.unpack_from("<II", raw, 0x18)
    payload = raw[0x800:0x800 + image_size]
    libs = {}
    modules = []
    exports = []
    exact_ranges = []
    candidates = list(inventory["runtime33_matches"])
    # Derive standalone startup placement from its SDK entry export and EXE
    # entry address. This is a candidate only; complete relocation replay is
    # required before its full exact .text extent earns runtime coverage.
    startup = ROOT / inventory["inputs"]["runtime_root"] / "LZH/PSX1020.LZH/PSX/LIB/2MBYTE.OBJ"
    startup_obj = psyqobj.load(str(startup))[0]
    if isinstance(startup_obj, Exception):
        raise ValueError("invalid Runtime 3.3 standalone startup object")
    startup_text = next(s for s,n in startup_obj.sections.items() if n == ".text")
    entries = [off for n,s,off in startup_obj.xdefs if n == "__SN_ENTRY_POINT" and s == startup_text]
    if len(entries) != 1:
        raise ValueError("startup entry export is ambiguous")
    entry = struct.unpack_from("<I", raw, 0x10)[0]
    candidates.append({"library":str(startup.relative_to(ROOT)),"module":startup.name,
                       "text_size":len(startup_obj.code[startup_text]),
                       "exe_address":hex(entry - entries[0])})
    for candidate in candidates:
        path = ROOT / candidate["library"]
        if path not in libs:
            libs[path] = psyqobj.load(str(path))
            if path.read_bytes()[:4] == b"LNK\x02":
                for obj in libs[path]:
                    if not isinstance(obj, Exception): obj.name = path.name
        for obj in libs[path]:
            if isinstance(obj, Exception) or obj.name != candidate["module"]:
                continue
            section = next((sn for sn, name in obj.sections.items() if name == ".text"), None)
            if section is None:
                continue
            code = bytes(obj.code.get(section, b""))
            if len(code) != candidate["text_size"]:
                continue
            address = int(candidate["exe_address"], 0)
            offset = address - load_address
            original = payload[offset:offset + len(code)]
            if len(original) != len(code):
                raise ValueError("module outside executable")
            patches = obj.patches.get(section, [])
            masked = bytearray(b"\x01" * len(code))
            for _, patch_offset, _ in patches:
                for i in range(patch_offset & ~3, min(len(code), (patch_offset & ~3) + 4)):
                    masked[i] = 0
            if any(masked[i] and code[i] != original[i] for i in range(len(code))):
                continue
            byte_exact = not patches and code == original
            if byte_exact:
                exact_ranges.append((address, address + len(code)))
            evidence = {"library": candidate["library"], "module": obj.name,
                        "address": hex(address), "size": len(code), "relocation_count": len(patches),
                        "byte_exact_object_text": byte_exact,
                        "object_text_sha256": hashlib.sha256(code).hexdigest(),
                        "reference_text_sha256": hashlib.sha256(original).hexdigest()}
            modules.append(evidence)
            defs = sorted((off, name) for name, sec, off in obj.xdefs if sec == section)
            for i, (off, name) in enumerate(defs):
                next_off = next((n for n, _ in defs[i + 1:] if n > off), len(code))
                exports.append({"name": name, "address": hex(address + off),
                                "span_to_next_export": next_off - off,
                                "span_is_verified_function_size": False,
                                "library": candidate["library"], "module": obj.name,
                                "module_byte_exact_object_text": byte_exact,
                                "evidence": "unrelocated module .text equality" if byte_exact else "relocation-masked module .text equality"})
            break

    # The original inventory intentionally excludes modules smaller than 48
    # bytes from its relocation-masked candidate scan. Independently scan
    # every module with a full text of 1..48 bytes and include exact whole-text
    # matches. A match must be aligned and unique in the executable payload.
    # Overlaps with inventory candidates or already certified text are omitted;
    # duplicate aliases at one address are grouped so each byte range counts
    # once. No relocation fields are masked or inferred in this supplemental
    # pass.
    seen_libraries = set()
    protected_ranges = list(exact_ranges)
    for candidate in inventory["runtime33_matches"]:
        start = int(candidate["exe_address"], 0)
        protected_ranges.append((start, start + int(candidate["text_size"])))
    supplemental = {}
    for lib in sorted((ROOT / inventory["inputs"]["runtime_root"]).rglob("*.LIB")):
        digest = hashlib.sha256(lib.read_bytes()).hexdigest()
        if digest in seen_libraries:
            continue
        seen_libraries.add(digest)
        for obj in psyqobj.load(str(lib)):
            if isinstance(obj, Exception):
                continue
            section = next((sid for sid, name in obj.sections.items() if name == ".text"), None)
            if section is None or obj.patches.get(section, []):
                continue
            code = bytes(obj.code.get(section, b""))
            if not 0 < len(code) <= 48:
                continue
            aligned_hits = []
            first = payload.find(code)
            while first >= 0:
                if first % 4 == 0:
                    aligned_hits.append(first)
                first = payload.find(code, first + 1)
            if len(aligned_hits) != 1:
                continue
            first = aligned_hits[0]
            address = load_address + first
            end = address + len(code)
            if any(address < b and a < end for a, b in protected_ranges):
                continue
            identity = (address, end, hashlib.sha256(code).hexdigest())
            supplemental.setdefault(identity, []).append((lib, obj, section, code))

    accepted_ranges = list(protected_ranges)
    for (address, end, code_hash), aliases in sorted(supplemental.items()):
            if any(address < b and a < end for a, b in accepted_ranges):
                continue
            # Prefer a descriptive archive member name over opaque short IDs
            # when duplicate modules export the same text at the same address.
            aliases.sort(key=lambda item: (bool(re.fullmatch(r"[A-Z]\d+", item[1].name)), item[1].name, str(item[0])))
            lib, obj, section, code = aliases[0]
            offset = address - load_address
            original = payload[offset:offset + len(code)]
            alias_records = []
            seen_aliases = {(obj.name, str(lib))}
            for alias_lib, alias_obj, _, _ in aliases[1:]:
                key = (alias_obj.name, str(alias_lib))
                if key in seen_aliases:
                    continue
                seen_aliases.add(key)
                alias_records.append({"module": alias_obj.name,
                                      "library": str(alias_lib.relative_to(ROOT))})
            evidence = {"library": str(lib.relative_to(ROOT)), "module": obj.name,
                        "address": hex(address), "size": len(code),
                        "relocation_count": len(obj.patches.get(section, [])), "byte_exact_object_text": True,
                        "object_text_sha256": code_hash,
                        "reference_text_sha256": hashlib.sha256(original).hexdigest(),
                        "discovery": "supplemental_unique_full_text_scan",
                        "exported_symbols": [name for name, sec, _ in obj.xdefs if sec == section],
                        "imported_symbols": sorted(set(obj.xrefs.values())),
                        "duplicate_module_aliases": alias_records}
            modules.append(evidence)
            exact_ranges.append((address, address + len(code)))
            accepted_ranges.append((address, end))
            defs = sorted((off, name) for name, sec, off in obj.xdefs if sec == section)
            for i, (off, name) in enumerate(defs):
                next_off = next((n for n, _ in defs[i + 1:] if n > off), len(code))
                exports.append({"name": name, "address": hex(address + off),
                                "span_to_next_export": next_off - off,
                                "span_is_verified_function_size": False,
                                "library": str(lib.relative_to(ROOT)), "module": obj.name,
                                "module_byte_exact_object_text": True,
                                "evidence": "unique full-byte module .text equality"})

    # Relax the inventory's literal-byte threshold for larger relocated
    # modules. Require a unique full relocation-masked placement, at least 16
    # unmasked bytes, an 8-byte literal anchor, and no overlap with any
    # inventory/supplemental range. Relocation bytes are never counted as
    # exact here; runtime_relocations.py must resolve all of them before a
    # module can enter exact coverage.
    relocated_candidates = {}
    relocated_seen_libraries = set()
    for lib in sorted((ROOT / inventory["inputs"]["runtime_root"]).rglob("*.LIB")):
        digest = hashlib.sha256(lib.read_bytes()).hexdigest()
        if digest in relocated_seen_libraries:
            continue
        relocated_seen_libraries.add(digest)
        for obj in psyqobj.load(str(lib)):
            if isinstance(obj, Exception):
                continue
            section = next((sid for sid, name in obj.sections.items() if name == ".text"), None)
            if section is None:
                continue
            code = bytes(obj.code.get(section, b""))
            patches = obj.patches.get(section, [])
            if len(code) < 48 or not patches:
                continue
            mask = bytearray(b"\x01" * len(code))
            for _, off, _ in patches:
                for i in range(off & ~3, min(len(code), (off & ~3) + 4)):
                    mask[i] = 0
            unmasked = sum(mask)
            if unmasked < 16 or unmasked / len(code) < 0.25:
                continue
            runs = []
            i = 0
            while i < len(mask):
                if mask[i]:
                    j = i
                    while j < len(mask) and mask[j]:
                        j += 1
                    if j - i >= 8:
                        runs.append((i, j))
                    i = j
                else:
                    i += 1
            if not runs:
                continue
            anchor_start, anchor_end = max(runs, key=lambda pair: pair[1] - pair[0])
            needle = code[anchor_start:anchor_end]
            aligned_hits = []
            pos = payload.find(needle)
            while pos >= 0:
                start = pos - anchor_start
                if (start >= 0 and start % 4 == 0 and start + len(code) <= len(payload)
                        and all(not mask[k] or code[k] == payload[start + k]
                                for k in range(len(code)))):
                    aligned_hits.append(start)
                pos = payload.find(needle, pos + 1)
            if len(aligned_hits) != 1:
                continue
            start = aligned_hits[0]
            address = load_address + start
            end = address + len(code)
            if any(address < b and a < end for a, b in accepted_ranges):
                continue
            accepted_ranges.append((address, end))
            identity = (address, end, hashlib.sha256(code).hexdigest())
            relocated_candidates.setdefault(identity, []).append(
                (lib, obj, section, code, len(patches), unmasked))

    for (address, end, code_hash), aliases in sorted(relocated_candidates.items()):
        aliases.sort(key=lambda item: (bool(re.fullmatch(r"[A-Z]\d+", item[1].name)), item[1].name, str(item[0])))
        lib, obj, section, code, relocation_count, unmasked = aliases[0]
        evidence = {"library": str(lib.relative_to(ROOT)), "module": obj.name,
                    "address": hex(address), "size": len(code),
                    "relocation_count": relocation_count,
                    "unmasked_byte_count": unmasked,
                    "unmasked_fraction": round(unmasked / len(code), 4),
                    "byte_exact_object_text": False,
                    "object_text_sha256": code_hash,
                    "discovery": "supplemental_unique_relocation_masked_text_scan",
                    "exported_symbols": [name for name, sec, _ in obj.xdefs if sec == section],
                    "imported_symbols": sorted(set(obj.xrefs.values())),
                    "duplicate_module_aliases": [
                        {"module": alias_obj.name, "library": str(alias_lib.relative_to(ROOT))}
                        for alias_lib, alias_obj, _, _, _, _ in aliases[1:]
                    ]}
        modules.append(evidence)
        defs = sorted((off, name) for name, sec, off in obj.xdefs if sec == section)
        for i, (off, name) in enumerate(defs):
            next_off = next((n for n, _ in defs[i + 1:] if n > off), len(code))
            exports.append({"name": name, "address": hex(address + off),
                            "span_to_next_export": next_off - off,
                            "span_is_verified_function_size": False,
                            "library": str(lib.relative_to(ROOT)), "module": obj.name,
                            "module_byte_exact_object_text": False,
                            "evidence": "unique full relocation-masked module .text equality"})

    # Include very small relocated text sections when a long literal run gives
    # a unique aligned placement. The size threshold used above misses these
    # complete sections even though their relocation bytes can be replayed by
    # runtime_relocations.py. Protect every currently registered C function
    # from newly discovered SDK ranges.
    c_ranges = []
    verifier_spec = importlib.util.spec_from_file_location(
        "registered_c_units", OUT.parent / "tools/verify.py")
    verifier = importlib.util.module_from_spec(verifier_spec)
    verifier_spec.loader.exec_module(verifier)
    verifier.validate_function_ownership(verifier.UNITS)
    for _, _, map_path in verifier.UNITS:
        for item in json.loads(map_path.read_text())["functions"].values():
            start = int(item["address"], 0)
            c_ranges.append((start, start + int(item["size"])))
    small_candidates = {}
    small_seen_libraries = set()
    for lib in sorted((ROOT / inventory["inputs"]["runtime_root"]).rglob("*.LIB")):
        digest = hashlib.sha256(lib.read_bytes()).hexdigest()
        if digest in small_seen_libraries:
            continue
        small_seen_libraries.add(digest)
        for obj in psyqobj.load(str(lib)):
            if isinstance(obj, Exception):
                continue
            section = next((sid for sid, name in obj.sections.items() if name == ".text"), None)
            if section is None:
                continue
            code = bytes(obj.code.get(section, b""))
            patches = obj.patches.get(section, [])
            if not patches or not 0 < len(code) < 48:
                continue
            mask = bytearray(b"\x01" * len(code))
            for _, off, _ in patches:
                if off < 0 or off + 4 > len(code):
                    raise ValueError(f"{obj.name}: relocation outside .text at {off:#x}")
                for i in range(off & ~3, min(len(code), (off & ~3) + 4)):
                    mask[i] = 0
            runs = []
            i = 0
            while i < len(mask):
                if mask[i]:
                    j = i
                    while j < len(mask) and mask[j]:
                        j += 1
                    if j - i >= 8:
                        runs.append((i, j))
                    i = j
                else:
                    i += 1
            if sum(mask) < 8 or not runs:
                continue
            anchor_start, anchor_end = max(runs, key=lambda pair: pair[1] - pair[0])
            needle = code[anchor_start:anchor_end]
            hits = []
            pos = payload.find(needle)
            while pos >= 0:
                start = pos - anchor_start
                if (start >= 0 and start % 4 == 0 and start + len(code) <= len(payload)
                        and all(not mask[k] or code[k] == payload[start + k]
                                for k in range(len(code)))):
                    hits.append(start)
                pos = payload.find(needle, pos + 1)
            if len(hits) != 1:
                continue
            address = load_address + hits[0]
            end = address + len(code)
            if any(address < b and a < end for a, b in accepted_ranges + c_ranges):
                continue
            identity = (address, end, hashlib.sha256(code).hexdigest())
            small_candidates.setdefault(identity, []).append(
                (lib, obj, section, code, len(patches), sum(mask), anchor_start, anchor_end))

    for (address, end, code_hash), aliases in sorted(small_candidates.items()):
        if any(address < b and a < end for a, b in accepted_ranges + c_ranges):
            continue
        accepted_ranges.append((address, end))
        aliases.sort(key=lambda item: (bool(re.fullmatch(r"[A-Z]\d+", item[1].name)), item[1].name, str(item[0])))
        lib, obj, section, code, relocation_count, unmasked, anchor_start, anchor_end = aliases[0]
        evidence = {"library": str(lib.relative_to(ROOT)), "module": obj.name,
                    "address": hex(address), "size": len(code),
                    "relocation_count": relocation_count,
                    "unmasked_byte_count": unmasked,
                    "unmasked_fraction": round(unmasked / len(code), 4),
                    "anchor_offset": anchor_start, "anchor_size": anchor_end - anchor_start,
                    "byte_exact_object_text": False,
                    "object_text_sha256": code_hash,
                    "discovery": "supplemental_unique_small_relocation_masked_text_scan",
                    "exported_symbols": [name for name, sec, _ in obj.xdefs if sec == section],
                    "imported_symbols": sorted(set(obj.xrefs.values())),
                    "duplicate_module_aliases": [
                        {"module": alias_obj.name, "library": str(alias_lib.relative_to(ROOT))}
                        for alias_lib, alias_obj, _, _, _, _, _, _ in aliases[1:]
                    ]}
        modules.append(evidence)
        defs = sorted((off, name) for name, sec, off in obj.xdefs if sec == section)
        for i, (off, name) in enumerate(defs):
            next_off = next((n for n, _ in defs[i + 1:] if n > off), len(code))
            exports.append({"name": name, "address": hex(address + off),
                            "span_to_next_export": next_off - off,
                            "span_is_verified_function_size": False,
                            "library": str(lib.relative_to(ROOT)), "module": obj.name,
                            "module_byte_exact_object_text": False,
                            "evidence": "unique small relocation-masked module .text equality"})
    # Small relocated modules whose literal bytes recur too often for an anchor
    # (for example getter/setter pairs) can still have one placement where
    # every relocation agrees. Accept a module only when all its text
    # relocations are %hi/%lo pairs on its own sections, every pair implies the
    # same base per section, each implied non-text section holds the object's
    # bytes, and exactly one aligned placement in the payload satisfies this.
    def hilo_value(word_hi, word_lo):
        lo = word_lo & 0xffff
        return (((word_hi & 0xffff) << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xffffffff

    consistent_candidates = {}
    consistent_seen_libraries = set()
    for lib in sorted((ROOT / inventory["inputs"]["runtime_root"]).rglob("*.LIB")):
        digest = hashlib.sha256(lib.read_bytes()).hexdigest()
        if digest in consistent_seen_libraries:
            continue
        consistent_seen_libraries.add(digest)
        for obj in psyqobj.load(str(lib)):
            if isinstance(obj, Exception):
                continue
            section = next((sid for sid, name in obj.sections.items() if name == ".text"), None)
            if section is None:
                continue
            code = bytes(obj.code.get(section, b""))
            patches = obj.patches.get(section, [])
            if not patches or not 0 < len(code) < 48 or len(patches) % 2:
                continue
            pairs = []
            for (t_hi, o_hi, e_hi), (t_lo, o_lo, e_lo) in zip(patches[::2], patches[1::2]):
                if (t_hi, t_lo) != (82, 84) or e_hi != e_lo:
                    break
                expr = e_hi
                if expr[0] == 4:
                    sid, addend = expr[1], 0
                elif (expr[0] == 44 and expr[1][0] == 4 and expr[2][0] == "c"):
                    sid, addend = expr[1][1], expr[2][1]
                else:
                    break
                data = bytes(obj.code.get(sid, b""))
                if sid == section or obj.sections.get(sid) not in (".data", ".sdata", ".rdata") or not data:
                    break
                pairs.append((o_hi, o_lo, sid, addend))
            else:
                mask = bytearray(b"\x01" * len(code))
                for _, off, _ in patches:
                    for i in range(off & ~3, min(len(code), (off & ~3) + 4)):
                        mask[i] = 0
                hits = []
                for start in range(0, len(payload) - len(code) + 1, 4):
                    if any(mask[k] and code[k] != payload[start + k] for k in range(len(code))):
                        continue
                    bases = {}
                    for o_hi, o_lo, sid, addend in pairs:
                        hi = struct.unpack_from("<I", payload, start + o_hi)[0]
                        lo = struct.unpack_from("<I", payload, start + o_lo)[0]
                        base = (hilo_value(hi, lo) - addend) & 0xffffffff
                        if bases.setdefault(sid, base) != base:
                            break
                    else:
                        if all(0 <= base - load_address and
                               payload[base - load_address:base - load_address + len(obj.code[sid])] == bytes(obj.code[sid])
                               for sid, base in bases.items()):
                            hits.append((start, bases))
                if len(hits) != 1:
                    continue
                start, bases = hits[0]
                address = load_address + start
                end = address + len(code)
                identity = (address, end, hashlib.sha256(code).hexdigest())
                consistent_candidates.setdefault(identity, []).append(
                    (lib, obj, section, code, len(patches), sum(mask), bases))

    for (address, end, code_hash), aliases in sorted(consistent_candidates.items()):
        if any(address < b and a < end for a, b in accepted_ranges + c_ranges):
            continue
        accepted_ranges.append((address, end))
        aliases.sort(key=lambda item: (bool(re.fullmatch(r"[A-Z]\d+", item[1].name)), item[1].name, str(item[0])))
        lib, obj, section, code, relocation_count, unmasked, bases = aliases[0]
        evidence = {"library": str(lib.relative_to(ROOT)), "module": obj.name,
                    "address": hex(address), "size": len(code),
                    "relocation_count": relocation_count,
                    "unmasked_byte_count": unmasked,
                    "unmasked_fraction": round(unmasked / len(code), 4),
                    "implied_section_bases": {obj.sections[sid]: hex(base) for sid, base in sorted(bases.items())},
                    "byte_exact_object_text": False,
                    "object_text_sha256": code_hash,
                    "discovery": "supplemental_unique_self_consistent_relocation_scan",
                    "exported_symbols": [name for name, sec, _ in obj.xdefs if sec == section],
                    "imported_symbols": sorted(set(obj.xrefs.values())),
                    "duplicate_module_aliases": [
                        {"module": alias[1].name, "library": str(alias[0].relative_to(ROOT))}
                        for alias in aliases[1:]
                    ]}
        modules.append(evidence)
        defs = sorted((off, name) for name, sec, off in obj.xdefs if sec == section)
        for i, (off, name) in enumerate(defs):
            next_off = next((n for n, _ in defs[i + 1:] if n > off), len(code))
            exports.append({"name": name, "address": hex(address + off),
                            "span_to_next_export": next_off - off,
                            "span_is_verified_function_size": False,
                            "library": str(lib.relative_to(ROOT)), "module": obj.name,
                            "module_byte_exact_object_text": False,
                            "evidence": "unique self-consistent relocation-masked module .text equality"})
    # Place small relocated modules by the call sites of already placed
    # modules. A jal in a placed module whose relocation imports an exported
    # symbol of a small relocated module fixes that module's address. Every
    # such call site must agree, the unrelocated bytes must equal the
    # executable there, and the range must not overlap anything accepted or any
    # registered C function. The literal-anchor scans above cannot place these
    # modules because their unrelocated bytes are only a return and a nop.
    call_seen_libraries = set()
    call_index = {}
    for lib in sorted((ROOT / inventory["inputs"]["runtime_root"]).rglob("*.LIB")):
        digest = hashlib.sha256(lib.read_bytes()).hexdigest()
        if digest in call_seen_libraries:
            continue
        call_seen_libraries.add(digest)
        for obj in psyqobj.load(str(lib)):
            if isinstance(obj, Exception):
                continue
            section = next((sid for sid, name in obj.sections.items() if name == ".text"), None)
            if section is None:
                continue
            code = bytes(obj.code.get(section, b""))
            if not obj.patches.get(section, []) or not 0 < len(code) < 48:
                continue
            for name, sec, off in obj.xdefs:
                if sec == section and off == 0:
                    call_index.setdefault(name, []).append((lib, obj, section, code))
    call_targets = {}
    object_cache = {}
    for placed in list(modules):
        placed_lib = ROOT / placed["library"]
        if placed_lib not in object_cache:
            object_cache[placed_lib] = {o.name: o for o in psyqobj.load(str(placed_lib))
                                        if not isinstance(o, Exception)}
        placed_obj = object_cache[placed_lib].get(placed["module"])
        if placed_obj is None:
            continue
        placed_section = next((sid for sid, name in placed_obj.sections.items() if name == ".text"), None)
        placed_address = int(placed["address"], 0)
        for kind, off, expr in placed_obj.patches.get(placed_section, []):
            if kind != 74 or expr[0] != 2 or placed_obj.xrefs.get(expr[1]) not in call_index:
                continue
            word = struct.unpack_from("<I", payload, placed_address - load_address + off)[0]
            if word >> 26 != 3:
                continue
            target = ((placed_address + off) & 0xf0000000) | ((word & 0x3ffffff) << 2)
            call_targets.setdefault(placed_obj.xrefs[expr[1]], set()).add(target)
    for name, targets in sorted(call_targets.items()):
        owners = call_index[name]
        if len(targets) != 1 or len({(o[3], o[1].name) for o in owners}) != 1:
            continue
        address = next(iter(targets))
        lib, obj, section, code = sorted(owners, key=lambda item: str(item[0]))[0]
        patches = obj.patches.get(section, [])
        mask = bytearray(b"\x01" * len(code))
        for _, off, _ in patches:
            for i in range(off & ~3, min(len(code), (off & ~3) + 4)):
                mask[i] = 0
        start = address - load_address
        end = address + len(code)
        if (address % 4 or start < 0 or start + len(code) > len(payload)
                or any(mask[k] and code[k] != payload[start + k] for k in range(len(code)))
                or any(address < b and a < end for a, b in accepted_ranges + c_ranges)):
            continue
        accepted_ranges.append((address, end))
        evidence = {"library": str(lib.relative_to(ROOT)), "module": obj.name,
                    "address": hex(address), "size": len(code),
                    "relocation_count": len(patches),
                    "unmasked_byte_count": sum(mask),
                    "unmasked_fraction": round(sum(mask) / len(code), 4),
                    "byte_exact_object_text": False,
                    "object_text_sha256": hashlib.sha256(code).hexdigest(),
                    "discovery": "supplemental_call_target_relocation_masked_text_scan",
                    "exported_symbols": [n for n, sec, _ in obj.xdefs if sec == section],
                    "imported_symbols": sorted(set(obj.xrefs.values())),
                    "duplicate_module_aliases": []}
        modules.append(evidence)
        defs = sorted((off, n) for n, sec, off in obj.xdefs if sec == section)
        for i, (off, n) in enumerate(defs):
            next_off = next((m for m, _ in defs[i + 1:] if m > off), len(code))
            exports.append({"name": n, "address": hex(address + off),
                            "span_to_next_export": next_off - off,
                            "span_is_verified_function_size": False,
                            "library": str(lib.relative_to(ROOT)), "module": obj.name,
                            "module_byte_exact_object_text": False,
                            "evidence": "call-target placed relocation-masked module .text equality"})
    union = []
    for a, b in sorted(exact_ranges):
        if union and a <= union[-1][1]:
            union[-1][1] = max(b, union[-1][1])
        else:
            union.append([a, b])
    result = {"executable_sha256": hashlib.sha256(raw).hexdigest(),
              "runtime": "3.3", "module_candidates": len(modules),
              "byte_exact_unrelocated_modules": sum(x["byte_exact_object_text"] for x in modules),
              "byte_exact_unrelocated_module_bytes": sum(b - a for a, b in union),
              "note": "Library object matches are not recovered C. Relocated modules remain provisional. Export spans may include internal routines and are not certified function sizes.",
              "modules": modules, "exports": exports}
    (OUT / "runtime_symbols.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"{len(exports)} exports; {result['byte_exact_unrelocated_modules']} unrelocated module text matches / {result['byte_exact_unrelocated_module_bytes']} unique bytes")


if __name__ == "__main__":
    main()
