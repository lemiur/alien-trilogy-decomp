"""Tests of tools/progress.py's counting logic: python -m unittest tools/test_progress.py"""
from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("progress", Path(__file__).resolve().parent / "progress.py")
progress = importlib.util.module_from_spec(spec)
spec.loader.exec_module(progress)

LOAD = 0x1000


def run(address, size, kind, unit=None, symbol=None):
    return {"address": hex(address), "size": size, "kind": kind, "unit": unit, "symbol": symbol}


class CategorizeTest(unittest.TestCase):
    RUNS = [run(0, 0x800, "fallback") | {"address": None},  # header: not payload
            run(0x1000, 0x10, "function"), run(0x1010, 0x8, "data"), run(0x1018, 0x4, "generated"),
            run(0x101c, 0x4, "fallback"), run(0x1020, 0x10, "asm"), run(0x1030, 0x20, "fallback"),
            run(0x1050, 0x8, "library_asm"), run(0x1058, 0x8, "fallback")]

    def test_every_category_counted_once(self):
        totals = progress.categorize(self.RUNS, LOAD, 0x60, [(0x1030, 0x8)], [(0x1038, 0x4)])
        self.assertEqual(totals, {"c_function": 0x10, "c_verbatim": 0, "c_data": 8, "generated_data": 4,
                                  "asm": 0x10, "library_asm": 8, "library_text": 8, "library_data": 4,
                                  "fallback": 0x20})

    def test_library_on_library_asm_bytes_is_refused(self):
        # A range the runtime reports certify (or claim as library data) must not also be
        # library assembly: the two claims are exclusive.
        with self.assertRaisesRegex(progress.ProgressError, "overlaps library_asm"):
            progress.categorize(self.RUNS, LOAD, 0x60, [(0x1050, 0x8)], [])
        with self.assertRaisesRegex(progress.ProgressError, "overlaps library_asm"):
            progress.categorize(self.RUNS, LOAD, 0x60, [], [(0x1054, 0x4)])

    def test_library_on_source_bytes_is_refused(self):
        with self.assertRaisesRegex(progress.ProgressError, "overlaps c_function"):
            progress.categorize(self.RUNS, LOAD, 0x60, [(0x100c, 0x8)], [])

    def test_library_text_and_data_overlap_is_refused(self):
        with self.assertRaisesRegex(progress.ProgressError, "overlaps library_text"):
            progress.categorize(self.RUNS, LOAD, 0x60, [(0x1030, 0x8)], [(0x1034, 0x8)])

    def test_uncovered_payload_is_refused(self):
        with self.assertRaisesRegex(progress.ProgressError, "does not cover"):
            progress.categorize(self.RUNS[:-1], LOAD, 0x60, [], [])


class PlacementTest(unittest.TestCase):
    def latest(self):
        return {"checks": [{"unit": "a", "function": "f_1", "address": "0x1000", "size": 16,
                            "status": "match", "kind": "c"},
                           {"unit": "g", "function": "f_2", "address": "0x1020", "size": 16,
                            "status": "match", "kind": "asm"},
                           {"unit": "s", "function": "SpuGetKeyStatus", "address": "0x1040", "size": 8,
                            "status": "match", "kind": "library_asm"}],
                "data_checks": [{"unit": "a", "status": "match", "kind": "c", "definitions": [
                                    {"symbol": ".rdata+0x0", "address": "0x1010", "size": 4}]},
                                {"unit": "b", "status": "match", "kind": "c", "definitions": [
                                    {"symbol": ".rdata+0x0", "address": "0x1014", "size": 4}]},
                                {"unit": "s", "status": "match", "kind": "library_asm", "definitions": [
                                    {"symbol": "_spu_RXX", "address": "0x1048", "size": 4}]}]}

    def owners(self):
        return [{"kind": "fallback", "unit": None, "symbol": None},
                {"kind": "function", "unit": "a", "symbol": "f_1", "address": "0x1000", "size": 16},
                {"kind": "data", "unit": "a", "symbol": ".rdata+0x0", "address": "0x1010", "size": 4},
                {"kind": "data", "unit": "b", "symbol": ".rdata+0x0", "address": "0x1014", "size": 4},
                {"kind": "asm", "unit": "g", "symbol": "f_2", "address": "0x1020", "size": 16},
                {"kind": "library_asm", "unit": "s", "symbol": "SpuGetKeyStatus", "address": "0x1040", "size": 8},
                {"kind": "library_asm", "unit": "s", "symbol": "_spu_RXX", "address": "0x1048", "size": 4}]

    def test_positional_names_in_different_units_are_distinct(self):
        report = progress.report_placements(self.latest())
        self.assertEqual(len(report["data"]), 2)
        self.assertEqual(report["asm:function"], {("g", "f_2", 0x1020, 16)})
        self.assertNotIn(("g", "f_2", 0x1020, 16), report["functions"])
        # Library assembly is its own category, apart from C and from hand-written asm.
        self.assertEqual(report["library_asm:function"], {("s", "SpuGetKeyStatus", 0x1040, 8)})
        self.assertEqual(report["library_asm:data"], {("s", "_spu_RXX", 0x1048, 4)})
        self.assertEqual(report["asm:data"], set())
        self.assertEqual(len(report["data"]), 2)

    def test_agreeing_placements(self):
        placed = progress.gate_placements(self.owners(), {"a": {"f_1"}, "g": {"f_2"}, "s": {"SpuGetKeyStatus"}})
        self.assertEqual(placed["library_asm:data"], {("s", "_spu_RXX", 0x1048, 4)})
        self.assertEqual(progress.compare_placements(placed, progress.report_placements(self.latest())), [])

    def test_static_functions_of_one_name_in_two_units_are_distinct(self):
        # Two units may each own a static of one name (tools/lnk_symbols.py); a function is
        # counted by (unit, symbol, address, size), never by name alone.
        latest = self.latest()
        latest["checks"] += [{"unit": u, "function": "helper", "address": a, "size": 8, "status": "match", "kind": "c"}
                             for u, a in (("a", "0x1030"), ("b", "0x1038"))]
        owners = self.owners() + [{"kind": "function", "unit": u, "symbol": "helper", "address": a, "size": 8}
                                  for u, a in (("a", "0x1030"), ("b", "0x1038"))]
        functions = {"a": {"f_1", "helper"}, "b": {"helper"}, "g": {"f_2"}, "s": {"SpuGetKeyStatus"}}
        placed = progress.gate_placements(owners, functions)
        self.assertEqual(placed["c_function"], {("a", "f_1", 0x1000, 16), ("a", "helper", 0x1030, 8),
                                                ("b", "helper", 0x1038, 8)})
        self.assertEqual(progress.compare_placements(placed, progress.report_placements(latest)), [])
        problems = progress.compare_placements(progress.gate_placements(owners[:-1], functions),
                                               progress.report_placements(latest))
        self.assertEqual(problems, ["verify.py matched function b:helper at 0x1038 that the gate did not place"])

    def test_disagreement_is_named(self):
        placed = progress.gate_placements(self.owners()[:-4], {"a": {"f_1"}, "g": {"f_2"}})
        problems = progress.compare_placements(placed, progress.report_placements(self.latest()))
        self.assertEqual(len(problems), 4)
        self.assertTrue(any("b:.rdata+0x0" in p for p in problems))
        self.assertTrue(any("asm function g:f_2" in p for p in problems))
        self.assertTrue(any("library asm function s:SpuGetKeyStatus" in p for p in problems))
        self.assertTrue(any("library asm datum s:_spu_RXX" in p for p in problems))

    def test_library_asm_placed_as_hand_written_asm_is_a_disagreement(self):
        owners = [dict(o, kind="asm") if o["kind"] == "library_asm" else o for o in self.owners()]
        functions = {"a": {"f_1"}, "g": {"f_2"}, "s": {"SpuGetKeyStatus"}}
        problems = progress.compare_placements(progress.gate_placements(owners, functions),
                                               progress.report_placements(self.latest()))
        self.assertEqual(len(problems), 4)


