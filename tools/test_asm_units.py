"""Tests of the assembly-category policy (tools/asm_units.py, config/asm_regions.json) and of
how the runtime reports treat library assembly (analysis/runtime_relocations.py):
python -m unittest tools/test_asm_units.py"""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


asm_units = load("asm_units_t", TOOLS / "asm_units.py")


def policy_file(tmp, edit=None):
    """A small regions config: one renderer range with a table window, two library text ranges
    and one library data range."""
    config = {"categories": {
        "asm": {"label": "assembly", "text": [["0x1000", "0x1fff"]],
                "text_data": [["0x1800", "0x1fff"]], "data": []},
        "library_asm": {"label": "library assembly", "text": [["0x3000", "0x30ff"], ["0x3200", "0x32ff"]],
                        "text_data": [], "data": [["0x9000", "0x90ff"]]}}}
    if edit:
        edit(config["categories"])
    path = Path(tmp) / "regions.json"
    path.write_text(json.dumps(config))
    return path


class PolicyTest(unittest.TestCase):
    def test_ranges_are_inclusive_and_per_category(self):
        with tempfile.TemporaryDirectory() as tmp:
            policy = asm_units.load_policy(policy_file(tmp))
        inside = lambda *args: asm_units.inside(*args, policy=policy)
        self.assertTrue(inside(0x1ffc, 4, "asm"))
        self.assertFalse(inside(0x1ffc, 8, "asm"))
        self.assertTrue(inside(0x3200, 0x100, "library_asm"))
        self.assertFalse(inside(0x30fc, 8, "library_asm"))          # straddles two ranges' gap
        self.assertFalse(inside(0x3000, 4, "asm"))                  # library text is not renderer text
        self.assertFalse(inside(0x1000, 4, "library_asm"))
        self.assertTrue(inside(0x9000, 4, "library_asm", False))    # library data
        self.assertFalse(inside(0x9000, 4, "asm", False))           # renderer units have no data

    def test_inconsistent_configs_are_refused(self):
        def overlap(c):
            c["library_asm"]["text"].append(["0x1ff0", "0x2000"])
        def window(c):
            c["asm"]["text_data"] = [["0x1800", "0x2010"]]
        def data_on_text(c):
            c["library_asm"]["data"].append(["0x3010", "0x3013"])
        def unknown(c):
            c["other_asm"] = c["asm"]
        def empty(c):
            c["asm"]["text"] = [["0x2000", "0x1000"]]
        for edit in (overlap, window, data_on_text, unknown, empty):
            with self.subTest(edit.__name__), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(asm_units.AsmPolicyError):
                    asm_units.load_policy(policy_file(tmp, edit))

    def test_live_config_matches_agents_md(self):
        policy = asm_units.load_policy()
        self.assertEqual(policy["asm"].text, ((0x80048f54, 0x800588bc),))
        self.assertEqual(policy["asm"].text_data, ((0x800558e8, 0x800588bc),))
        library = policy["library_asm"]
        self.assertEqual(len(library.text), 16)
        self.assertEqual(sum(hi - lo for lo, hi in library.text), 45096)
        self.assertEqual(library.text_data, ())
        # LIBCD C_010 (text 0x80061310, data 0x800748a0) is certifiable 3.3, not library assembly.
        self.assertFalse(asm_units.inside(0x80061310, 4, "library_asm"))
        self.assertFalse(asm_units.inside(0x800748a0, 4, "library_asm", False))

    def test_region_violations_name_text_and_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            policy = asm_units.load_policy(policy_file(tmp))
        spec = {"functions": {"Ok": {"address": "0x3000", "size": 8}, "Bad": {"address": "0x3100", "size": 8}}}
        defs = {"good": {"address": "0x9000", "size": 4}, "bad": {"address": "0x9100", "size": 4}}
        problems = asm_units.region_violations("u", spec, defs, "library_asm", policy)
        self.assertEqual(len(problems), 2)
        self.assertIn("Bad at 0x3100", problems[0])
        self.assertIn("bad at 0x9100", problems[1])


class RuntimeMarkingTest(unittest.TestCase):
    """A runtime candidate placed by library assembly is "library_asm", never certified or
    conflicting library; a partial cover is an error."""

    @classmethod
    def setUpClass(cls):
        cls.rr = load("runtime_relocations_t", TOOLS.parent / "analysis" / "runtime_relocations.py")

    def modules(self):
        return [{"module": "S_GKS", "address": "0x800590a8", "text_size": 148, "status": "unresolved"},
                {"module": "S_SCA", "address": "0x8005bf9c", "text_size": 916, "status": "unresolved"},
                {"module": "COUNTER", "address": "0x8005d120", "text_size": 368, "status": "full_match"}]

    def test_whole_cover_marks_the_module(self):
        modules = self.modules()
        self.rr.mark_library_asm(modules, [("libspu_s_gks", "SpuGetKeyStatus", 0x800590a8, 0x8005913c)])
        self.assertEqual(modules[0]["status"], "library_asm")
        self.assertEqual(modules[0]["archive_status"], "unresolved")
        self.assertEqual(modules[0]["library_asm_units"], ["libspu_s_gks"])
        self.assertEqual([m["status"] for m in modules[1:]], ["unresolved", "full_match"])

    def test_cover_by_several_routines(self):
        modules = self.modules()
        self.rr.mark_library_asm(modules, [("u", "a", 0x8005bf9c, 0x8005c000), ("u", "b", 0x8005c000, 0x8005c330)])
        self.assertEqual(modules[1]["status"], "library_asm")

    def test_partial_cover_is_refused(self):
        for placed in ([("u", "a", 0x800590a8, 0x80059100)],                         # short
                       [("u", "a", 0x800590a8, 0x80059100), ("u", "b", 0x80059104, 0x8005913c)],  # hole
                       [("u", "a", 0x800590a0, 0x8005913c)]):                        # spills over
            with self.subTest(placed=placed), self.assertRaises(ValueError):
                self.rr.mark_library_asm(self.modules(), placed)

    def test_registry_ranges_come_from_the_maps(self):
        verify = load("verify_t", TOOLS / "verify.py")
        ranges = verify.library_asm_text_ranges()
        for unit, name, start, end in ranges:
            self.assertEqual(verify.asm_category(unit), "library_asm")
            self.assertTrue(asm_units.inside(start, end - start, "library_asm"))


if __name__ == "__main__":
    unittest.main()
