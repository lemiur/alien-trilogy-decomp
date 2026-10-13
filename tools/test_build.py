"""Tests of the project include directory (include/) in tools/build.py and tools/build_asm.py:
python -m unittest tools/test_build.py

The last test compiles a probe unit under DOSBox-X and is skipped when DOSBox-X is absent.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parent


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, TOOLS / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


build = load("psyq_build", "build.py")
build_asm = load("psyq_build_asm", "build_asm.py")
DOSBOX = Path(r"C:\DOSBox-X\dosbox-x.exe")


class DosNameTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def test_short_names_pass_and_hash_matches_tree_sha256(self):
        (self.root / "CAMERA.H").write_bytes(b"#define X 1\r\n")
        (self.root / "level.h").write_bytes(b"#define Y 2\r\n")
        path, digest, count = build.project_include_dir(self.root)
        self.assertEqual((digest, count), build.tree_sha256(self.root))
        self.assertEqual(count, 2)

    def test_long_name_fails_loudly(self):
        (self.root / "longheader.h").write_bytes(b"\r\n")
        with self.assertRaisesRegex(SystemExit, "8.3"):
            build.project_include_dir(self.root)

    def test_long_extension_fails(self):
        (self.root / "CAMERA.HPP4").write_bytes(b"\r\n")
        with self.assertRaisesRegex(SystemExit, "8.3"):
            build.project_include_dir(self.root)

    def test_long_directory_name_fails(self):
        (self.root / "subdirectory").mkdir()
        (self.root / "subdirectory" / "A.H").write_bytes(b"\r\n")
        with self.assertRaisesRegex(SystemExit, "8.3"):
            build.project_include_dir(self.root)

    def test_header_change_changes_tree_hash(self):
        (self.root / "A.H").write_bytes(b"#define A 1\r\n")
        before = build.project_include_dir(self.root)[1]
        (self.root / "A.H").write_bytes(b"#define A 2\r\n")
        self.assertNotEqual(before, build.project_include_dir(self.root)[1])

    def test_repository_include_dir_is_valid(self):
        path, _, count = build.project_include_dir()
        self.assertEqual(path, build.PROJECT_INCLUDE.resolve())
        self.assertGreaterEqual(count, 1)


class JobTest(unittest.TestCase):
    """The SDK keeps precedence: its -I comes before the project's, and both mount read-only."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.source = self.root / "unit.c"
        self.source.write_text("int x;\n")
        self.args = argparse.Namespace(build_root=self.root, compiler="C257", assembler="A234",
                                       opt="-O2 -G8")

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def test_c_job_search_order_and_mounts(self):
        job = build.prepare_job(self.args, self.source, self.root, self.root / "sdk", self.root / "inc")
        run = (job / "RUN.BAT").read_text()
        sdk = run.index(f"-I{build.INCLUDE_DRIVE}:\\")
        project = run.index(f"-I{build.PROJECT_INCLUDE_DRIVE}:\\")
        self.assertLess(sdk, project)
        conf = (job / "dosbox.conf").read_text()
        self.assertIn(f'mount {build.PROJECT_INCLUDE_DRIVE.lower()} "{self.root / "inc"}" -ro', conf)
        tail = next(line for line in run.splitlines() if "CPPPSX" in line).split("CPPPSX", 1)[1]
        self.assertLessEqual(len(tail.split(">")[0]), 127, "DOS command tail limit")

    def test_asm_job_search_order_and_distinct_drives(self):
        source = self.root / "unit.s"
        source.write_bytes(b"\r\n")
        job = build_asm.prepare_job(self.args, source, self.root, self.root / "sdk", self.root / "inc")
        run = (job / "RUN.BAT").read_text()
        order = [run.index(f"-I{d}:\\") for d in (build_asm.UNIT_DRIVE, build.INCLUDE_DRIVE,
                                                   build.PROJECT_INCLUDE_DRIVE)]
        self.assertEqual(order, sorted(order))
        self.assertEqual(len({build_asm.UNIT_DRIVE, build.INCLUDE_DRIVE, build.PROJECT_INCLUDE_DRIVE}), 3)
        conf = (job / "dosbox.conf").read_text()
        self.assertIn(f'mount {build.PROJECT_INCLUDE_DRIVE.lower()} "{self.root / "inc"}" -ro', conf)


@unittest.skipUnless(DOSBOX.is_file(), "DOSBox-X is not installed")
class CompileTest(unittest.TestCase):
    def test_probe_header_resolves_in_both_spellings(self):
        root = Path(tempfile.mkdtemp())
        try:
            source = root / "probe.c"
            source.write_text('#include "PROJTEST.H"\n#include <projtest.h>\n#include <sys/types.h>\n'
                              "int probe(void) { return PROJECT_INCLUDE_PROBE; }\n")
            result = subprocess.run([sys.executable, str(TOOLS / "build.py"), "--source", str(source),
                                     "--build-root", str(root / "builds")],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report["project_include_dir"]["tree_sha256"], build.project_include_dir()[1])
            listing = Path(report["assembly"]).read_text(errors="replace")
            self.assertIn("4660", listing)  # 0x1234 from include/PROJTEST.H
        finally:
            shutil.rmtree(root, ignore_errors=True)

