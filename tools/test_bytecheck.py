"""Tests for bytecheck.py relocation-expression resolution."""
from __future__ import annotations

import importlib.util
from pathlib import Path
from types import SimpleNamespace
import struct
import unittest

spec = importlib.util.spec_from_file_location("bytecheck", Path(__file__).with_name("bytecheck.py"))
bytecheck = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bytecheck)

TEXT, DATA, SDATA = 1, 2, 3


def resolver():
    # Two functions in one object .text, placed discontiguously in the image.
    obj = SimpleNamespace(
        sections={TEXT: ".text", DATA: ".data", SDATA: ".sdata"},
        xdefs=[("f_a", TEXT, 0x00), ("f_b", TEXT, 0x20), ("g_local", DATA, 0x8)],
        xrefs={7: "f_ext", 8: "g_ext"},
    )
    symbol_map = {
        "gp": "0x8008cd74",
        "sections": {".data": "0x80070000", ".sdata": "0x8008cd74"},
        "functions": {"f_a": {"address": "0x80020000", "size": 0x20},
                      "f_b": {"address": "0x80031000", "size": 0x18}},
        "symbols": {"f_ext": "0x80055550", "g_ext": "0x8008cd80"},
    }
    return bytecheck.Resolver(obj, symbol_map)


def text_plus(offset):
    # PsyQ serializes the right operand first: [.text] + offset.
    return (44, ("c", offset), (4, TEXT))


class TextRelocationTests(unittest.TestCase):
    def test_text_hi_lo_resolves_through_containing_function(self):
        r = resolver()
        self.assertEqual(r.resolve((4, TEXT)), 0x80020000)
        self.assertEqual(r.resolve(text_plus(0x10)), 0x80020010)
        # The second function is not contiguous with the first.
        self.assertEqual(r.resolve(text_plus(0x20)), 0x80031000)
        self.assertEqual(r.resolve(text_plus(0x2c)), 0x8003100c)

    def test_operand_order_does_not_matter_for_addition(self):
        r = resolver()
        self.assertEqual(r.resolve((44, (4, TEXT), ("c", 0x24))), 0x80031004)

    def test_offset_outside_mapped_function_fails(self):
        r = resolver()
        with self.assertRaisesRegex(bytecheck.CheckError, "not inside a mapped function"):
            r.resolve(text_plus(0x38))
        with self.assertRaisesRegex(bytecheck.CheckError, "not inside a mapped function"):
            r.resolve(text_plus(0x100))

    def test_text_function_without_map_entry_fails(self):
        r = resolver()
        del r.fs["f_b"]
        with self.assertRaisesRegex(bytecheck.CheckError, "not inside a mapped function"):
            r.resolve(text_plus(0x20))

    def test_text_difference_is_an_offset(self):
        r = resolver()
        self.assertEqual(r.resolve((46, (4, TEXT), text_plus(0x2c))), 0x2c)

    def test_other_expressions_unchanged(self):
        r = resolver()
        self.assertEqual(r.resolve((2, 7)), 0x80055550)
        self.assertEqual(r.resolve((44, ("c", 4), (4, DATA))), 0x80070004)
        # GP-relative: g_ext - start(.sdata); right operand first.
        self.assertEqual(r.resolve((46, (12, SDATA), (2, 8))), 0xc)

    def test_sum_of_two_text_values_fails(self):
        r = resolver()
        with self.assertRaises(bytecheck.CheckError):
            r.resolve((44, (4, TEXT), (4, TEXT)))


