"""Tests of tools/elf_lnk.py on frozen GNU as objects: python -m unittest tools/test_elf_lnk.py.

Fixtures (tools/testdata/gnu_as/, frozen ELF objects of the pinned GNU assembler):
- fog_00.elf: LIBGTE FOG_00 SetFogFar (92 bytes, the tge trap at +0x2c, jal SetDQA/SetDQB);
- patchgte.elf: LIBGTE PATCHGTE _patch_gte (160 bytes; split %hi/%lo pairs to .text+0x68 and
  the end label .text+0xa0; an absolute lui+sw / lui+lw pair to its 16-byte .sbss block).
"""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parent
FIXTURES = TOOLS / "testdata" / "gnu_as"


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


elf_lnk = load("elf_lnk")
lnk_symbols = load("lnk_symbols")
bytecheck = load("bytecheck")
REFERENCE = TOOLS.parents[1] / "games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07"


def converted(name, data=None):
    lnk = elf_lnk.convert(data if data is not None else (FIXTURES / name).read_bytes())
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "INPUT.OBJ"
        path.write_bytes(lnk)
        return lnk_symbols.load(path)


def relocations(data):
    """[(file offset of the r_info word, type)] of every REL entry of an ELF fixture."""
    elf = elf_lnk.Elf(data)
    return [(s["offset"] + 8 * i + 4, struct.unpack_from("<I", data, s["offset"] + 8 * i + 4)[0] & 0xff)
            for s in elf.sections if s["type"] == 9 for i in range(s["size"] // 8)]


class ConvertTest(unittest.TestCase):
    def test_fog_00_records(self):
        o = converted("fog_00.elf")
        self.assertEqual(o.sections, {1: ".rdata", 2: ".text", 3: ".data", 4: ".sdata", 5: ".sbss",
                                      6: ".bss", 7: ".ctors", 8: ".dtors"})
        self.assertEqual([(n, s, off) for n, s, off in o.xdefs], [("SetFogFar", 2, 0)])
        self.assertEqual(sorted(o.xrefs.values()), ["SetDQA", "SetDQB"])
        names = {n: i for i, n in o.xrefs.items()}
        self.assertEqual(o.patches[2], [(74, 0x3c, (2, names["SetDQA"])), (74, 0x44, (2, names["SetDQB"]))])
        code = bytes(o.code[2])
        self.assertEqual(len(code), 92)
        self.assertEqual(struct.unpack_from("<I", code, 0x2c)[0], 0x00001770)  # tge zero, zero, 0x5d
        self.assertEqual(struct.unpack_from("<I", code, 0x3c)[0], 0x0c000000)  # relocated field cleared
        self.assertEqual(o.locals, [])

    def test_patchgte_split_pairs_and_sbss_static(self):
        o = converted("patchgte.elf")
        text = lambda c: (44, ("c", c), (4, 2))
        self.assertEqual(o.patches[2][:2], [(82, 0x00, (4, 5)), (84, 0x04, (4, 5))])
        # lui k0; lui k1; lw; addiu k0; addiu k1: two HI16 halves before their LO16 halves.
        self.assertIn((82, 0x1c, text(0x68)), o.patches[2])
        self.assertIn((82, 0x20, text(0xa0)), o.patches[2])
        self.assertIn((84, 0x28, text(0x68)), o.patches[2])
        self.assertIn((84, 0x2c, text(0xa0)), o.patches[2])
        self.assertEqual(o.locals, [("g_8008d304", 5, 0)])  # code labels ($stub, $copy) get none
        self.assertEqual(len(o.code[2]), 160)
        spec = json.loads((FIXTURES / "patchgte.json").read_text())
        lnk_symbols.audit(o, spec)  # .text owned by _patch_gte, .sbss by the static

    @unittest.skipUnless(REFERENCE.is_file(), "needs the retail executable")
    def test_converted_objects_match_retail(self):
        reference = bytecheck.Reference(REFERENCE.read_bytes())
        for name, function in (("patchgte", "_patch_gte"), ("fog_00", "SetFogFar")):
            o = converted(f"{name}.elf")
            spec = json.loads((FIXTURES / f"{name}.json").read_text())
            report = bytecheck.check(o, spec, reference, function)
            self.assertEqual(report["status"], "match", name)

    def test_unsupported_relocation_is_refused(self):
        data = bytearray((FIXTURES / "patchgte.elf").read_bytes())
        at, _ = relocations(data)[0]
        data[at] = 7  # R_MIPS_GPREL16
        with self.assertRaisesRegex(elf_lnk.ElfError, "relocation type 7"):
            elf_lnk.convert(bytes(data))

    def test_orphan_halves_are_refused(self):
        data = bytes((FIXTURES / "patchgte.elf").read_bytes())
        rels = relocations(data)
        hi = next(at for at, kind in rels if kind == elf_lnk.R_MIPS_HI16)
        lo = [at for at, kind in rels if kind == elf_lnk.R_MIPS_LO16][-1]  # lw ra, %lo(...)
        broken = bytearray(data)
        broken[hi] = elf_lnk.R_MIPS_32  # its LO16 is left without a HI16
        with self.assertRaisesRegex(elf_lnk.ElfError, "LO16 without a preceding HI16"):
            elf_lnk.convert(bytes(broken))
        broken = bytearray(data)
        broken[lo] = elf_lnk.R_MIPS_32  # its HI16 is left without a LO16
        with self.assertRaisesRegex(elf_lnk.ElfError, "HI16 .* without a following LO16"):
            elf_lnk.convert(bytes(broken))

    def test_not_a_mips_relocatable_is_refused(self):
        data = bytearray((FIXTURES / "fog_00.elf").read_bytes())
        data[16] = 2  # ET_EXEC
        with self.assertRaisesRegex(elf_lnk.ElfError, "relocatable"):
            elf_lnk.convert(bytes(data))

    def test_expression_encoding_round_trips(self):
        expr = (44, ("c", 0x68), (4, 2))
        parsed, end = lnk_symbols.psyqobj.expr(elf_lnk.encode(expr), 0)
        self.assertEqual(parsed, expr)


if __name__ == "__main__":
    unittest.main()
