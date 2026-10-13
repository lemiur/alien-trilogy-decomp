"""Second witness for compiler-generated data that tools/gate.py links from source.

The gate derives where a string pool, jump table or constant goes from the retail
operands of matched code, and counts those bytes as source. This module checks each
of the gate's claims without the gate's code: it reads verify.py's own objects, maps
and per-function results, and accepts a claim (unit, section, offset, size, address)
only when

- the object bytes of the claim, with their word relocations resolved through the
  verify.py-matched functions and the unit map, equal the reference bytes;
- at least one relocation in a verify.py-matched function, or one word relocation in a
  verify.py-verified data definition, of that unit refers into the claim; every such
  relocation's retail instruction field encodes the claimed address, and every such data
  word holds it in the reference (checked by a path of its own, ``_data_anchors``);
- the claim overlaps no other claim, no matched function and no verified data definition;
- the claim is one whole item without its alignment padding, by a minimal sizer of its
  own (``_object_extent``, below).

Item extents without labels. The object keeps no compiler-local labels ($LC0, $L5); what
it keeps are exported symbols, local symbols of statics, section ends, and relocations
whose expression is section+addend. An addend names a referenced byte, not an item:
``lw $3,$LC0+4($gp)`` (the second word of an inline ``strcpy`` of an 8-byte literal) and a
reference to a literal that starts at +4 encode the same section+4. So the rule is:

- item boundaries are labels the object keeps (exported and static symbols) and section ends; a
  boundary inside the claim rejects it ("runs past the next item boundary");
- the claim must start at a boundary or at a relocation target (an item nobody refers to at
  its start has no evidenced start);
- a relocation target strictly inside the claim is an interior reference into the claimed
  item: it never splits the claim, and its retail operand must still encode the claimed
  address plus its offset (checked with the other anchors);
- after the claim, the next boundary or relocation target outside the claim ends the
  item's room: the bytes up to it must be fewer than 8 and all zero, i.e. ``.align`` fill,
  which belongs to no item and comes from the fallback.

Blind spot: two adjacent compiler-local items, the second known only from relocation
targets, look exactly like one item with an interior reference, so a claim that merges
them is accepted here. What still holds for such a merge is that every byte equals the
reference and every relocation into it encodes its retail address, and that it overlaps
no other claim, matched function or verified data definition; so the gate's own claim of
the second item, or an exported label, still rejects it. The cost of the blind spot is a
misstated extent between unlabelled items, never a wrong byte or referent.

It knows nothing about assembly listings or the section layout, so a defect in the
gate's item sizing (tools/data_extent.py) or anchoring shows up as a disagreement,
not as a shared mistake.
"""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import struct

WORKSPACE = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location("psyqobj_witness", WORKSPACE / "shared-tools/psyqobj.py")
psyqobj = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(psyqobj)
# The object's local-symbol records (statics), which psyqobj does not keep.
_spec = importlib.util.spec_from_file_location("lnk_symbols_witness", Path(__file__).resolve().parent / "lnk_symbols.py")
lnk_symbols = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lnk_symbols)