# A synthetic unit whose .rdata has a hole: f_a's jump table, an unrelated table that
# retail keeps elsewhere (another function's), then f_b's jump table. In retail the two
# jump tables are 0x100 bytes apart, so no single .rdata base places both.
LOAD, IMAGE = 0x80010000, 0x30000
GP = 0x80038000
RDATA, SDATA = 2, 3
LISTING = """\t.rdata
\t.align\t2
$L10:
\t.word\t$L2
\t.word\t$L3
$L20:
\t.word\t1
\t.word\t2
\t.word\t3
\t.word\t4
$L30:
\t.word\t$L5
\t.word\t$L6
\t.sdata
\t.align\t2
$LC0:
\t.word\t7
$LC1:
\t.word\t9
"""
LAYOUT = {"regions": [{"section": ".rdata", "start": {"min": "0x80010000", "max": "0x80010000"}, "end": "0x80013000"},
                      {"section": ".text", "start": "0x80013000", "end": "0x80038000"},
                      {"section": ".sdata", "start": "0x80038000", "end": "0x80039000"}]}
JT1, JT2, LC1 = 0x80011000, 0x80011100, GP + 0x10
FUNCTIONS = {"f_a": 0x80020000, "f_b": 0x80030000, "f_c": 0x80031000, "f_d": 0x80032000}


def rdata_plus(offset):
    return (44, ("c", offset), (4, RDATA)) if offset else (4, RDATA)


def word(raw, address, value):
    struct.pack_into("<I", raw, 0x800 + address - LOAD, value)


class HoleUnit:
    """Object, symbol map and retail image of the unit; tests bend one of them."""

    def __init__(self):
        lui, lw, jr, nop = 0x3c020000, 0x8c420000, 0x00400008, 0
        text = [lui, lw, jr, nop] * 3 + [0x8f820000, jr, nop, nop]  # f_d: lw $2,%gp_rel($LC1)($gp)
        self.obj = SimpleNamespace(
            sections={TEXT: ".text", RDATA: ".rdata", SDATA: ".sdata"},
            code={TEXT: bytearray(struct.pack("<16I", *text)),
                  RDATA: bytearray(struct.pack("<8I", 0, 0, 1, 2, 3, 4, 0, 0)),
                  SDATA: bytearray(struct.pack("<2I", 7, 9))},
            patches={TEXT: [(82, 0x00, rdata_plus(0)), (84, 0x04, rdata_plus(0)),
                            (82, 0x10, rdata_plus(0x18)), (84, 0x14, rdata_plus(0x18)),
                            (82, 0x20, rdata_plus(0x18)), (84, 0x24, rdata_plus(0x18)),
                            (30, 0x30, (46, (12, SDATA), (44, ("c", 4), (4, SDATA))))],
                     RDATA: [(16, 0x00, text_plus(0x08)), (16, 0x04, text_plus(0x0c)),
                             (16, 0x18, text_plus(0x18)), (16, 0x1c, text_plus(0x1c))]},
            xdefs=[("f_a", TEXT, 0x00), ("f_b", TEXT, 0x10), ("f_c", TEXT, 0x20), ("f_d", TEXT, 0x30)],
            xrefs={})
        self.map = {"gp": hex(GP), "section_starts": {".sdata": hex(GP)},
                    "functions": {n: {"address": hex(a), "size": 16} for n, a in FUNCTIONS.items()}}
        raw = bytearray(0x800 + IMAGE)
        raw[:8] = b"PS-X EXE"
        struct.pack_into("<II", raw, 0x18, LOAD, IMAGE)
        for name, table in (("f_a", JT1), ("f_b", JT2), ("f_c", JT2)):
            base = FUNCTIONS[name]
            for i, insn in enumerate((lui | (table + 0x8000) >> 16, lw | table & 0xffff, jr, nop)):
                word(raw, base + 4 * i, insn)
        for i, insn in enumerate((0x8f820000 | (LC1 - GP), jr, nop, nop)):
            word(raw, FUNCTIONS["f_d"] + 4 * i, insn)
        word(raw, JT1, FUNCTIONS["f_a"] + 8)
        word(raw, JT1 + 4, FUNCTIONS["f_a"] + 12)
        word(raw, JT1 + 8, 0xdeadbeef)  # another function's literal sits in the hole
        word(raw, JT2, FUNCTIONS["f_b"] + 8)
        word(raw, JT2 + 4, FUNCTIONS["f_b"] + 12)
        word(raw, LC1, 9)
        self.raw = raw
        self.definitions = {}
        self.layout = LAYOUT

    def check(self, function):
        reference = bytecheck.Reference(bytes(self.raw))
        placements = bytecheck.ItemPlacements(self.obj, self.map, reference, self.definitions, LISTING, self.layout)
        return bytecheck.check(self.obj, self.map, reference, function, placements)

    def code(self, function, index, value):
        word(self.raw, FUNCTIONS[function] + 4 * index, value)


