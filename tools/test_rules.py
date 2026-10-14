"""Tests for tools/rules.py on the frozen fixtures in tools/testdata/rules/ (never live units)."""
from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rules  # noqa: E402

DATA = Path(__file__).resolve().parent / "testdata/rules"
UNITS = [(u, DATA / f"src/{u}.c", DATA / f"config/{u}.json") for u in ("alpha", "beta")]


def run(files=None):
    files = files or [DATA / "src/alpha.c", DATA / "src/beta.c", DATA / "include/ALPHA.H"]
    return rules.check(files=files, units=UNITS, include_dir=DATA / "include", sdk_dir=None,
                       exceptions_path=DATA / "config/exceptions.json",
                       match_pending_path=DATA / "config/match_pending.json")


GAMMA = [("gamma", DATA / "src/gamma.c", DATA / "config/gamma.json")]


class ByteCursorTest(unittest.TestCase):
    """R4 beyond the constant-offset cast: raw byte cursors (fixture unit gamma)."""

    @classmethod
    def setUpClass(cls):
        cls.checker = rules.check(files=[DATA / "src/gamma.c"], units=GAMMA, include_dir=DATA / "include",
                                  sdk_dir=None, exceptions_path=DATA / "config/exceptions.json",
                                  match_pending_path=DATA / "config/match_pending.json")

    def r4(self, function):
        return [f for f in self.checker.findings if f.rule == "R4" and f.function == function]

    def test_byte_pointer_stepped_by_member_size_or_offsetof(self):
        for function in ("gamma_member_size_cursor", "gamma_member_size_plain", "gamma_member_size_assign",
                         "gamma_offsetof_step", "gamma_null_base_offset", "gamma_cast_step",
                         "gamma_int_cursor"):
            self.assertTrue(any("member's size or an offset" in f.message for f in self.r4(function)), function)

    def test_cursor_pattern_reports_each_step_and_read(self):
        lines = {f.line: f.message for f in self.r4("gamma_member_size_cursor")}
        self.assertEqual(sorted(lines), [36, 37, 38])
        self.assertIn("stepped", lines[36])
        self.assertIn("dereferenced", lines[37])

    def test_sizeof_operand_is_not_a_dereference(self):
        self.assertEqual([f for f in self.r4("gamma_member_size_assign") if "dereferenced" in f.message], [])

    def test_byte_pointer_dereferenced_through_a_cast(self):
        for function in ("gamma_deref_short", "gamma_deref_parenthesised", "gamma_member_through_cast",
                         "gamma_index_through_cast", "gamma_local_cursor"):
            self.assertTrue(any("dereferenced through a cast" in f.message for f in self.r4(function)), function)

    def test_typed_access_word_copy_register_and_file_buffer_are_clean(self):
        for function in ("gamma_typed", "gamma_plain_steps", "gamma_word_copy", "gamma_register",
                         "gamma_file_buffer"):
            hits = [f.format() for f in self.checker.findings if f.function == function and f.tier == "match"]
            self.assertEqual(hits, [], function)
        self.assertTrue([f for f in self.checker.findings if f.rule == "R19" and f.function == "gamma_word_copy"])


class FixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.checker = run()
        cls.summary = cls.checker.summary()

    def found(self, rule, function=None, state="open"):
        return [f for f in self.checker.findings
                if f.rule == rule and (function is None or f.function == function) and f.state == state]

    def assertFound(self, rule, function, state="open"):
        self.assertTrue(self.found(rule, function, state), f"no {state} {rule} finding in {function}")

    def assertClean(self, function):
        hits = [f.format() for f in self.checker.findings if f.function == function and f.tier == "match"]
        self.assertEqual(hits, [])

    def test_lex_tracks_lines_through_comments_and_strings(self):
        src = rules.lex(Path("x.c"), "x.c", '/* a\n b */ int x = "s;\\"t";\n// c\nint y;\n')
        self.assertEqual([(t.text, t.line) for t in src.toks if t.kind == "id"], [("int", 2), ("x", 2), ("int", 4),
                                                                                 ("y", 4)])
        self.assertEqual(len(src.comments), 2)

    def test_knr_definition_is_one_function(self):
        funcs, _ = rules.parse_top(self.checker.sources[0])
        self.assertIn("alpha_knr", [f.name for f in funcs])
        self.assertFound("R17", "alpha_knr")

    def test_r2_extern_owned_elsewhere_or_in_header(self):
        names = {f.name for f in self.found("R2")}
        self.assertEqual(names, {"g_beta_total", "g_header_only"})
        self.assertEqual([f.name for f in self.found("R2-pending")], ["g_unowned"])
        self.assertIn(("src/alpha.c", "alpha_extern_user"), {(k[0][-11:], k[1]) for k in
                                                             self.checker.blocked_functions()})

    def test_forward_declaration_of_own_static_is_fine(self):
        self.assertFalse([f for f in self.checker.findings if f.name == "alpha_forward"])
        self.assertClean("alpha_forward")

    def test_r4_offset_access(self):
        self.assertFound("R4", "alpha_offset")
        self.assertFound("R4", "alpha_int_step")
        self.assertClean("alpha_relocate")

    def test_r17_volatile(self):
        self.assertFound("R17", "alpha_volatile_local")
        self.assertFound("R17", "alpha_volatile_cast")
        self.assertClean("alpha_vblank")
        unproven = [f for f in self.found("R17") if f.name == "g_unproven_flag"]
        self.assertTrue(unproven)
        self.assertIn("alpha_unproven", {k[1] for k in unproven[0].users})

    def test_r17_fake_code(self):
        for function in ("alpha_dummy", "alpha_empty_if", "alpha_bare_semicolon", "alpha_do_while",
                         "alpha_self_assign", "alpha_neutral"):
            self.assertFound("R17", function)
        self.assertEqual(len(self.found("R17", "alpha_neutral")), 2)

    def test_r18_inline_asm(self):
        self.assertFound("R18", "alpha_asm")

    def test_r1_duplicate_types(self):
        self.assertTrue([f for f in self.found("R1") if f.name == "Shared"])
        self.assertTrue([f for f in self.found("R1-copy") if "u8" in f.message])
        self.assertTrue(self.found("R1-local"))

    def test_advisory_checks(self):
        self.assertFound("R14", "alpha_goto")
        self.assertFound("R7", "f_80012345")
        self.assertFound("R11", "f_80012345")
        self.assertFound("R31", "alpha_swap")
        self.assertTrue(self.found("R21"))
        self.assertTrue(self.found("R37"))
        self.assertClean("alpha_goto")
        self.assertClean("alpha_swap")

    def test_match_pending_marker_file_and_revoked_exception(self):
        for function in ("alpha_pending", "alpha_pending_file", "alpha_revoked"):
            self.assertFound("R17", function, state="pending")
            self.assertFalse(self.found("R17", function))

    def test_rule_exception_excuses_advisory_only(self):
        self.assertFound("R14", "alpha_excused_goto", state="excused")
        self.assertFound("R14", "alpha_json_excused", state="excused")
        self.assertFound("R4", "alpha_bad_exception")
        invalid = {f.function for f in self.checker.invalid}
        self.assertEqual(invalid, {"alpha_bad_exception", "alpha_offset", "alpha_dead_local_vague",
                                   "alpha_dead_local_unruled"})

    def test_r17_dead_local_exception(self):
        for function in ("alpha_dead_local", "alpha_dead_local_header"):
            self.assertFound("R17", function, state="excused")
            self.assertNotIn(function, {k[1] for k in self.checker.blocked_functions()})
        for function in ("alpha_dead_local_vague", "alpha_dead_local_unruled", "alpha_dead_local_misplaced"):
            self.assertFound("R17", function)
        self.assertFound("R17", "alpha_dead_local_other", state="excused")
        self.assertEqual([f.message for f in self.found("R17", "alpha_dead_local_other")], ["bare ';' statement"])
        invalid = {f.function: f.message for f in self.checker.invalid if f.rule == "R17"}
        self.assertEqual(set(invalid), {"alpha_dead_local_vague", "alpha_dead_local_unruled"})
        self.assertIn("fewer than", invalid["alpha_dead_local_vague"])
        self.assertIn("ruled-out", invalid["alpha_dead_local_unruled"])
        self.assertEqual([s.rsplit(":", 1)[1] for s in self.summary["dead_local_exceptions"]],
                         ["alpha_dead_local", "alpha_dead_local_header"])

    def test_dead_local_evidence_grammar(self):
        problem = rules.dead_local_evidence_problem
        self.assertIsNone(problem("two RECTs: the size and type of the callee's rects; ruled out: inlined callee"))
        self.assertIsNotNone(problem(""))
        self.assertIsNotNone(problem("frame"))
        self.assertIsNotNone(problem("the eight unused bytes come from the callee's two RECTs here"))
        self.assertIsNotNone(problem("nothing at all explains this space; ruled out: inline callees and macros"))

    def test_inline_helper_passes_findings_to_callers(self):
        blocked = {k[1]: v for k, v in self.checker.blocked_functions().items()}
        self.assertEqual(blocked.get("alpha_helper_caller"), {"R17"})

    def test_summary(self):
        s = self.summary
        self.assertEqual(s["byte_matched_functions"], 37)
        self.assertEqual(s["function_definitions"], 39)
        self.assertEqual(s["match_pending_functions"], 3)
        self.assertEqual(s["functions_with_unexcused_match_findings"], 22)
        self.assertEqual(s["matched_under_the_rules"], 12)
        self.assertEqual(s["invalid_exceptions"], 4)
        self.assertEqual(s["mapped_functions_not_found"], [])
        text = rules.format_summary(s)
        self.assertTrue(text.startswith("rules: 12 of 37 byte-matched functions matched under the rules"))

    def test_clean_file_alone(self):
        c = run([DATA / "src/beta.c"])
        s = c.summary()
        self.assertEqual(s["byte_matched_functions"], 3)
        # the header's Shared still clashes with beta.c's: indexed although not scanned
        self.assertEqual(s["matched_under_the_rules"], 2)

    def test_finding_format(self):
        f = rules.Finding("src/a.c", 3, "R17", "bare ';' statement", "f")
        self.assertEqual(f.format(), "src/a.c:3: RULE R17 [match]: bare ';' statement (f)")
        self.assertEqual(rules.Finding("src/a.c", 3, "R14", "goto x").tier, "advisory")


if __name__ == "__main__":
    unittest.main()