class AsmSplitTest(unittest.TestCase):
    SOURCE = ("/* header .word 1 */\n\t.text\nf_code:\n\tnop\n\tjr\tra\n\tnop\n"
              "t_table:\n\t.word\t1, 2\nt_more:\n\t.half\t3\n\t.half\t4\n")
    SPEC = {"functions": {"f_code": {"address": "0x800558e0", "size": 12},
                          "t_table": {"address": "0x800558ec", "size": 8},
                          "t_more": {"address": "0x800558f4", "size": 4}}}

    def asm_units(self):
        return progress.load_module("verify_t", Path(__file__).resolve().parent / "verify.py").asm_units

    def test_tables_are_labels_owning_data_directives(self):
        self.assertEqual(self.asm_units().table_labels(self.SOURCE, self.SPEC), {"t_table", "t_more"})

    def test_data_label_outside_window_is_a_routine(self):
        spec = {"functions": {"t_table": {"address": "0x80050000", "size": 8}}}
        self.assertEqual(self.asm_units().table_labels(self.SOURCE, spec), set())

    def test_split_asm(self):
        items = {("g", "f_code", 0x800558e0, 12), ("g", "t_table", 0x800558ec, 8), ("h", "t_table", 0x1, 4)}
        routines, tables = progress.split_asm(items, {"g": {"t_table"}})
        self.assertEqual(routines, {("g", "f_code", 0x800558e0, 12), ("h", "t_table", 0x1, 4)})
        self.assertEqual(tables, {("g", "t_table", 0x800558ec, 8)})


class RulesHeadlineTest(unittest.TestCase):
    SUMMARY = {"matched_under_the_rules": 1200, "byte_matched_functions": 1300, "match_pending_functions": 60,
               "functions_with_unexcused_match_findings": 40, "failing_functions_by_rule": {"R2": 30, "R4": 10}}

    def test_headline_comes_first_with_both_numbers(self):
        line = progress.rules_lines(self.SUMMARY)[0]
        self.assertTrue(line.startswith("**Matched under the rules: 1,200 of 1,300 byte-matched game C functions.**"))
        self.assertIn("MATCH-PENDING: 60", line)
        self.assertIn("(R2 30, R4 10)", line)

    def test_no_summary_no_headline(self):
        self.assertEqual(progress.rules_lines(None), [])

    def test_disagreeing_function_counts_are_refused(self):
        class FakeChecker:
            def summary(self):
                return {"byte_matched_functions": 3}

        class FakeRules:
            @staticmethod
            def check(decimal):
                return FakeChecker()

        original = progress.load_module
        progress.load_module = lambda name, path, register=False: FakeRules
        try:
            with self.assertRaises(progress.ProgressError):
                progress.rules_summary(4)
            self.assertEqual(progress.rules_summary(3), {"byte_matched_functions": 3})
        finally:
            progress.load_module = original


if __name__ == "__main__":
    unittest.main()