class ItemPlacementTests(unittest.TestCase):
    def test_hole_between_jump_tables_verifies(self):
        unit = HoleUnit()
        for function in ("f_a", "f_b", "f_c", "f_d"):
            self.assertEqual(unit.check(function)["status"], "match", function)
        # A stale whole-section base is not consulted for offsets that an item placement covers.
        unit.map["sections"] = {".rdata": hex(JT1), ".sdata": hex(GP)}
        for function in ("f_a", "f_b", "f_d"):
            self.assertEqual(unit.check(function)["status"], "match", function)

    def test_interior_and_resolved_addresses(self):
        unit = HoleUnit()
        reference = bytecheck.Reference(bytes(unit.raw))
        placements = bytecheck.ItemPlacements(unit.obj, unit.map, reference, {}, LISTING, LAYOUT)
        r = bytecheck.Resolver(unit.obj, unit.map, placements)
        self.assertEqual(r.resolve(rdata_plus(0x1c)), JT2 + 4)  # interior reference into $L30
        self.assertEqual(r.resolve((46, (12, SDATA), (44, ("c", 4), (4, SDATA)))), LC1 - GP)
        # The unrelated table has no anchor and the map no .rdata base: unproven, so loud.
        with self.assertRaisesRegex(bytecheck.CheckError, "no item placement and no section base"):
            r.resolve(rdata_plus(0x8))

    def test_data_definition_places_its_item(self):
        unit = HoleUnit()
        unit.definitions = {".rdata+0x18": {"address": hex(JT2), "size": 8}}
        self.assertEqual(unit.check("f_b")["status"], "match")

    def test_misplaced_item_fails(self):
        # The retail bytes at the implied address are not the object's table.
        unit = HoleUnit()
        word(unit.raw, JT2 + 4, 0)
        with self.assertRaisesRegex(bytecheck.CheckError, r"misplaced \.rdata\+0x18: object bytes differ"):
            unit.check("f_b")
        self.assertEqual(unit.check("f_a")["status"], "match")  # other items are unaffected
        # A data definition that the anchors contradict.
        unit = HoleUnit()
        unit.definitions = {".rdata+0x18": {"address": hex(JT2 + 4), "size": 8}}
        with self.assertRaisesRegex(bytecheck.CheckError, r"definition \.rdata\+0x18 is at 0x80011104 but relocations imply \['0x80011100'\]"):
            unit.check("f_b")
        # Anchors that point outside the retail section of the item's name.
        unit = HoleUnit()
        for function in ("f_b", "f_c"):
            unit.code(function, 0, 0x3c020000 | 0x8002)
        word(unit.raw, 0x80021100, FUNCTIONS["f_b"] + 8)
        word(unit.raw, 0x80021104, FUNCTIONS["f_b"] + 12)
        with self.assertRaisesRegex(bytecheck.CheckError, r"not inside the retail \.rdata"):
            unit.check("f_b")
        # Anchors that point into a function of the unit (no layout to stop them first).
        unit.layout = None
        for function in ("f_b", "f_c"):
            unit.code(function, 0, 0x3c020000 | 0x8002)
            unit.code(function, 1, 0x8c420000)
        word(unit.raw, FUNCTIONS["f_a"], FUNCTIONS["f_b"] + 8)
        word(unit.raw, FUNCTIONS["f_a"] + 4, FUNCTIONS["f_b"] + 12)
        with self.assertRaisesRegex(bytecheck.CheckError, "overlaps f_a"):
            unit.check("f_b")

    def test_ambiguous_anchor_fails(self):
        # f_c reads the table's second entry ($L30+4) at another retail address holding identical bytes.
        unit = HoleUnit()
        unit.obj.patches[TEXT] = [(k, at, rdata_plus(0x1c) if at in (0x20, 0x24) else e)
                                  for k, at, e in unit.obj.patches[TEXT]]
        unit.code("f_c", 1, 0x8c420000 | 0x1204)
        word(unit.raw, 0x80011200, FUNCTIONS["f_b"] + 8)
        word(unit.raw, 0x80011204, FUNCTIONS["f_b"] + 12)
        with self.assertRaisesRegex(bytecheck.CheckError, r"ambiguous \.rdata\+0x18: relocations imply several "
                                                          r"addresses \['0x80011100', '0x80011200'\]"):
            unit.check("f_b")
        self.assertEqual(unit.check("f_a")["status"], "match")
        # Two LO16 values for one target expression, then a LO16 half without its HI16 half.
        unit = HoleUnit()
        unit.code("f_c", 1, 0x8c420000 | 0x1200)
        with self.assertRaisesRegex(bytecheck.CheckError, r"HI16/LO16 halves of \.rdata\+0x18: hi \['0x8001'\] lo \['0x1100', '0x1200'\]"):
            unit.check("f_b")
        unit = HoleUnit()
        unit.obj.patches[TEXT] = [p for p in unit.obj.patches[TEXT] if p[1] not in (0x10, 0x20)]
        with self.assertRaisesRegex(bytecheck.CheckError, r"HI16/LO16 halves of \.rdata\+0x18: hi \[\] lo \['0x1100'\]"):
            unit.check("f_b")