class UnitAssemblerTest(unittest.TestCase):
    """config/asm_build.json: the per-unit assembler choice of tools/build_asm.py, on the frozen
    fixtures of tools/testdata/asm_build/ (selection tables, locks and a stand-in ASPSX.EXE)."""

    FIXTURES = TOOLS / "testdata" / "asm_build"

    def options(self, unit, config="asm_build.json"):
        return build_asm.unit_options(build.PROJECT / "asm" / "lib" / unit, self.FIXTURES / config)

    def test_selection(self):
        self.assertEqual(self.options("probe_a208.s"), {"assembler": "A208"})
        self.assertEqual(self.options("probe_gnu.s"), {"assembler": "GNU_AS"})
        self.assertEqual(self.options("probe_aspsx.s"), {"assembler": "ASPSX"})
        self.assertEqual(self.options("unlisted.s"), {"assembler": "ASPSX"})
        self.assertEqual(build_asm.unit_options(build.PROJECT / "asm" / "lib" / "x.s",
                                                self.FIXTURES / "absent.json"), {"assembler": "ASPSX"})

    def test_dos_assembler_of_each_choice(self):
        self.assertEqual(build_asm.dos_assembler({"assembler": "ASPSX"}, "A234"), "A234")
        self.assertEqual(build_asm.dos_assembler({"assembler": "A208"}, "A234"), "A208")
        self.assertIsNone(build_asm.dos_assembler({"assembler": "GNU_AS"}, "A234"))

    def test_every_entry_is_validated(self):
        # The bad entry belongs to another unit: the whole table is still refused.
        with self.assertRaisesRegex(SystemExit, "not one of"):
            self.options("probe_a208.s", "unknown_assembler.json")
        with self.assertRaisesRegex(SystemExit, "reason"):
            self.options("probe_a208.s", "missing_reason.json")
        with self.assertRaisesRegex(SystemExit, "unknown options"):
            self.options("probe_a208.s", "unknown_option.json")

    def test_a208_job_runs_the_unit_assembler(self):
        root = Path(tempfile.mkdtemp())
        try:
            source = root / "unit.s"
            source.write_bytes(b"\r\n")
            args = argparse.Namespace(build_root=root, assembler="A234")
            run = lambda options: (build_asm.prepare_job(args, source, root, root, root, options)
                                   / "RUN.BAT").read_text()
            self.assertIn(r"C:\A208\ASPSX -q INPUT.I", run({"assembler": "A208"}))
            self.assertIn(r"C:\A234\ASPSX -q INPUT.I", run({"assembler": "ASPSX"}))
            self.assertNotIn("ASPSX -q", run({"assembler": "GNU_AS"}))
        finally:
            shutil.rmtree(root, ignore_errors=True)

    def test_dos_assembler_hash_is_verified(self):
        tools = self.FIXTURES / "toolchain"
        path = build_asm.pinned_dos_assembler("A208", tools, self.FIXTURES / "lock.json")
        self.assertEqual(path, tools / "A208" / "ASPSX.EXE")
        with self.assertRaisesRegex(SystemExit, "differs from the lock"):
            build_asm.pinned_dos_assembler("A208", tools, self.FIXTURES / "lock_wrong_hash.json")
        with self.assertRaisesRegex(SystemExit, "pins no assembler"):
            build_asm.pinned_dos_assembler("A208", tools, self.FIXTURES / "lock_unpinned.json")
        with self.assertRaisesRegex(SystemExit, "no sha256"):
            build_asm.pinned_dos_assembler("A208", tools, self.FIXTURES / "lock_no_hash.json")

    def test_repository_config_and_lock(self):
        entries = build_asm.load_unit_config()  # the repository table validates
        lock = json.loads(build.LOCK.read_text(encoding="utf-8"))
        for entry in entries.values():
            if entry["assembler"] in build_asm.UNIT_DOS_ASSEMBLERS:
                name = entry["assembler"]
                self.assertIn(name, lock["assemblers"])
                self.assertEqual(lock["files"][f"toolchain/{name}/ASPSX.EXE"],
                                 lock["library_assemblers"][name]["sha256"])
        if (build.DEFAULT_TOOLS / "A208" / "ASPSX.EXE").is_file():
            build_asm.pinned_dos_assembler("A208")  # verifies the pinned copy

    def test_lock_pins_the_gnu_assembler(self):
        lock = json.loads(build.LOCK.read_text(encoding="utf-8"))
        entry = lock["gnu_as"]
        self.assertEqual(lock["files"][entry["path"]], entry["path_sha256"])
        self.assertEqual(lock["files"][entry["zip"]], entry["zip_sha256"])
        if (build.ROOT / entry["path"]).is_file():
            path, options, _ = build_asm.pinned_gnu_as()  # verifies both hashes
            self.assertIn("-march=r3000", options)


if __name__ == "__main__":
    unittest.main()
