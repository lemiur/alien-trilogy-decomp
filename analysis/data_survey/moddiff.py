"""Word-level diff of one SDK library module section against the retail image.

usage: python moddiff.py <library path relative to ../sdk> <module> <address> [section]

Prints every word that differs at the aligned position (relocated words are
shown as masked), with both disassemblies, then a summary.  Evidence only.
"""
import struct
import sys
from pathlib import Path

import capstone

HERE = Path(__file__).resolve()
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / 'shared-tools'))
import psyqobj  # noqa: E402

LOAD = 0x80010000


def main():
    lib, mod, addr = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    sect = sys.argv[4] if len(sys.argv) > 4 else '.text'
    img = (ROOT / 'games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07').read_bytes()[0x800:]
    md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN)
    for m in psyqobj.load(str(ROOT / 'sdk' / lib)):
        if isinstance(m, Exception) or m.name != mod:
            continue
        for sn, nm in m.sections.items():
            if nm != sect or sn not in m.code:
                continue
            code = bytes(m.code[sn])
            patched = {}
            for t, off, e in m.patches.get(sn, []):
                patched[off & ~3] = (t, e)
            defs = {off: n for (n, s, off) in m.xdefs if s == sn}
            diff = 0
            for k in range(0, len(code) - 3, 4):
                a = addr + k
                rw = img[a - LOAD:a - LOAD + 4]
                lw = code[k:k + 4]
                if k in defs:
                    print('%08x <%s>' % (a, defs[k]))
                if k in patched or rw == lw:
                    continue
                diff += 1
                di = lambda b, at: ' '.join(next(((i.mnemonic, i.op_str) for i in md.disasm(b, at)), ('?', '')))
                print('  %08x +%04x retail %s %-34s lib %s %s' % (a, k, rw.hex(), di(rw, a), lw.hex(), di(lw, a)))
            print('%s %s %s: %d bytes, %d relocated words, %d differing words' % (lib, mod, nm, len(code), len(patched), diff))


if __name__ == '__main__':
    main()