class StaticUnit:
    """f_a (exported) calls s_b, a static function: ASPSX emits no XDEF for s_b, only a
    local-symbol record, and the call is a MIPS26 relocation to .text+0x10."""
    JAL, JR, NOP = 0x0c000000, 0x03e00008, 0

    def __init__(self):
        self.obj = SimpleNamespace(
            sections={TEXT: ".text"},
            code={TEXT: bytearray(struct.pack("<8I", self.JAL, self.NOP, self.JR, self.NOP,
                                              self.JR, self.NOP, self.NOP, self.NOP))},
            patches={TEXT: [(74, 0x00, text_plus(0x10))]},
            xdefs=[("f_a", TEXT, 0x00)], locals=[("s_b", TEXT, 0x10)], xrefs={9: "s_b"})
        self.map = {"functions": {"f_a": {"address": hex(FUNCTIONS["f_a"]), "size": 16},
                                  "s_b": {"address": hex(FUNCTIONS["f_c"]), "size": 16, "static": True}}}
        raw = bytearray(0x800 + IMAGE)
        raw[:8] = b"PS-X EXE"
        struct.pack_into("<II", raw, 0x18, LOAD, IMAGE)
        for i, insn in enumerate((self.JAL | (FUNCTIONS["f_c"] >> 2) & 0x03ffffff, self.NOP, self.JR, self.NOP)):
            word(raw, FUNCTIONS["f_a"] + 4 * i, insn)
        for i, insn in enumerate((self.JR, self.NOP, self.NOP, self.NOP)):
            word(raw, FUNCTIONS["f_c"] + 4 * i, insn)
        self.reference = bytecheck.Reference(bytes(raw))

    def check(self, function, symbol_map=None):
        return bytecheck.check(self.obj, symbol_map or self.map, self.reference, function)