def _int(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def _sext16(value):
    return value - 0x10000 if value & 0x8000 else value


def _split_half(kind, expr):
    """(82, E) / (84, E) for a split half of asm/lib/hilo.h (type 26 ``(E + 0x8000) >> 16``,
    type 30 ``E - (((E + 0x8000) >> 16) << 16)``, constants of E folded), else unchanged."""
    def terms(e):
        if e[0] == "c":
            return None, e[1]
        if e[0] in (2, 4):
            return e, 0
        if e[0] == 44:
            a, b = terms(e[1]), terms(e[2])
            if a is not None and b is not None and (a[0] is None or b[0] is None):
                return (b[0] if a[0] is None else a[0]), (a[1] + b[1]) & 0xffffffff
        return None

    def rounded(e):
        found = terms(e[2]) if e[0] == 60 and e[1] == ("c", 16) else None
        return None if not found or found[0] is None else (found[0], (found[1] - 0x8000) & 0xffffffff)

    target = rounded(expr) if kind == 26 else None
    if kind == 30 and expr[0] == 46 and expr[1][0] == 58 and expr[1][1] == ("c", 16):
        target = rounded(expr[1][2])
        target = target if target == terms(expr[2]) else None
    if target is None:
        return kind, expr
    return (82 if kind == 26 else 84), (target[0] if not target[1] else (44, ("c", target[1]), target[0]))


def _unit_context(unit, report):
    checks = [c for c in report.get("checks", []) if c["unit"] == unit]
    data = [d for d in report.get("data_checks", []) if d["unit"] == unit]
    if not checks and not data:
        return None
    first = (data or checks)[0]
    obj = lnk_symbols.load(first["object"])
    mapping = json.loads(Path(first["map_snapshot"] if "map_snapshot" in first else first["symbol_map"])
                         .read_text(encoding="utf-8"))
    matched = {c["function"]: (_int(c["address"]), int(c["size"])) for c in checks if c.get("status") == "match"}
    verified = [d for c in data if c.get("status") == "match" for d in c.get("definitions", [])]
    return obj, mapping, matched, verified


def _data_anchors(obj, verified, sid, offset, size, address, raw, load, header):
    """(anchors, problems): word relocations of verified data definitions into a claim.

    Each definition is found in the object by the section and object offset that
    data_check.py reported for it; each of its word relocations whose expression is
    section+constant inside the claim must find the claimed address in the reference word.
    """
    anchors, problems = 0, []
    for d in verified:
        if "object_offset" not in d:
            continue
        dsid = next((s for s, n in obj.sections.items() if n == d["section"]), None)
        start, daddr, dsize = int(d["object_offset"]), _int(d["address"]), int(d["size"])
        for kind, at, expr in obj.patches.get(dsid, []):
            if kind != 16 or not start <= at < start + dsize:
                continue
            if expr[0] == 4:
                leaf, const = expr, 0
            elif expr[0] == 44 and expr[1][0] == 4 and expr[2][0] == "c":
                leaf, const = expr[1], expr[2][1]
            elif expr[0] == 44 and expr[2][0] == 4 and expr[1][0] == "c":
                leaf, const = expr[2], expr[1][1]
            else:
                continue
            if leaf[1] != sid or not offset <= const < offset + size:
                continue
            word = struct.unpack_from("<I", raw, header + daddr + at - start - load)[0]
            if word != address + const - offset:
                problems.append(f"word at {daddr + at - start:#x} in {d['symbol']} holds {word:#x}, "
                                f"not {address + const - offset:#x}")
            anchors += 1
    return anchors, problems


def _object_extent(obj, sid, offset, size):
    """Problem with the claim's extent by object evidence alone, or None (rule: module docstring)."""
    code = bytes(obj.code.get(sid, b""))
    labels = {0, len(code)} | {o for _, s, o in list(obj.xdefs) + list(getattr(obj, "locals", ())) if s == sid}
    targets = set()
    for patches in obj.patches.values():
        for _, _, expr in patches:
            inner = expr[2] if expr[0] == 46 and expr[1][0] == 12 else expr
            if inner[0] == 4 and inner[1] == sid:
                targets.add(0)
            elif inner[0] == 44:
                for leaf, const in ((inner[1], inner[2]), (inner[2], inner[1])):
                    if leaf == (4, sid) and const[0] == "c":
                        targets.add(const[1])
    if offset not in labels | targets:
        return "claim does not start at an item boundary of the object"
    end = offset + size
    inside = sorted(b for b in labels if offset < b < end)
    if inside:
        return f"claim runs past the next item boundary at +{inside[0]:#x}"
    following = min((b for b in labels | targets if b >= end), default=end)
    fill = code[end:following]
    if len(fill) >= 8 or any(fill):
        return f"claim leaves {len(fill)} bytes before the next item boundary that are not alignment fill"
    return None


def witness(claims, report, raw, load, header=0x800):
    """Return (witnessed byte count, problems) for the gate's generated-data claims."""
    problems, witnessed, contexts = [], 0, {}
    occupied = []
    for unit in sorted({c["unit"] for c in claims}):
        context = _unit_context(unit, report)
        contexts[unit] = context
        if context:
            occupied += [(a, a + n, f"{unit}:{name}") for name, (a, n) in context[2].items()]
            occupied += [(_int(d["address"]), _int(d["address"]) + int(d["size"]), f"{unit}:{d['symbol']}")
                         for d in context[3]]
    # Labels are per unit ($LC0 recurs across objects): claims are compared by address only.
    spans = sorted(((_int(c["address"]), _int(c["address"]) + int(c["size"]), c) for c in claims),
                   key=lambda span: span[:2])
    for (a, b, c), (d, _, e) in zip(spans, spans[1:]):
        if d < b:
            problems.append(f"generated claims {c['unit']}:{c['label']} and {e['unit']}:{e['label']} overlap")
    for claim in claims:
        unit, name = claim["unit"], f"{claim['unit']}:{claim['label']}"
        context = contexts.get(unit)
        if context is None:
            problems.append(f"{name}: verify.py has no results for unit {unit}")
            continue
        obj, mapping, matched, verified = context
        address, size, offset = _int(claim["address"]), int(claim["size"]), _int(claim["offset"])
        sid = next((s for s, n in obj.sections.items() if n == claim["section"]), None)
        code = bytes(obj.code.get(sid, b"")) if sid is not None else b""
        if sid is None or offset + size > len(code):
            problems.append(f"{name}: claim lies outside the object's {claim['section']}")
            continue
        if any(lo < address + size and address < hi for lo, hi, _ in occupied):
            problems.append(f"{name}: claim overlaps matched or verified bytes")
            continue
        extent = _object_extent(obj, sid, offset, size)
        if extent:
            problems.append(f"{name}: {extent}")
            continue
        text = {s for s, n in obj.sections.items() if n == ".text"}
        # Function starts by declared linkage: exported by XDEF, static by local symbol.
        functions = mapping.get("functions", {})
        starts = {n: (s, o) for static, pool in ((False, obj.xdefs), (True, obj.locals)) for n, s, o in pool
                  if s in text and n in functions and lnk_symbols.is_static(functions[n]) == static}

        def text_address(section, value):
            for fname, (faddr, fsize) in matched.items():
                if fname in starts and starts[fname][0] == section and starts[fname][1] <= value < starts[fname][1] + fsize:
                    return faddr + value - starts[fname][1]
            return None

        def symbol(index):
            sym = obj.xrefs.get(index)
            if sym in mapping.get("symbols", {}):
                v = mapping["symbols"][sym]
                return _int(v["address"] if isinstance(v, dict) else v)
            if sym in matched and not lnk_symbols.is_static(mapping.get("functions", {}).get(sym, {})):
                return matched[sym][0]  # an external reference never names a static
            return None

        actual, ok = bytearray(code[offset:offset + size]), True
        for kind, at, expr in obj.patches.get(sid, []):
            if not offset <= at < offset + size:
                continue
            value = None
            if kind == 16 and expr[0] == 2:
                value = symbol(expr[1])
            elif kind == 16 and expr[0] == 44:
                leaf, const = (expr[1], expr[2]) if expr[1][0] == 4 else (expr[2], expr[1])
                if leaf[0] == 4 and const[0] == "c" and leaf[1] in text:
                    value = text_address(leaf[1], const[1])
            if value is None or at + 4 > offset + size:
                problems.append(f"{name}: data relocation at +{at - offset:#x} cannot be resolved")
                ok = False
                break
            struct.pack_into("<I", actual, at - offset, value & 0xffffffff)
        if not ok:
            continue
        reference = raw[header + address - load:header + address - load + size]
        if bytes(actual) != reference:
            problems.append(f"{name}: object bytes differ from the reference at {address:#x}")
            continue
        gp = mapping.get("gp")
        anchors = 0
        for fname, (faddr, fsize) in matched.items():
            if fname not in starts:
                continue
            fsec, fstart = starts[fname]
            for kind, at, expr in obj.patches.get(fsec, []):
                if not fstart <= at < fstart + fsize:
                    continue
                kind, expr = _split_half(kind, expr)
                inner = expr[2] if kind == 30 and expr[0] == 46 else expr
                if inner[0] == 4:
                    leaf, const = inner, ("c", 0)
                elif inner[0] == 44:
                    leaf, const = (inner[1], inner[2]) if inner[1][0] == 4 else (inner[2], inner[1])
                else:
                    continue
                if leaf != (4, sid) or const[0] != "c" or not offset <= const[1] < offset + size:
                    continue
                target = address + const[1] - offset
                field = struct.unpack_from("<I", raw, header + faddr + at - fstart - load)[0] & 0xffff
                expect = {82: ((target + 0x8000) >> 16) & 0xffff, 84: target & 0xffff,
                          30: (target - _int(gp)) & 0xffff if gp is not None else None}.get(kind)
                if expect is None or field != expect:
                    problems.append(f"{name}: relocation in {fname} does not encode {target:#x}")
                    ok = False
                anchors += 1
        data_anchors, issues = _data_anchors(obj, verified, sid, offset, size, address, raw, load, header)
        problems += [f"{name}: {issue}" for issue in issues]
        ok = ok and not issues
        if not anchors + data_anchors:
            problems.append(f"{name}: no matched function or verified data definition refers into the claim")
            continue
        if ok:
            witnessed += size
    return witnessed, problems
