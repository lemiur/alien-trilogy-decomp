"""Tests of the decomp-permuter glue (tools/permute_compile.py, tools/permute_setup.py):
python -m unittest tools/test_permute.py

The compile test builds a known-matching unit under DOSBox-X through the compile.sh
interface and is skipped when DOSBox-X is absent.
"""
from __future__ import annotations

from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import permute_compile as pc  # noqa: E402
import permute_setup as ps  # noqa: E402

FIXTURE = TOOLS / "testdata" / "statics"  # frozen unit: tests never read live units
UNIT, FUNCTION = "tasker", "TaskCreate"  # a function the frozen unit matches


class ElfTest(unittest.TestCase):
    def test_round_trip_and_listing(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "f.o"
            code = bytes.fromhex("e8ffbd27" "0800e003" "00000000")  # addiu sp,sp,-24; jr ra; nop
            pc.write_elf(path, "f_1234", code)
            self.assertEqual(pc.read_elf(path), ("f_1234", code))
            data = path.read_bytes()
            self.assertEqual((data[5], data[18] | data[19] << 8), (1, 8))  # LE, EM_MIPS
            lines = pc.disassemble(path).splitlines()
            self.assertIn("<f_1234>:", lines[2])
            self.assertEqual(lines[3].split("\t")[2:], ["addiu", "sp,sp,-0x18"])


class StripTest(unittest.TestCase):
    def test_keeps_target_and_what_it_needs(self):
        text = ("#include <a.h>\n/* it's a { comment */\nstruct S { int a; };\nint t[2] = { 1, 2 };\n"
                "int other(int x)\n{ return x; }\nint unused(Missing *m) { return 0; }\n"
                "void (*table[])(void) = { tabled };\nvoid tabled(void) { }\n"
                "static int helper(int x) { if (x) { return 1; } return 0; }\n"
                "static int dead(int x) { return x; }\n"
                "#define SET(p) do { \\\n    (p) = 1; \\\n} while (0)\n"
                "void keep(void) { char *s = \"}\"; other(helper(1)); }\n")
        out = ps.strip_other_functions(text, "keep")
        self.assertIn("int other(int x);", out)
        self.assertNotIn("return x;", out)
        self.assertNotIn("unused", out)
        self.assertNotIn("dead", out)
        self.assertIn("void tabled(void);", out)
        self.assertIn("static int helper(int x) { if (x)", out)
        self.assertIn('void keep(void) { char *s = "}";', out)
        for kept in ("#include <a.h>", "} while (0)\nvoid keep(void)","struct S { int a; };", "int t[2] = { 1, 2 };"):
            self.assertIn(kept, out)


@unittest.skipUnless(pc.DOSBOX.is_file() and shutil.which("bash"), "needs DOSBox-X and bash")
class CompileTest(unittest.TestCase):
    def test_compile_sh_interface_reproduces_retail_bytes(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "out.o"
            mapping = FIXTURE / f"{UNIT}.json"
            subprocess.run([sys.executable, str(TOOLS / "permute_compile.py"), "--map", str(mapping),
                            "--function", FUNCTION, "--build-root", str(Path(d) / "jobs"),
                            str(FIXTURE / f"{UNIT}.c"), "-o", str(out)], check=True)
            name, code = pc.read_elf(out)
            self.assertEqual(name, FUNCTION)
            self.assertEqual(code, pc.retail_bytes(json.loads(mapping.read_text()), FUNCTION))
            self.assertEqual(list((Path(d) / "jobs").iterdir()), [])  # job directory removed


if __name__ == "__main__":
    unittest.main()