class StaticFunctionTests(unittest.TestCase):
    def test_static_function_is_located_by_its_local_symbol(self):
        unit = StaticUnit()
        self.assertEqual(unit.check("s_b")["status"], "match")
        report = unit.check("f_a")  # the call resolves through the static's own placement
        self.assertEqual(report["status"], "match")
        self.assertEqual(report["patches"], [{"type": 74, "offset": 0, "applied_value": FUNCTIONS["f_c"]}])

    def test_extent_ends_at_the_next_static(self):
        unit = StaticUnit()
        unit.map["functions"]["f_a"]["size"] = 0x20  # would swallow s_b
        with self.assertRaisesRegex(bytecheck.CheckError, "does not match object function extent 0x10"):
            unit.check("f_a")

    def test_linkage_must_agree_with_the_object(self):
        unit = StaticUnit()
        exported = {"functions": {**unit.map["functions"], "s_b": {"address": hex(FUNCTIONS["f_c"]), "size": 16}}}
        with self.assertRaisesRegex(bytecheck.CheckError, "static in the object but the map declares it exported"):
            unit.check("s_b", exported)
        # A .text offset inside a function found under the wrong linkage is in no mapped function.
        with self.assertRaisesRegex(bytecheck.CheckError, "not inside a mapped function"):
            unit.check("f_a", exported)
        static_a = {"functions": {**unit.map["functions"], "f_a": {**unit.map["functions"]["f_a"], "static": True}}}
        with self.assertRaisesRegex(bytecheck.CheckError, "exported in the object but the map declares it static"):
            unit.check("f_a", static_a)
        with self.assertRaisesRegex(bytecheck.CheckError, "must be true or false"):
            unit.check("s_b", {"functions": {**unit.map["functions"], "s_b": {**unit.map["functions"]["s_b"], "static": 1}}})

    def test_external_reference_never_names_a_static(self):
        # An XREF s_b is some other unit's global, not this unit's static of the same name.
        unit = StaticUnit()
        resolver = bytecheck.Resolver(unit.obj, unit.map)
        with self.assertRaisesRegex(bytecheck.CheckError, "unmapped relocation symbol s_b"):
            resolver.resolve((2, 9))
        unit.map["symbols"] = {"s_b": "0x80055550"}
        resolver = bytecheck.Resolver(unit.obj, unit.map)
        self.assertEqual(resolver.resolve((2, 9)), 0x80055550)
        self.assertEqual(resolver.resolve(text_plus(0x14)), FUNCTIONS["f_c"] + 4)


def hi16(e):
    """asm/lib/hilo.h HI16(E) as ASPSX writes it: (E + 0x8000) >> 16."""
    return (60, ("c", 16), (44, ("c", 0x8000), e))


def lo16(e):
    """asm/lib/hilo.h LO16(E): E - (HI16(E) << 16)."""
    return (46, (58, ("c", 16), hi16(e)), e)


class SplitPairUnit:
    """f_s: lui a0, HI16(g_ext); jal f_ext; addiu a0, a0, LO16(g_ext); jr ra; nop; lui at,
    HI16(.text + 0x18); addiu at, at, LO16(.text + 0x18) (an end label: one past f_s)."""
    ADDRESS, TARGET, CALLEE = 0x80020000, 0x8001cd80, 0x80020800

    def __init__(self):
        words = [0x3c040000, 0x0c000000, 0x24840000, 0x03e00008, 0, 0]
        end = (44, ("c", 0x18), (4, TEXT))
        self.obj = SimpleNamespace(
            sections={TEXT: ".text"}, code={TEXT: bytearray(struct.pack("<6I", *words))},
            patches={TEXT: [(26, 0x00, hi16((2, 8))), (74, 0x04, (2, 7)), (30, 0x08, lo16((2, 8)))]},
            xdefs=[("f_s", TEXT, 0)], xrefs={7: "f_ext", 8: "g_ext"})
        self.map = {"functions": {"f_s": {"address": hex(self.ADDRESS), "size": 0x18}},
                    "symbols": {"f_ext": hex(self.CALLEE), "g_ext": hex(self.TARGET)}}
        raw = bytearray(0x800 + IMAGE)
        raw[:8] = b"PS-X EXE"
        struct.pack_into("<II", raw, 0x18, LOAD, IMAGE)
        hi, lo = (self.TARGET + 0x8000) >> 16, self.TARGET & 0xffff
        for i, w in enumerate((0x3c040000 | hi, 0x0c000000 | (self.CALLEE >> 2) & 0x3ffffff,
                               0x24840000 | lo, 0x03e00008, 0, 0)):
            word(raw, self.ADDRESS + 4 * i, w)
        self.raw = raw
        self.end = end

    def check(self):
        return bytecheck.check(self.obj, self.map, bytecheck.Reference(bytes(self.raw)), "f_s")


