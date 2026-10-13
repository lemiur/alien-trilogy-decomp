"""Resolve Runtime 3.3 PsyQ library relocations against the original EXE.

This reports exact library module text evidence; it does not count exports as
recovered C functions. Unresolved or conflicting relocation contexts fail
closed and are recorded with their evidence.

A candidate whose whole text range is placed by registered library assembly units
(LIBRARY_ASM_UNITS in tools/verify.py: the module is a rebuild that matches no
archive, written as an annotated .s unit) gets status "library_asm" and names those
units, whatever its relocations resolved to: its bytes are library assembly, never
certified library and never a conflicting candidate. Its relocations stay in the
binding evidence (they are what the archive object would need), so they still make
a binding ambiguous for the other modules that import it. A unit that covers only
part of a candidate is an error.
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
PROJECT = Path(__file__).resolve().parents[1]
CONFIG = PROJECT / "config"
REPORTS = PROJECT / "reports"
SPEC = importlib.util.spec_from_file_location("psyqobj", ROOT / "shared-tools" / "psyqobj.py")
psyqobj = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(psyqobj)

OPS = (44, 46)


def as_int(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def s16(value):
    value &= 0xffff
    return value - 0x10000 if value & 0x8000 else value


def u32(value):
    return value & 0xffffffff


def pstr(data, offset):
    length = data[offset]
    return data[offset + 1:offset + 1 + length].decode("latin1"), offset + 1 + length


def library_modules(path):
    """Return raw LNK records from a PsyQ LIB while keeping the SDK files read-only."""
    data = Path(path).read_bytes()
    if data[:4] == b"LNK\x02":
        return [(Path(path).name, data)]
    if data[:4] != b"LIB\x01":
        raise ValueError(f"not a PsyQ LIB or standalone LNK: {path}")
    out = []
    i = 4
    while i < len(data):
        name = data[i:i + 8].decode("latin1").strip()
        _, header_size, total_size = struct.unpack_from("<III", data, i + 8)
        lnk = data[i + header_size:i + total_size]
        if lnk[:4] != b"LNK\x02":
            raise ValueError(f"invalid LNK member {name} in {path}")
        out.append((name, lnk))
        i += total_size
    return out


def symbol_ids(lnk):
    """Recover exported/imported symbol numbers omitted by the existing reader."""
    definitions = {}
    imports = {}
    i = 4
    while i < len(lnk):
        op = lnk[i]
        i += 1
        if op == 0:
            break
        if op == 2:
            size = struct.unpack_from("<H", lnk, i)[0]; i += 2 + size
        elif op == 6:
            i += 2
        elif op == 8:
            i += 4
        elif op == 10:
            typ, off = lnk[i], struct.unpack_from("<H", lnk, i + 1)[0]
            i += 3
            _, i = psyqobj.expr(lnk, i)
        elif op == 12:
            num, sec, off = struct.unpack_from("<HHI", lnk, i); i += 8
            name, i = pstr(lnk, i); definitions[num] = (name, sec, off)
        elif op == 14:
            num = struct.unpack_from("<H", lnk, i)[0]; i += 2
            name, i = pstr(lnk, i); imports[num] = name
        elif op == 16:
            i += 5; _, i = pstr(lnk, i)
        elif op == 18:
            i += 6; _, i = pstr(lnk, i)
        elif op == 20:
            i += 3; _, i = pstr(lnk, i)
        elif op in (22, 24, 26):
            i += 2
        elif op == 28:
            i += 2; _, i = pstr(lnk, i)
        elif op == 30:
            i += 6
        elif op == 32:
            i += 4
        elif op == 34:
            pass
        elif op == 36:
            i += 1
        elif op == 38:
            i += 2
        elif op == 40:
            i += 6; _, i = pstr(lnk, i)
        elif op == 42:
            i += 2
        elif op == 44:
            i += 3
        elif op == 46:
            i += 1
        elif op == 48:
            num, sid, size = struct.unpack_from("<HHI", lnk, i); i += 8
            name, i = pstr(lnk, i); definitions[num] = (name, sid, None, size)
        elif op == 50:
            i += 2
        elif op == 52:
            i += 3
        elif op == 54:
            i += 4
        elif op == 56:
            i += 6
        elif op == 58:
            i += 8
        elif op == 60:
            i += 2
        elif op == 74:
            i += 2 + 4 + 2 + 4 + 2 + 4 + 2 + 4 + 4; _, i = pstr(lnk, i)
        elif op == 76:
            i += 10
        elif op in (78, 80):
            i += 10
        elif op == 82:
            i += 2 + 4 + 2 + 2 + 4; _, i = pstr(lnk, i)
        elif op == 84:
            i += 2 + 4 + 2 + 2 + 4
            dims = struct.unpack_from("<H", lnk, i)[0]; i += 2 + 4 * dims
            _, i = pstr(lnk, i); _, i = pstr(lnk, i)
        else:
            raise ValueError(f"unsupported LNK opcode {op} at {i - 1:#x}")
        if i > len(lnk):
            raise ValueError(f"truncated LNK while scanning symbol ids at {i:#x}")
    return definitions, imports


def term(expr, obj, module_key):
    """Return one symbolic leaf plus addend, or None for compound contexts."""
    if expr[0] == 2 and expr[1] in obj.xrefs:
        return ("symbol", obj.xrefs[expr[1]]), 0
    if expr[0] == 2 and expr[1] in getattr(obj, "id_definitions", {}):
        return ("local_symbol", module_key, expr[1]), 0
    if expr[0] in (4, 12, 22) and expr[1] in obj.sections:
        return (({4: "section_base", 12: "section_start", 22: "section_end"}[expr[0]], module_key, expr[1]), 0)
    if expr[0] == "c":
        return None, expr[1]
    if expr[0] == 44:
        left = term(expr[1], obj, module_key)
        right = term(expr[2], obj, module_key)
        if left is None or right is None:
            return None
        a, aa = left
        b, bb = right
        if a is None:
            return b, aa + bb
        if b is None:
            return a, aa + bb
        return None, 0
    return None


def linear(expr, obj, bindings, module_key, module_address, gp):
    """Evaluate expression to known constant plus unresolved variables.

    PsyQ stores SUB's right child before its left child. `psyqobj.expr` keeps
    that serialized order, so semantic left-minus-right is second-minus-first.
    """
    if expr[0] == "c":
        return expr[1], {}
    if isinstance(expr[0], int) and expr[0] == 2:
        name = obj.xrefs.get(expr[1])
        if name is not None:
            key = ("symbol", name)
        else:
            definition = getattr(obj, "id_definitions", {}).get(expr[1])
            if definition is None:
                raise ValueError(f"unknown symbol index {expr[1]}")
            key = ("local_symbol", module_key, expr[1])
            name, sid, off = definition[:3]
            secname = obj.sections.get(sid)
            if off is not None and secname == ".text":
                return module_address + off, {}
            section_key = ("section_base", module_key, sid)
            if off is not None and section_key in bindings:
                return u32(bindings[section_key] + off), {}
        if key in bindings:
            return bindings[key], {}
        return 0, {key: 1}
    if isinstance(expr[0], int) and expr[0] in (4, 12, 22):
        sid = expr[1]
        if sid not in obj.sections:
            raise ValueError(f"unknown section index {sid}")
        tag = {4: "section_base", 12: "section_start", 22: "section_end"}[expr[0]]
        name = obj.sections[sid]
        key = (tag, module_key, sid)
        if tag == "section_base" and name == ".text":
            return module_address, {}
        if tag == "section_start" and name == ".sdata" and gp is not None:
            return gp, {}
        if key in bindings:
            return bindings[key], {}
        return 0, {key: 1}
    op, first, second = expr
    if op not in OPS:
        raise ValueError(f"unsupported expression operator {op:#x}")
    a, av = linear(first, obj, bindings, module_key, module_address, gp)
    b, bv = linear(second, obj, bindings, module_key, module_address, gp)
    variables = defaultdict(int)
    if op == 44:
        constant = a + b
        for key, coefficient in av.items():
            variables[key] += coefficient
        for key, coefficient in bv.items():
            variables[key] += coefficient
    else:
        # Serialized order is right operand then left operand.
        constant = b - a
        for key, coefficient in bv.items():
            variables[key] += coefficient
        for key, coefficient in av.items():
            variables[key] -= coefficient
    return u32(constant), {k: v for k, v in variables.items() if v}


def encoded_word(data, offset):
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"relocation word at {offset:#x} is outside text")
    return struct.unpack_from("<I", data, offset)[0]


def j26_address(word, place):
    if word >> 26 not in (2, 3):
        raise ValueError(f"MIPS26 relocation at {place:#x} is not J/JAL ({word:08x})")
    target = ((place + 4) & 0xf0000000) | ((word & 0x03ffffff) << 2)
    if target & 3:
        raise ValueError(f"unaligned MIPS26 target {target:#x}")
    return target


def leaf_expression(expr, obj, module_key):
    """Canonical leaf/addend pair for direct symbol or section relocations."""
    found = term(expr, obj, module_key)
    if found is None:
        return None
    key, addend = found
    if key is None:
        return None
    return key, addend


def add_observation(observations, key, address, evidence):
    observations[key].append({"address": u32(address), "evidence": evidence})


def expression_keys(expr, obj, module_key):
    keys = set()
    if expr[0] == 2:
        if expr[1] in obj.xrefs:
            keys.add(("symbol", obj.xrefs[expr[1]]))
        elif expr[1] in getattr(obj, "id_definitions", {}):
            keys.add(("local_symbol", module_key, expr[1]))
    elif expr[0] in (4, 12, 22):
        tag = {4: "section_base", 12: "section_start", 22: "section_end"}[expr[0]]
        keys.add((tag, module_key, expr[1]))
    elif expr[0] in OPS:
        keys.update(expression_keys(expr[1], obj, module_key))
        keys.update(expression_keys(expr[2], obj, module_key))
    return keys


def range_union_size(items):
    merged = []
    for start, size in sorted(items):
        end = start + size
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return sum(end - start for start, end in merged)


def library_asm_ranges():
    """[(unit, function, start, end)] of the registered library assembly routines."""
    spec = importlib.util.spec_from_file_location("runtime_registry", PROJECT / "tools" / "verify.py")
    registry = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(registry)
    return registry.library_asm_text_ranges()


def mark_library_asm(modules, placed):
    """Give status "library_asm" to every candidate wholly placed by library assembly routines
    ``placed`` [(unit, function, start, end)]; raise on a candidate placed only in part."""
    for module in modules:
        if "address" not in module or "text_size" not in module:
            continue
        start = as_int(module["address"])
        end = start + int(module["text_size"])
        hits = sorted((a, b, unit) for unit, _, a, b in placed if a < end and start < b)
        if not hits:
            continue
        cursor = start
        for a, b, _ in hits:
            if a > cursor:
                break
            cursor = max(cursor, b)
        if hits[0][0] != start or cursor < end or any(a < start or b > end for a, b, _ in hits):
            raise ValueError(f"library assembly {sorted({h[2] for h in hits})} covers only part of "
                             f"runtime candidate {module['module']} at {start:#x}..{end - 1:#x}")
        module["archive_status"] = module["status"]
        module["status"] = "library_asm"
        module["library_asm_units"] = sorted({h[2] for h in hits})


def binding_conflict_diagnostics(bindings, modules):
    """Make cross-module contradictions easy to inspect without resolving them."""
    module_by_name = {m.get("module"): m for m in modules}
    diagnostics = []
    for binding in bindings:
        if binding["status"] != "ambiguous":
            continue
        grouped = defaultdict(lambda: {"addresses": set(), "observations": 0})
        for observation in binding["evidence"]:
            evidence = observation.get("evidence", {})
            name = evidence.get("module", "<unknown>")
            grouped[name]["addresses"].add(f"0x{observation['address']:08x}")
            grouped[name]["observations"] += 1
        participants = []
        for name, details in sorted(grouped.items()):
            module = module_by_name.get(name, {})
            participants.append({
                "module": name,
                "address": module.get("address"),
                "status": module.get("status"),
                **({"library_asm_units": module["library_asm_units"],
                    "note": "rebuilt module written as library assembly: its unit's map and data "
                            "definitions give the referent of this relocation"}
                   if module.get("status") == "library_asm" else {}),
                "imported_symbols": module.get("imported_symbols", []),
                "candidate_addresses": sorted(details["addresses"]),
                "observation_count": details["observations"],
            })
        diagnostics.append({
            "kind": "conflicting_cross_module_binding",
            "binding_kind": binding["kind"],
            "name_or_section": binding["name_or_section"],
            "status": "unresolved",
            "reason": "independently observed module relocations imply different addresses; no shared binding is selected",
            "participants": participants,
        })
    return diagnostics


def mips_hilo_pairs(module, obj, section, ref_bytes, module_address):
    """Pair nearby HI16/LO16 instructions by relocation expression and register."""
    patches = obj.patches.get(section, [])
    highs = [(off, expr, encoded_word(ref_bytes, off)) for typ, off, expr in patches if typ == 82]
    lows = [(off, expr, encoded_word(ref_bytes, off)) for typ, off, expr in patches if typ == 84]
    pairs = []
    for lo_off, lo_expr, lo_word in lows:
        lo_term = leaf_expression(lo_expr, obj, module)
        if lo_term is None:
            continue
        lo_base = (lo_word >> 21) & 31
        candidates = []
        for hi_off, hi_expr, hi_word in highs:
            if hi_off >= lo_off or leaf_expression(hi_expr, obj, module) != lo_term:
                continue
            # HI16 should patch a LUI; the LO instruction must consume its rt.
            if (hi_word >> 26) != 0x0f:
                continue
            hi_reg = (hi_word >> 16) & 31
            if lo_base != hi_reg:
                continue
            lo_opcode = (lo_word >> 26) & 0x3f
            lo_imm = lo_word & 0xffff
            if lo_opcode == 0x0d:  # ORI uses an unsigned low half.
                effective = ((hi_word & 0xffff) << 16) | lo_imm
            else:
                effective = (((hi_word & 0xffff) << 16) + s16(lo_imm)) & 0xffffffff
            candidates.append((lo_off - hi_off, hi_off, effective))
        if candidates:
            # The latest matching LUI in the same register is the paired high
            # relocation. Equal-distance alternatives remain ambiguous.
            distance = min(x[0] for x in candidates)
            nearest = [x for x in candidates if x[0] == distance]
            if len(nearest) == 1:
                _, hi_off, effective = nearest[0]
                pairs.append({"hi_offset": hi_off, "lo_offset": lo_off,
                              "expression": lo_expr, "target": effective,
                              "address": f"0x{module_address + hi_off:08x}"})
    return pairs


def load_context(runtime, inventory):
    raw = (ROOT / inventory["inputs"]["executable"]).read_bytes()
    if hashlib.sha256(raw).hexdigest() != runtime["executable_sha256"]:
        raise ValueError("runtime symbol inventory belongs to a different executable")
    load_address, text_size = struct.unpack_from("<II", raw, 0x18)
    payload = raw[0x800:0x800 + text_size]
    if len(payload) != text_size:
        raise ValueError("truncated PS-X EXE text payload")
    exports = defaultdict(set)
    for record in runtime["exports"]:
        exports[record["name"]].add(as_int(record["address"]))
    known = {}
    provenance = {}
    for name, addresses in exports.items():
        if len(addresses) == 1:
            known[("symbol", name)] = next(iter(addresses))
            provenance[("symbol", name)] = "Runtime 3.3 matched module export"
    for path in (CONFIG / "frontend.json", CONFIG / "sprites.json",
                 CONFIG / "main.json"):
        if not path.exists():
            continue
        obj = json.loads(path.read_text(encoding="utf-8"))
        for category in ("symbols", "functions"):
            for name, value in obj.get(category, {}).items():
                address = as_int(value["address"]) if isinstance(value, dict) else as_int(value)
                key = ("symbol", name)
                if key in known and known[key] != address:
                    known.pop(key, None)
                    provenance[key] = "ambiguous recovered maps"
                elif provenance.get(key) != "ambiguous recovered maps":
                    known[key] = address
                    provenance[key] = str(path.relative_to(ROOT))
    return raw, load_address, payload, known, provenance


def resolve(runtime, inventory, runtime_root):
    raw, load_address, payload, known, provenance = load_context(runtime, inventory)
    gp = as_int(inventory["inputs"]["startup_gp_observed"])
    libraries = {}
    raw_libraries = {}
    library_hashes = {}
    records = []
    global_observations = defaultdict(list)
    modules = []
    for cand in runtime["modules"]:
        libpath = ROOT / cand["library"]
        if libpath not in libraries:
            library_hashes[str(libpath.relative_to(ROOT))] = hashlib.sha256(libpath.read_bytes()).hexdigest()
            libraries[libpath] = psyqobj.load(str(libpath))
            if libpath.read_bytes()[:4] == b"LNK\x02":
                for obj in libraries[libpath]:
                    if not isinstance(obj, Exception): obj.name = libpath.name
            raw_libraries[libpath] = library_modules(libpath)
        obj = next((x for x in libraries[libpath]
                    if not isinstance(x, Exception) and x.name == cand["module"]), None)
        if obj is None:
            modules.append({"module": cand["module"], "library": cand["library"], "status": "parse_error"})
            continue
        text_section = next((sid for sid, name in obj.sections.items() if name == ".text"), None)
        if text_section is None:
            modules.append({"module": cand["module"], "library": cand["library"], "status": "no_text"})
            continue
        raw_module = next((lnk for name, lnk in raw_libraries[libpath] if name == cand["module"]), None)
        if raw_module is None:
            modules.append({"module": cand["module"], "library": cand["library"], "status": "raw_module_missing"})
            continue
        definitions, imports = symbol_ids(raw_module)
        obj.id_definitions = definitions
        obj.xrefs.update(imports)
        code = bytes(obj.code.get(text_section, b""))
        address = as_int(cand["address"])
        start = address - load_address
        if start < 0 or start + len(code) > len(payload):
            modules.append({"module": cand["module"], "library": cand["library"], "status": "outside_exe"})
            continue
        reference = payload[start:start + len(code)]
        module_key = f"{cand['library']}::{cand['module']}@{address:08x}"
        patches = obj.patches.get(text_section, [])
        for _, off, _ in patches:
            if off < 0 or off + 4 > len(code):
                raise ValueError(f"{obj.name}: relocation outside .text at {off:#x}")
        nonreloc_ok = all(code[i] == reference[i] for i in range(len(code))
                          if all(not (off <= i < off + 4) for _, off, _ in patches))
        pair_data = mips_hilo_pairs(module_key, obj, text_section, reference, address)
        # Record independently observed addresses for symbols and local section
        # placement. These observations are later cross-checked across modules.
        for typ, off, expr in patches:
            place = address + off
            if typ == 74:
                inferred_target = j26_address(encoded_word(reference, off), place)
                t = leaf_expression(expr, obj, module_key)
                if t:
                    key, addend = t
                    add_observation(global_observations, key, inferred_target - addend,
                                    {"module": obj.name, "type": typ, "offset": off,
                                     "operand_target": f"0x{inferred_target:08x}", "addend": addend})
        for pair in pair_data:
            t = leaf_expression(pair["expression"], obj, module_key)
            if t:
                key, addend = t
                add_observation(global_observations, key, pair["target"] - addend,
                                {"module": obj.name, "type": "HI16/LO16",
                                 "offsets": [pair["hi_offset"], pair["lo_offset"]],
                                 "addend": addend,
                                 "operand_target": f"0x{pair['target']:08x}"})
        # GP-relative relocations directly expose their signed displacement.
        # Solve only a single unresolved leaf with coefficient +/-1; anything
        # broader is left unresolved rather than guessed.
        local_bindings = {}
        for typ, off, expr in patches:
            if typ != 30:
                continue
            place = address + off
            val, vars_ = linear(expr, obj, {**known, **local_bindings}, module_key, address, gp)
            word = encoded_word(reference, off)
            delta = s16(word)
            if len(vars_) == 1:
                (key, coeff), = vars_.items()
                if coeff in (1, -1):
                    solved = u32((u32(delta) - val) * coeff)
                    local_bindings[key] = solved
                    add_observation(global_observations, key, solved,
                                    {"module": obj.name, "type": typ, "offset": off,
                                     "gp_displacement": delta,
                                     "evidence_address": f"0x{place:08x}"})
        # Local/global HILO targets can resolve any one unknown expression leaf.
        for pair in pair_data:
            val, vars_ = linear(pair["expression"], obj, {**known, **local_bindings}, module_key, address, gp)
            if len(vars_) == 1:
                (key, coeff), = vars_.items()
                if coeff in (1, -1):
                    solved = u32((pair["target"] - val) * coeff)
                    local_bindings[key] = solved
                    add_observation(global_observations, key, solved,
                                    {"module": obj.name, "type": "HI16/LO16",
                                     "offsets": [pair["hi_offset"], pair["lo_offset"]],
                                     "evidence_address": pair["address"]})
        records.append({"candidate": cand, "obj": obj, "code": code,
                        "reference": reference, "address": address,
                        "module_key": module_key, "section": text_section,
                        "patches": patches, "pairs": pair_data,
                        "imports": imports,
                        "local_bindings": local_bindings,
                        "nonreloc_ok": nonreloc_ok})

    # Merge independently observed symbol addresses. Any conflicting values
    # make the binding ambiguous, even if one happens to match a known export.
    inferred = {}
    ambiguous_keys = set()
    binding_reports = []
    for key, evidence in global_observations.items():
        values = {e["address"] for e in evidence}
        known_value = known.get(key)
        all_values = set(values)
        if known_value is not None:
            all_values.add(known_value)
        if len(all_values) == 1:
            inferred[key] = next(iter(all_values))
        else:
            ambiguous_keys.add(key)
        binding_reports.append({"kind": key[0], "name_or_section": key[1:],
                                "address": f"0x{next(iter(all_values)):08x}" if len(all_values) == 1 else None,
                                "status": "resolved" if len(all_values) == 1 else "ambiguous",
                                "evidence_count": len(evidence), "evidence": evidence[:32],
                                "known_source": provenance.get(key)})
    for key, value in known.items():
        if key not in ambiguous_keys:
            inferred.setdefault(key, value)

    for rec in records:
        obj, cand = rec["obj"], rec["candidate"]
        address, code, reference = rec["address"], rec["code"], rec["reference"]
        section, module_key = rec["section"], rec["module_key"]
        local = dict(rec["local_bindings"])
        # Merge observations for this module's section anchors, preserving any
        # contradictory solutions as an explicit error.
        for key, evid in global_observations.items():
            if len(key) >= 3 and key[1] == module_key:
                vals = {x["address"] for x in evid}
                if len(vals) == 1:
                    local[key] = next(iter(vals))
                elif len(vals) > 1:
                    local.pop(key, None)
        # A context-local solution cannot override a symbol address that was
        # independently observed at conflicting runtime call sites.
        for key in ambiguous_keys:
            local.pop(key, None)
        actual = bytearray(code)
        applied = []
        errors = []
        for typ, off, expr in rec["patches"]:
            try:
                conflicting = sorted(expression_keys(expr, obj, module_key) & ambiguous_keys, key=repr)
                if conflicting:
                    raise ValueError("ambiguous cross-module relocation binding: " + repr(conflicting))
                val, vars_ = linear(expr, obj, {**inferred, **local}, module_key, address, gp)
                if vars_:
                    raise ValueError("unresolved relocation context: " + repr(vars_))
                val = u32(val)
                word = encoded_word(actual, off)
                place = address + off
                resolved = None
                if typ == 82:
                    resolved = ((val + 0x8000) >> 16) & 0xffff
                    word = (word & 0xffff0000) | resolved
                elif typ == 84:
                    resolved = val & 0xffff
                    word = (word & 0xffff0000) | resolved
                elif typ == 74:
                    if val & 3:
                        raise ValueError(f"unaligned MIPS26 target {val:#x}")
                    if ((val ^ (place + 4)) & 0xf0000000):
                        raise ValueError(f"MIPS26 target {val:#x} out of jump region at {place + 4:#x}")
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
                applied.append({"type": typ, "offset": off, "value": resolved})
            except (ValueError, KeyError, struct.error) as exc:
                errors.append({"type": typ, "offset": off, "error": str(exc)})
        diffs = [i for i, (a, b) in enumerate(zip(actual, reference)) if a != b]
        if not rec["nonreloc_ok"]:
            status = "nonrelocated_bytes_mismatch"
        elif errors:
            status = "unresolved"
        elif diffs:
            status = "relocated_bytes_mismatch"
        else:
            status = "full_match"
        rec["result"] = {
            "module": cand["module"], "library": cand["library"],
            "address": cand["address"], "text_size": len(code),
            "imported_symbols": sorted(set(rec["imports"].values())),
            "relocation_count": len(rec["patches"]), "status": status,
            "unrelocated_bytes_match": rec["nonreloc_ok"],
            "applied_relocations": applied, "unresolved_relocations": errors,
            "mismatch_count": len(diffs), "mismatch_offsets": diffs[:64],
            "reconstructed_sha256": hashlib.sha256(actual).hexdigest(),
            "reference_sha256": hashlib.sha256(reference).hexdigest(),
            "hilo_pair_count": len(rec["pairs"]),
            "hilo_pairs": rec["pairs"][:64]
        }
        modules.append(rec["result"])
    mark_library_asm(modules, library_asm_ranges())
    return raw, modules, binding_reports, global_observations, library_hashes


def main():
    runtime = json.loads((ANALYSIS / "runtime_symbols.json").read_text(encoding="utf-8"))
    inventory = json.loads((ANALYSIS / "inventory.json").read_text(encoding="utf-8"))
    raw, modules, bindings, _, library_hashes = resolve(runtime, inventory, inventory["inputs"]["runtime_root"])
    matched = [x for x in modules if x["status"] == "full_match"]
    counts = defaultdict(int)
    sizes = defaultdict(int)
    all_ranges = []
    relocated_ranges = []
    unrelocated_ranges = []
    for record in modules:
        address = as_int(record["address"])
        size = int(record["text_size"])
        if record["status"] == "full_match":
            all_ranges.append((address, size))
            if record["relocation_count"]:
                counts["relocated_exact_modules"] += 1
                relocated_ranges.append((address, size))
            else:
                counts["unrelocated_exact_modules"] += 1
                unrelocated_ranges.append((address, size))
    counts["exact_module_matches"] = sum(x["status"] == "full_match" for x in modules)
    sizes["unrelocated_exact_module_text_bytes_unique"] = range_union_size(unrelocated_ranges)
    sizes["relocated_exact_module_text_bytes_unique"] = range_union_size(relocated_ranges)
    sizes["all_exact_module_text_bytes_unique"] = range_union_size(all_ranges)
    report = {
        "executable_sha256": hashlib.sha256(raw).hexdigest(),
        "runtime": runtime["runtime"],
        "source_runtime_symbol_report": "alien_decomp/analysis/runtime_symbols.json",
        "runtime_library_sha256": library_hashes,
        "binding_evidence_note": "Export addresses are independently recorded by the symbol inventory. Additional addresses inferred from J26 or HI16/LO16 instruction operands are evidence from the same executable being compared, not independent golden data; cross-module conflicts are rejected.",
        "module_candidate_count": len(modules),
        "counts": dict(counts), "text_bytes": dict(sizes),
        "c_function_count": 0,
        "classification_note": "These are exact linked Runtime library object module text ranges, not recovered C function counts. No relocation instruction bytes are masked during full-match status.",
        "module_results": modules,
        "symbol_bindings": bindings,
        "binding_conflict_diagnostics": binding_conflict_diagnostics(bindings, modules)
    }
    (ANALYSIS / "runtime_linked.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (CONFIG / "runtime_bindings.json").write_text(json.dumps({
        "executable_sha256": report["executable_sha256"],
        "runtime": runtime["runtime"],
        "bindings": [x for x in bindings if x["status"] == "resolved" and x["kind"] == "symbol"],
        "section_placements": [x for x in bindings if x["status"] == "resolved" and x["kind"] != "symbol"],
        "unresolved_bindings": [x for x in bindings if x["status"] != "resolved"]
    }, indent=2) + "\n", encoding="utf-8")
    REPORTS.mkdir(parents=True, exist_ok=True)
    latest = {k: v for k, v in report.items() if k != "module_results"}
    latest["module_status_counts"] = dict(__import__("collections").Counter(x["status"] for x in modules))
    (REPORTS / "runtime_latest.json").write_text(json.dumps(latest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"modules": len(modules), "counts": dict(counts),
                      "status_counts": latest["module_status_counts"],
                      "relocated_exact_module_text_bytes_unique": sizes["relocated_exact_module_text_bytes_unique"]}, indent=2))


if __name__ == "__main__":
    main()
