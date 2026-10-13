"""Tests of tools/lint_literals.py: python -m unittest tools/test_lint_literals.py"""
from __future__ import annotations

import contextlib
import importlib.util
import io
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("lint_literals", HERE / "lint_literals.py")
lint = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lint)
DATA = HERE / "testdata" / "lint"


def literals(text):
    return [lit for _, _, lit in lint.scan(text)]


class AllowedTest(unittest.TestCase):
    def test_fixture_has_no_findings(self):
        self.assertEqual(lint.scan((DATA / "allowed.c").read_text()), [])

    def test_each_allowed_case(self):
        for src in ("/* 0x1 */", "// 0x1", 'char *s = "0x1";', "int c = '0x1';",
                    "#define A 0x1", "#define A \\\n 0x1", "enum { A = 0x1 };", "enum T { A = 0x1 };",
                    "int t[] = { 0x1, 0x2 };", "struct S s = { { 0x1 }, .a = 0x2 };",
                    "// continued \\\n 0x1\n"):
            with self.subTest(src=src):
                self.assertEqual(lint.scan(src), [])

    def test_decimal_and_float_ignored(self):
        self.assertEqual(lint.scan("int f(void) { return 10 + 1e+3 + 017; }"), [])


class ViolationTest(unittest.TestCase):
    def test_fixture_findings_exact(self):
        found = [(ln, lit) for ln, _, lit in lint.scan((DATA / "violations.c").read_text())]
        self.assertEqual(found, [
            (1, "0x10"),    # function-like macro body is code
            (3, "0x20"),    # other directive
            (7, "0x10"),    # array bound outside the initialiser braces
            (10, "0x30"),   # local initialiser
            (11, "0x31"), (11, "0x32"),   # local brace initialiser
            (13, "0x40"),   # struct-member store
            (14, "0xFF00"),  # mask
            (15, "0Xabu"),
            (16, "0x4"), (16, "0x50"),   # array index, store
            (18, "0x60"),   # case label
            (21, "0x70"),   # function argument
            (25, "0x80"),   # return expression after an enum earlier in the file
        ])

    def test_each_kind(self):
        cases = {
            "expr": "int f(int a) { return a + 0x1; }",
            "mask": "int f(int a) { return a & 0xFF; }",
            "argument": "void f(void) { g(0x1); }",
            "case": "void f(int a) { switch (a) { case 0x1: break; } }",
            "index": "int f(int *a) { return a[0x1]; }",
            "local init": "void f(void) { int a = 0x1; }",
            "member store": "void f(struct S *s) { s->m = 0x1; }",
            "function-like macro": "#define M(x) ((x) & 0x1)",
            "array bound": "int a[0x10];",
            "lowercase suffix": "int a = 0xffUL;",
        }
        for name, src in cases.items():
            with self.subTest(name):
                self.assertEqual(len(lint.scan(src)), 1)

    def test_enum_returning_function_body_is_code(self):
        self.assertEqual(literals("enum E f(void) { return 0x1; }"), ["0x1"])

    def test_position(self):
        self.assertEqual(lint.scan("int a;\n  x = 0x1F;\n"), [(2, 7, "0x1F")])

    def test_position_after_continuation_and_comment(self):
        self.assertEqual(lint.scan("#define A \\\n 1\n/* a\nb */ x = 0x2;"), [(4, 10, "0x2")])


def decimals(text):
    return [lit for _, _, lit in lint.scan(text, decimal=True)]


class DecimalTest(unittest.TestCase):
    def test_allowed_fixture_has_no_findings(self):
        self.assertEqual(lint.scan((DATA / "decimal_allowed.c").read_text(), decimal=True), [])

    def test_hex_fixture_decimals(self):
        found = [(ln, lit) for ln, _, lit in lint.scan((DATA / "allowed.c").read_text(), decimal=True)]
        # array bound, file-scope scalar initialiser (only brace initialisers are data), octal in code
        self.assertEqual(found, [(18, "4"), (25, "10"), (26, "077")])

    def test_violations_fixture_exact(self):
        found = [(ln, lit) for ln, _, lit in lint.scan((DATA / "decimal_violations.c").read_text(), decimal=True)]
        self.assertEqual(found, [
            (1, "16"),     # function-like macro body is code
            (3, "3"),      # other directive
            (6, "10"),     # array bound outside the initialiser braces
            (9, "30"),     # local initialiser
            (10, "31"), (10, "32"),   # local brace initialiser
            (11, "255"),   # mask
            (12, "4"),     # shift
            (13, "5"), (13, "3"),     # array index, negative store
            (15, "6"),     # case label
            (18, "070"), (18, "7u"),  # octal and suffixed arguments
            (19, "8"),     # multiplying a sizeof term is outside its operand
        ])

    def test_small_values_allowed(self):
        for src in ("x = 0;", "x = 1;", "x = -1;", "x = y / 2;", "x = 2u;", "x = 00;", "x = 1L;"):
            with self.subTest(src=src):
                self.assertEqual(decimals("void f(void) { " + src + " }"), [])

    def test_sizeof_operand(self):
        self.assertEqual(decimals("int f(void) { return sizeof(char[16]); }"), [])
        self.assertEqual(decimals("int f(void) { return sizeof((char (*)[16])0); }"), [])
        self.assertEqual(decimals("int f(void) { return sizeof(T) * 16; }"), ["16"])
        self.assertEqual(decimals("int f(void) { return g(16, sizeof(T)); }"), ["16"])
        self.assertEqual(decimals("#define M(x) sizeof(char[16])"), [])

    def test_hex_and_floats_ignored(self):
        self.assertEqual(decimals("int f(void) { return 0x10 + 1.5 + 2e3 + .5f; }"), [])

    def test_hex_mode_unchanged_by_sizeof(self):
        self.assertEqual(literals("int f(void) { return sizeof(char[0x10]); }"), ["0x10"])

    def test_position(self):
        self.assertEqual(lint.scan("int a;\n  x = 31;\n", decimal=True), [(2, 7, "31")])


class MainTest(unittest.TestCase):
    def test_exit_status(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            self.assertEqual(lint.main(["--files", str(DATA / "allowed.c")]), 0)
            self.assertEqual(lint.main(["--files", str(DATA / "violations.c")]), 1)
            self.assertEqual(lint.main(["--files", str(DATA / "violations.c"), "--report-only"]), 0)
        self.assertIn("violations.c:25:", buf.getvalue())

    def test_decimal_exit_status(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            self.assertEqual(lint.main(["--decimal", "--files", str(DATA / "decimal_allowed.c")]), 0)
            self.assertEqual(lint.main(["--decimal", "--files", str(DATA / "decimal_violations.c")]), 1)
        self.assertIn("decimal_violations.c:19:", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