class SplitPairTests(unittest.TestCase):
    def test_shapes_are_recognised(self):
        e = (2, 8)
        self.assertEqual(bytecheck.split_half(26, hi16(e)), (82, e))
        self.assertEqual(bytecheck.split_half(30, lo16(e)), (84, e))
        # ASPSX folds the constants of a section-relative E: [.data] + 656 + 0x8000.
        folded_hi = (60, ("c", 16), (44, (4, 3), ("c", 656 + 0x8000)))
        folded_lo = (46, (58, ("c", 16), folded_hi), (44, (4, 3), ("c", 656)))
        self.assertEqual(bytecheck.split_half(26, folded_hi), (82, (44, ("c", 656), (4, 3))))
        self.assertEqual(bytecheck.split_half(30, folded_lo), (84, (44, ("c", 656), (4, 3))))
        # Not a split half: a plain 16-bit field, a GP-relative offset, a LO16 of another E.
        self.assertIsNone(bytecheck.split_half(26, e))
        self.assertIsNone(bytecheck.split_half(30, (46, (12, 4), e)))
        self.assertIsNone(bytecheck.split_half(30, (46, (58, ("c", 16), hi16((2, 9))), e)))
        self.assertIsNone(bytecheck.split_half(26, (60, ("c", 15), (44, ("c", 0x8000), e))))

    def test_split_pair_across_a_jal_matches(self):
        unit = SplitPairUnit()
        report = unit.check()
        self.assertEqual(report["status"], "match")
        self.assertEqual([(p["type"], p.get("split")) for p in report["patches"]],
                         [(26, "HI16"), (74, None), (30, "LO16")])

    def test_wrong_target_fails(self):
        unit = SplitPairUnit()
        unit.map["symbols"]["g_ext"] = hex(unit.TARGET + 0x10000)  # another symbol's address
        self.assertEqual(unit.check()["status"], "mismatch")

    def test_unpaired_half_fails(self):
        unit = SplitPairUnit()
        unit.obj.patches[TEXT] = [p for p in unit.obj.patches[TEXT] if p[0] != 30]
        with self.assertRaisesRegex(bytecheck.CheckError, "split HI16 of .* has no LO16 half"):
            unit.check()

    def test_other_16_bit_field_expressions_are_refused(self):
        unit = SplitPairUnit()
        unit.obj.patches[TEXT][0] = (26, 0x00, (2, 8))  # lui a0, g_ext: the full address
        unit.obj.patches[TEXT][2] = (30, 0x08, (2, 8))  # addiu a0, a0, g_ext
        with self.assertRaisesRegex(bytecheck.CheckError, "unsupported PsyQ patch type 26"):
            unit.check()

    def test_end_label_resolves_to_the_end_of_the_last_function(self):
        unit = SplitPairUnit()
        resolver = bytecheck.Resolver(unit.obj, unit.map)
        self.assertEqual(resolver.resolve(unit.end), unit.ADDRESS + 0x18)
        with self.assertRaisesRegex(bytecheck.CheckError, "not inside a mapped function"):
            resolver.resolve((44, ("c", 0x1c), (4, TEXT)))


if __name__ == "__main__":
    unittest.main()
