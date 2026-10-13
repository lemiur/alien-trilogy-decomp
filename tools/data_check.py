"""Verify complete mapped initialized C definitions in a PsyQ object.

Word relocations in a definition are resolved and compared with the reference;
any other relocation is rejected. Matching function instructions alone does not
verify initial data.

Extents come from the unit's assembly listing (``--asm``, by default the
``INPUT.S`` beside the object) through tools/data_extent.py, the sizer the gate
uses: an item is the bytes its directives emit, without the ``.align`` padding
after it, and a definition must be exactly one item. A definition is keyed by its
exported name, by the name of a static the unit map declares in ``static_data``
(its local symbol, scoped to the unit), or, for a compiler-labelled item such as
a jump table, by its position (``.rdata+0x38``).

A section-relative target (``.rdata+0x24``, as a reference to a string literal
or a static array compiles) resolves through the placement of the item that
contains it; a target inside an item (``$LC0+4``) is an interior reference into
that item, never a new one. Placement follows tools/bytecheck.py's
``ItemPlacements``, so verify.py applies one rule to code and data: a data
definition of this unit places its item; otherwise an unnamed compiler-generated
item is placed when every relocation into it from this unit's mapped functions and
data definitions implies one address inside the retail section of its name and its
object bytes equal the reference there; only an offset that no item placement
covers falls back to the symbol map's ``sections`` base. A definition that those
relocations contradict, anchors that imply several addresses, or an evidenced item
whose bytes are not at its implied address is an error, never a choice.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("bytecheck", Path(__file__).resolve().parent / "bytecheck.py")
bytecheck = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bytecheck)
psyqobj, data_extent = bytecheck.psyqobj, bytecheck.data_extent


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--object", required=True, type=Path)
    ap.add_argument("--unit", required=True)
    ap.add_argument("--symbol-map", type=Path)
    ap.add_argument("--map", type=Path, default=ROOT / "alien_decomp/config/data.json")
    ap.add_argument("--reference", required=True, type=Path)
    ap.add_argument("--json-report", required=True, type=Path)
    ap.add_argument("--asm", type=Path, help="assembly listing of the object (default: INPUT.S beside it)")
    ap.add_argument("--layout", type=Path, default=bytecheck.LAYOUT,
                    help="retail section layout (config/section_layout.json)")
    args = ap.parse_args()
    report = {"unit": args.unit, "status": "error", "definitions": []}
    try:
        definitions = json.loads(args.map.read_text()).get(args.unit, {})
        raw = args.reference.read_bytes()
        reference = bytecheck.Reference(raw)
        load, image_size = reference.load, reference.size
        obj = bytecheck.load_object(args.object)  # with its local (static) symbols
        symbol_map = json.loads(args.symbol_map.read_text()) if args.symbol_map else {}
        asm_path = args.asm or args.object.with_name("INPUT.S")
        listing = None
        if definitions or asm_path.is_file():  # sizes definitions; finds static labels
            listing = asm_path.read_text(encoding="latin1")
        layout = json.loads(args.layout.read_text(encoding="utf-8"))
        placements = bytecheck.ItemPlacements(obj, symbol_map, reference, definitions, listing, layout)
        # Initialised statics (unexported data labels of the listing) are static_data entries,
        # each placed by the data definition of its name (tools/lnk_symbols.py).
        problems = bytecheck.lnk_symbols.initialised_statics(symbol_map, definitions, placements.static_labels())
        if problems:
            raise ValueError("; ".join(problems))
        resolver = bytecheck.Resolver(obj, symbol_map, placements)
        if definitions:
            placements.items()  # the listing must size the object's data sections

        for name, address, size, section, start in [(n, a, s, sid, off) for n, sid, off, a, s in placements.defs()]:
            if size <= 0 or address < load or address + size > load + image_size:
                raise ValueError(f"invalid initialized definition range: {name}")
            # Relocations from this unit's code and data into the item must not contradict it.
            placements.place(section, data_extent.item_at(placements.items(), obj.sections[section], start))
            code = bytes(obj.code.get(section, b""))
            end = start + size  # the item's emitted bytes; .align padding after it belongs to no item
            actual = bytearray(code[start:end])
            applied = []
            for typ, off, expr in obj.patches.get(section, []):
                if not start <= off < end:
                    continue
                if typ != 16 or off + 4 > end:
                    raise ValueError(f"{name}: unsupported data patch type {typ} at {off:#x}")
                value = resolver.resolve(expr)
                struct.pack_into("<I", actual, off - start, value)
                applied.append({"type": typ, "offset": off - start, "address": hex(value)})
            expected = raw[0x800 + address - load:0x800 + address - load + size]
            if len(expected) != size:
                raise ValueError("reference is truncated")
            differences = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
            report["definitions"].append({"symbol": name, "address": hex(address), "size": size,
                                           "section": obj.sections[section], "object_offset": start, "status": "mismatch" if differences else "match",
                                           "mismatch_offsets": differences, "object_bytes": actual.hex(),
                                           "reference_bytes": expected.hex(),
                                           "applied_relocations": applied,
                                           "object_sha256": hashlib.sha256(actual).hexdigest(),
                                           "reference_sha256": hashlib.sha256(expected).hexdigest()})
        report["status"] = "mismatch" if any(x["status"] != "match" for x in report["definitions"]) else "match"
    except (bytecheck.CheckError, OSError, ValueError, KeyError, AssertionError, IndexError, struct.error) as exc:
        report["error"] = str(exc)
    args.json_report.parent.mkdir(parents=True, exist_ok=True)
    args.json_report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "match" else 1


if __name__ == "__main__":
    raise SystemExit(main())
