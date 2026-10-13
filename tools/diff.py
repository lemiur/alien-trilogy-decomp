"""Show object and original MIPS instructions side by side for source tuning.

R marks an unresolved relocation instruction; this diagnostic never certifies
a match. Use verify.py or bytecheck.py for a complete linked byte comparison.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import sys

import capstone

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("lnk_symbols", Path(__file__).resolve().parent / "lnk_symbols.py")
lnk_symbols = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lnk_symbols)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--object", required=True, type=Path)
    ap.add_argument("--symbol-map", required=True, type=Path)
    ap.add_argument("--function", required=True)
    ap.add_argument("--reference", type=Path, default=ROOT / "games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07")
    args = ap.parse_args()
    mapping = json.loads(args.symbol_map.read_text())
    target = mapping["functions"][args.function]
    address = int(target["address"], 0)
    raw = args.reference.read_bytes()
    if raw[:8] != b"PS-X EXE":
        raise ValueError("reference must be a PS-X EXE")
    load, size = struct.unpack_from("<II", raw, 0x18)
    if address < load or address + target["size"] > load + size:
        raise ValueError("function range is outside the executable")
    obj = lnk_symbols.load(args.object)  # exported functions by XDEF, static ones by local symbol
    text = [(n, sn, off) for n, sn, off in list(obj.xdefs) + obj.locals if obj.sections.get(sn) == ".text"]
    _, sec, start = next(x for x in text if x[0] == args.function)
    code = bytes(obj.code[sec])
    end = min((off for _, sn, off in text if sn == sec and off > start), default=len(code))
    mine = code[start:end]
    theirs = raw[0x800 + address - load:0x800 + address - load + target["size"]]
    relocations = {off - start: (typ, expr) for typ, off, expr in obj.patches.get(sec, []) if start <= off < end}
    dis = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 | capstone.CS_MODE_LITTLE_ENDIAN)

    def instruction(data, offset):
        if offset + 4 > len(data):
            return "<absent>"
        word = data[offset:offset + 4]
        ins = next(dis.disasm(word, address + offset), None)
        asm = f"{ins.mnemonic} {ins.op_str}" if ins else "<undecoded>"
        return f"{struct.unpack('<I', word)[0]:08x}  {asm}"

    print(f"{args.function}: object {len(mine)} bytes; original {len(theirs)} bytes")
    print("R = unresolved relocation; ! = differing literal instruction")
    for offset in range(0, max(len(mine), len(theirs)), 4):
        mark = "R" if offset in relocations else (" " if mine[offset:offset + 4] == theirs[offset:offset + 4] else "!")
        print(f"{address + offset:08x} {mark} {instruction(mine, offset):58s} | {instruction(theirs, offset)}")
    for offset, (typ, expr) in relocations.items():
        print(f"  relocation +0x{offset:x}: type {typ}, expression {expr}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, ValueError, StopIteration) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
