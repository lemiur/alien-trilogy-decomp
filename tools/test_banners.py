"""Tests for tools/banners.py on the frozen fixture in tools/testdata/banners/ (never live units)."""
from __future__ import annotations

from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import banners  # noqa: E402

DATA = Path(__file__).resolve().parent / "testdata/banners"


def read(name: str) -> str:
    return (DATA / name).read_text(encoding="ascii")


class ConvertTest(unittest.TestCase):
    def test_fixture_output_is_frozen(self):
        self.assertEqual(banners.convert(read("input.c")), read("expected.c"))

    def test_idempotent(self):
        expected = read("expected.c")
        self.assertEqual(banners.convert(expected), expected)

    def test_only_comments_change(self):
        def code(text):
            stripped = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            return [line for line in stripped.splitlines() if line.strip()]
        self.assertEqual(code(read("input.c")), code(banners.convert(read("input.c"))))

    def test_one_banner_per_definition(self):
        lines = banners.convert(read("input.c")).splitlines()
        boxes = sum(1 for i, line in enumerate(lines) if line == "/*" and lines[i + 1] == banners.BAR)
        self.assertEqual(boxes, len(banners.function_definitions(lines)))


class DefinitionTest(unittest.TestCase):
    def test_definitions_found(self):
        names = [n for _, n in banners.function_definitions(read("input.c").split("\n"))]
        self.assertEqual(names, ["Fx_Bare", "Fx_Add", "Fx_Spawn", "Fx_Clear", "Fx_Walk", "f_12345", "Fx_Stride",
                                 "Fx_Wrapped", "Fx_Long", "Fx_Done", "Fx_Orphan", "Fx_Mention"])

    def test_markers_stay_outside_the_banner(self):
        out = banners.convert(read("input.c"))
        banner_of = {}
        lines = out.split("\n")
        for i, l in enumerate(lines):
            if l == "/*" and lines[i + 1] == banners.BAR:
                end = lines.index("*/", i)
                banner_of[lines[i + 2]] = "\n".join(lines[i:end])
        for name in ("Fx_Stride", "f_12345", "Fx_Walk", "Fx_Clear", "Fx_Spawn"):
            for token in ("MATCH-PENDING", "RULE-EXCEPTION", "identity-TODO", "name:"):
                self.assertNotIn(token, banner_of[name], (name, token))
        self.assertIn("MATCH-PENDING", banner_of["Fx_Mention"])   # mentioned, not a marker


class DirtyTreeTest(unittest.TestCase):
    def test_refuses_dirty_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x.c"
            path.write_bytes(b"/* x */\nvoid F( void )\n{\n}\n")
            with mock.patch.object(banners, "dirty_tree", return_value=[" M src/a.c"]):
                self.assertEqual(banners.main([str(path)]), 2)
            self.assertEqual(path.read_bytes(), b"/* x */\nvoid F( void )\n{\n}\n")

    def test_check_mode_reports_without_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x.c"
            path.write_bytes(b"void F( void )\n{\n}\n")
            self.assertEqual(banners.main(["--check", str(path)]), 1)
            self.assertEqual(path.read_bytes(), b"void F( void )\n{\n}\n")

    def test_write_mode_then_clean(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x.c"
            path.write_bytes(b"/* Does F. */\nvoid F( void )\n{\n}\n")
            with mock.patch.object(banners, "dirty_tree", return_value=[]):
                self.assertEqual(banners.main([str(path)]), 0)
            self.assertEqual(banners.main(["--check", str(path)]), 0)
            self.assertIn(b"F\n\nDoes F.\n", path.read_bytes())


if __name__ == "__main__":
    unittest.main()
