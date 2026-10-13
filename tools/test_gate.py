"""Tests of tools/gate.py's own logic: python -m unittest tools/test_gate.py (needs DOSBox-X)."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import re
import time
import types
import unittest

spec = importlib.util.spec_from_file_location("gate", Path(__file__).resolve().parent / "gate.py")
gate = importlib.util.module_from_spec(spec)
sys.modules["gate"] = gate  # dataclasses resolve annotations through sys.modules
spec.loader.exec_module(gate)


class GateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract, cls.raw = gate.load_contract()

    def test_base_case_reproduces_contract(self):
        linker = gate.Linker(self.contract, self.raw)
        image = linker.link([])
        self.assertEqual(hashlib.sha256(image).hexdigest(), self.contract["sha256"])
        self.assertEqual(linker.counts(), {"fallback": len(self.raw)})
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "gate"
            out.mkdir()
            (out / self.contract["binary"]).write_bytes(b"stale")
            code = gate.main(["--base-case", "--allow-dirty", "--output", str(out)])
            self.assertIn(code, (0, 2))  # 2: dirty working tree, never "GATE OK"
            self.assertEqual(hashlib.sha256((out / self.contract["binary"]).read_bytes()).hexdigest(),
                             self.contract["sha256"])

    def test_corrupted_object_is_attributed(self):
        # Frozen copy of the seeds unit from before the main merge (96e1c323), so the test
        # does not depend on how live units are later reorganised.
        fixtures = Path(__file__).resolve().parent / "testdata" / "attribution"
        entry = ("seeds", fixtures / "seeds.c", fixtures / "seeds.json")
        data_defs = {"g_idx": {"address": "0x8008cd74", "size": 2}}
        with tempfile.TemporaryDirectory() as tmp:
            unit, object_path, asm = gate.build_unit(entry, Path(tmp))
            map_spec = json.loads(entry[2].read_text())
            item = gate.unit_object(unit, object_path, map_spec, data_defs, set())
            clean = gate.Linker(self.contract, self.raw)
            clean.link([item])
            self.assertEqual(clean.attribute(self.raw), [])
            target = next(p for p in item.placements if p.symbol == "f_15324")
            item.obj.code[target.section][target.offset + 8] ^= 0x40
            broken = gate.Linker(self.contract, self.raw)
            broken.link([item])
            blame = broken.attribute(self.raw)
            self.assertEqual(len(blame), 1)
            owner, offsets = blame[0]
            self.assertEqual((owner["kind"], owner["unit"], owner["symbol"]), ("function", "seeds", "f_15324"))
            self.assertEqual(offsets, [self.contract["header_size"] + target.address + 8 - 0x80010000])

    def test_dirty_tree_refusal(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            run = lambda *a: subprocess.run(["git", "-C", tmp, *a], check=True, capture_output=True)
            run("init", "-q")
            (repo / "src").mkdir()
            (repo / "src/a.c").write_text("int a;\n")
            run("add", ".")
            run("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "x")
            self.assertEqual(gate.dirty_paths(repo), [])
            for ignored in ("builds/job/INPUT.OBJ", "reports/latest.json", "tools/__pycache__/g.pyc"):
                (repo / ignored).parent.mkdir(parents=True, exist_ok=True)
                (repo / ignored).write_text("x")
            self.assertEqual(gate.dirty_paths(repo), [])
            (repo / "src/a.c").write_text("int b;\n")
            (repo / "src/new.c").write_text("int c;\n")
            self.assertEqual(sorted(gate.dirty_paths(repo)), ["src/a.c", "src/new.c"])

    def test_empty_work_list_refused(self):
        with self.assertRaises(gate.GateError):
            gate.select_units([])

    def test_inline_asm_is_verbatim(self):
        asm = "\t.text\n\t.ent\tf_a\nf_a:\n#APP\n\tnop\n#NO_APP\n\t.end\tf_a\n\t.ent\tf_b\nf_b:\n\t.end\tf_b\n"
        self.assertEqual(gate.verbatim_symbols(asm), {"f_a"})
        self.assertEqual(gate.verbatim_symbols("\t.text\n#APP\nf_c:\n\t.word 0\n#NO_APP\n"), {"f_c"})
        self.assertEqual(gate.verbatim_symbols("\t.text\n\t.ent f_d\nf_d:\n\t.word 0\n\t.end f_d\n"), {"f_d"})
        self.assertEqual(gate.verbatim_symbols("\t.sdata\ng:\n\t.word 0\n"), set())


    def test_asm_data_items(self):
        asm = ("\t.rdata\n\t.align\t2\n$LC0:\n\t.ascii\t\"a\\\"b\\\\c\\n\\000\"\n\t.align\t3\n$L5:\n"
               "\t.word\t$L2\n\t.word\t$L3\n\t.globl\tg\n\t.data\n\t.align\t2\ng:\n\t.half\t1,2\n\t.space\t3\n"
               "\t.text\n\t.ent\tf\nf:\n\tj\t$31\n\t.end\tf\n\t.rdata\n$LC1:\n\t.byte\t7\n")
        items = gate.asm_data_items(asm)
        self.assertEqual(items[".rdata"]["size"], 17)
        self.assertEqual([(i["label"], i["offset"], i["size"]) for i in items[".rdata"]["items"]],
                         [("$LC0", 0, 7), ("$L5", 8, 8), ("$LC1", 16, 1)])
        self.assertEqual([(i["label"], i["offset"], i["size"]) for i in items[".data"]["items"]], [("g", 0, 7)])
        with self.assertRaises(gate.GateError):
            gate.asm_data_items("\t.rdata\n$LC0:\n\t.ascii\t\"\\q\"\n")

    def _switch_unit(self, tmp, withdraw):
        """actor_attack: f_2bfc0's jump table (.rdata+0x0), optionally without the unit's manual data definitions.
        Frozen copy (unit, headers, data definitions) so the test does not depend on live units."""
        fixtures = Path(__file__).resolve().parent / "testdata" / "switch"
        entry = ("actor_attack", fixtures / "actor_attack.c", fixtures / "actor_attack.json")
        unit, object_path, asm = gate.build_unit(entry, Path(tmp))
        spec = json.loads(entry[2].read_text())
        data_defs = json.loads((fixtures / "data.json").read_text())["actor_attack"]
        if withdraw:
            spec.pop("sections")
            data_defs = {k: v for k, v in data_defs.items() if k != ".rdata+0x0"}
        item = gate.unit_object(unit, object_path, spec, data_defs, set())
        return entry, item, object_path, asm.read_text(encoding="latin1")

    def test_generated_switch_table_is_placed_and_witnessed(self):
        layout = gate.section_layout.load_layout()
        with tempfile.TemporaryDirectory() as tmp:
            entry, item, object_path, asm = self._switch_unit(tmp, withdraw=True)
            placed, findings = gate.generated_placements(item, asm, self.raw, self.contract, layout)
            # the unit's other tables keep their manual definitions and are only confirmed
            self.assertEqual({f["status"] for f in findings}, {"confirmed"})
            self.assertNotIn("0x0", [f["offset"] for f in findings])
            self.assertEqual([(p.kind, p.address, p.size, p.offset) for p in placed],
                             [("generated", 0x80011cf4, 56, 0)])
            item.placements += placed
            linker = gate.Linker(self.contract, self.raw)
            linker.link([item])
            self.assertEqual(linker.attribute(self.raw), [])
            self.assertEqual(linker.counts()["generated"], 56)
            report = {"checks": [{"unit": "actor_attack", "function": n, "address": f["address"], "size": f["size"],
                                  "status": "match", "object": str(object_path), "symbol_map": str(entry[2])}
                                 for n, f in item.spec["functions"].items()], "data_checks": []}
            claims = [{**o, "label": o["symbol"]} for o in linker.owners if o["kind"] == "generated"]
            self.assertEqual(gate.generated_witness.witness(claims, report, self.raw, linker.load), (56, []))
            moved = [{**claims[0], "address": hex(0x80011cf8)}]
            witnessed, problems = gate.generated_witness.witness(moved, report, self.raw, linker.load)
            self.assertEqual(witnessed, 0)
            self.assertTrue(problems)
            # A LO16 operand of 0 makes the table's implied address 0x80010000, outside the retail
            # .rdata, so the table is left to the fallback.
            text = next(s for s, n in item.obj.sections.items() if n == ".text")
            lo = next(off for k, off, e in item.obj.patches[text] if k == 84 and gate.section_reference(e))
            code = next(p for p in item.placements if p.section == text and p.offset <= lo < p.offset + p.size)
            site = self.contract["header_size"] + code.address + lo - code.offset - 0x80010000
            bent = bytearray(self.raw)
            struct.pack_into("<H", bent, site, 0)
            item.placements = [p for p in item.placements if p.kind != "generated"]
            placed, findings = gate.generated_placements(item, asm, bytes(bent), self.contract, layout)
            self.assertEqual(placed, [])
            table = [f for f in findings if f["offset"] == "0x0"]
            self.assertEqual([f["status"] for f in table], ["outside_section"])
            self.assertIn("0x80010000", table[0]["reason"])

    def test_manual_switch_definition_is_confirmed(self):
        layout = gate.section_layout.load_layout()
        with tempfile.TemporaryDirectory() as tmp:
            _, item, _, asm = self._switch_unit(tmp, withdraw=False)
            placed, findings = gate.generated_placements(item, asm, self.raw, self.contract, layout)
            self.assertEqual(placed, [])
            self.assertEqual({f["status"] for f in findings}, {"confirmed"})
            self.assertIn(".rdata+0x0", [f["symbol"] for f in findings])
            next(p for p in item.placements if p.symbol == ".rdata+0x0").address += 4
            _, findings = gate.generated_placements(item, asm, self.raw, self.contract, layout)
            self.assertEqual([f["status"] for f in findings if f["offset"] == "0x0"], ["conflict"])

    # Data-only units whose initializers use string literals: retail g_difficulty_choices
    # (0x8006c1d8) points at "Acid Reign {", "Raging Terror {{" and "Xenomania {{{".
    DIFFICULTY = 'const char *const g_difficulty_choices[] = {\n    "Acid Reign {", "Raging Terror {{", "Xenomania {{{"\n};\n'
    POOL = [(0x80011384, 14), (0x80011394, 17), (0x800113a8, 13)]

    def _literal_unit(self, tmp, source, defs, unit="lit"):
        tmp = Path(tmp) / unit
        tmp.mkdir()
        (tmp / "lit.c").write_text(source)
        spec = {"gp": "0x8008cd74", "functions": {}, "symbols": {}}
        mapping = tmp / "lit.json"
        mapping.write_text(json.dumps(spec))
        (tmp / "data.json").write_text(json.dumps({unit: defs}))
        (tmp / "objects").mkdir()
        _, object_path, asm = gate.build_unit((unit, tmp / "lit.c", mapping), tmp / "objects")
        item = gate.unit_object(unit, object_path, spec, defs, set())
        return item, object_path, mapping, asm.read_text(encoding="latin1")

    def _data_check(self, tmp, object_path, mapping, raw):
        tmp, unit = mapping.parent, mapping.parent.name
        reference, out = tmp / "reference.exe", tmp / "data_check.json"
        reference.write_bytes(raw)
        subprocess.run([sys.executable, str(Path(gate.TOOLS) / "data_check.py"), "--object", str(object_path),
                        "--unit", unit, "--symbol-map", str(mapping), "--map", str(tmp / "data.json"),
                        "--reference", str(reference), "--json-report", str(out)], capture_output=True)
        return {**json.loads(out.read_text()), "object": str(object_path), "map_snapshot": str(mapping)}

    def test_compiler_local_labels_are_per_unit(self):
        # Two units each emit $LC0: "Acid Reign {" (0x800113a8, through g_difficulty_choices)
        # and "Track 10" (0x800113dc, through g_track_names[9] at 0x8006c208).
        layout = gate.section_layout.load_layout()
        with tempfile.TemporaryDirectory() as tmp:
            units, checks = [], []
            for unit, source, defs in (
                    ("lit_a", self.DIFFICULTY, {"g_difficulty_choices": {"address": "0x8006c1d8", "size": 12}}),
                    ("lit_b", 'const char *const t[] = { "Track 10" };\n', {"t": {"address": "0x8006c208", "size": 4}})):
                item, object_path, mapping, asm = self._literal_unit(tmp, source, defs, unit)
                placed, findings = gate.generated_placements(item, asm, self.raw, self.contract, layout)
                self.assertEqual(findings, [])
                item.placements += placed
                units.append(item)
                checks.append(self._data_check(tmp, object_path, mapping, self.raw))
            local = {(p.unit, p.symbol, p.address) for i in units for p in i.placements if p.kind == "generated"}
            self.assertIn(("lit_a", "$LC0", 0x800113a8), local)
            self.assertIn(("lit_b", "$LC0", 0x800113dc), local)
            linker = gate.Linker(self.contract, self.raw)
            linker.link(units)
            self.assertNotIn("$LC0", linker.symbols)
            self.assertEqual(linker.attribute(self.raw), [])
            self.assertEqual(linker.counts()["generated"], 44 + 9)
            self.assertEqual([c["status"] for c in checks], ["match", "match"])
            report = {"checks": [], "data_checks": checks}
            claims = [{**o, "label": o["symbol"]} for o in linker.owners if o["kind"] == "generated"]
            self.assertEqual(gate.generated_witness.witness(claims, report, self.raw, linker.load), (53, []))

    def test_data_literal_pool_is_placed_and_witnessed(self):
        layout = gate.section_layout.load_layout()
        defs = {"g_difficulty_choices": {"address": "0x8006c1d8", "size": 12}}
        with tempfile.TemporaryDirectory() as tmp:
            item, object_path, mapping, asm = self._literal_unit(tmp, self.DIFFICULTY, defs)
            placed, findings = gate.generated_placements(item, asm, self.raw, self.contract, layout)
            self.assertEqual(findings, [])
            self.assertEqual(sorted((p.kind, p.address, p.size) for p in placed),
                             [("generated", a, n) for a, n in self.POOL])
            item.placements += placed
            linker = gate.Linker(self.contract, self.raw)
            linker.link([item])
            self.assertEqual(linker.attribute(self.raw), [])
            self.assertEqual((linker.counts()["generated"], linker.counts()["data"]), (44, 12))
            check = self._data_check(tmp, object_path, mapping, self.raw)
            self.assertEqual(check["status"], "match", check.get("error"))
            self.assertEqual([r["address"] for r in check["definitions"][0]["applied_relocations"]],
                             ["0x800113a8", "0x80011394", "0x80011384"])
            report = {"checks": [], "data_checks": [check]}
            claims = [{**o, "label": o["symbol"]} for o in linker.owners if o["kind"] == "generated"]
            self.assertEqual(gate.generated_witness.witness(claims, report, self.raw, linker.load), (44, []))

    def test_data_literal_wrong_anchor_is_rejected(self):
        layout = gate.section_layout.load_layout()
        defs = {"g_difficulty_choices": {"address": "0x8006c1d8", "size": 12}}
        word = self.contract["header_size"] + 0x8006c1d8 - 0x80010000  # retail pointer to "Acid Reign {"
        with tempfile.TemporaryDirectory() as tmp:
            item, object_path, mapping, asm = self._literal_unit(tmp, self.DIFFICULTY, defs)
            check = self._data_check(tmp, object_path, mapping, self.raw)
            report = {"checks": [], "data_checks": [check]}
            placed, _ = gate.generated_placements(item, asm, self.raw, self.contract, layout)
            claims = [{"unit": "lit", "label": p.symbol, "section": ".rdata", "offset": p.offset,
                       "size": p.size, "address": hex(p.address)} for p in placed]
            # The witness: a word that does not hold the claimed address rejects the claim
            # even though the claimed bytes match.
            bent = bytearray(self.raw)
            struct.pack_into("<I", bent, word, 0x800113ac)
            witnessed, problems = gate.generated_witness.witness(claims, report, bytes(bent), 0x80010000)
            self.assertEqual(witnessed, 44 - 13)
            self.assertTrue(any("holds 0x800113ac" in p for p in problems), problems)
            # data_check: the anchored item's bytes are not at the implied address.
            check = self._data_check(tmp, object_path, mapping, bytes(bent))
            self.assertEqual(check["status"], "error")
            self.assertIn("object bytes differ from the reference", check["error"])
            # The gate: a word pointing outside the retail .rdata leaves the item to the fallback.
            struct.pack_into("<I", bent, word, 0x80010000)
            placed, findings = gate.generated_placements(item, asm, bytes(bent), self.contract, layout)
            self.assertEqual(sorted(p.address for p in placed), [0x80011384, 0x80011394])
            self.assertEqual([f["status"] for f in findings], ["outside_section"])

    def test_data_literal_ambiguous_anchor_stays_in_fallback(self):
        # One literal shared by two one-entry tables whose retail words point at different strings.
        layout = gate.section_layout.load_layout()
        source = 'const char *const a[] = { "Acid Reign {" };\nconst char *const b[] = { "Acid Reign {" };\n'
        defs = {"a": {"address": "0x8006c1d8", "size": 4}, "b": {"address": "0x8006c1dc", "size": 4}}
        with tempfile.TemporaryDirectory() as tmp:
            item, object_path, mapping, asm = self._literal_unit(tmp, source, defs)
            placed, findings = gate.generated_placements(item, asm, self.raw, self.contract, layout)
            self.assertEqual(placed, [])
            self.assertEqual([f["status"] for f in findings], ["ambiguous"])
            self.assertIn("0x80011394", findings[0]["reason"])
            self.assertIn("0x800113a8", findings[0]["reason"])
            with self.assertRaises(gate.GateError):  # the tables cannot be linked against a guess
                gate.Linker(self.contract, self.raw).link([item])
            check = self._data_check(tmp, object_path, mapping, self.raw)
            self.assertEqual(check["status"], "error")
            self.assertIn("several addresses", check["error"])

    def test_section_layout_matches_evidence(self):
        layout, bss = gate.section_layout.derive()
        self.assertEqual(layout, gate.section_layout.load_layout())
        self.assertEqual(bss, json.loads(gate.section_layout.BSS_CONFIG.read_text()))
        self.assertEqual(gate.section_layout.validate(layout, bss), [])
        self.assertEqual(gate.section_layout.section_of(layout, 0x8001380c), ".text")
        self.assertEqual(gate.section_layout.section_of(layout, 0x8006a580), ".data")  # .text/.data pinned at 0x8006a578
        ev = gate.section_layout.Evidence()
        bent = bytearray(ev.raw)
        struct.pack_into("<H", bent, 0x800 + 0x800588c8 - 0x80010000, 0xd080)  # LO16 of sectstart(.sbss)
        ev.raw = bytes(bent)
        with self.assertRaises(gate.section_layout.LayoutError):
            gate.section_layout.derive(ev)  # .sbss would now start inside the library .sdata


    def test_failed_run_reaps_children_and_output_is_deletable(self):
        entries = list(gate.verify.UNITS[:2])  # any two registered units: the build is replaced
        self.assertEqual(len(entries), 2)
        for failure in (gate.GateError("boom"), KeyboardInterrupt()):
            with tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / "gate"
                started = []
                real_start = gate.CHILDREN.start

                def spy(argv, **kw):
                    proc = real_start(argv, **kw)
                    started.append(proc)
                    return proc

                def failing_build(entry, root, children=None):
                    while not any("verify.py" in str(p.args) for p in started):
                        time.sleep(0.05)
                    raise failure

                gate.CHILDREN.start = spy
                real_build, gate.build_unit = gate.build_unit, failing_build
                try:
                    argv = ["--allow-dirty", "--output", str(out), "--jobs", "2",
                            "--units", *[e[0] for e in entries]]
                    if isinstance(failure, KeyboardInterrupt):
                        with self.assertRaises(KeyboardInterrupt):
                            gate.main(argv)
                    else:
                        self.assertEqual(gate.main(argv), 1)
                finally:
                    gate.CHILDREN.start = real_start
                    gate.build_unit = real_build
                self.assertTrue(started)
                self.assertTrue(all(p.poll() is not None for p in started), "a child outlived the gate")
                shutil.rmtree(out)  # the verify.log handle and builds are released
                self.assertFalse(out.exists())


@unittest.skipUnless(sys.platform == "win32", "fake DOSBox-X is a batch file")
class BuildCrashTest(unittest.TestCase):
    """build.py: retry DOSBox-X crashes only; never retry or mask a compiler error."""

    BUILD = Path(__file__).resolve().parent / "build.py"
    REAL = r"C:\DOSBox-X\dosbox-x.exe"

    def run_build(self, tmp, script):
        tmp = Path(tmp)
        fake = tmp / "fake.bat"
        fake.write_text("@echo off\r\n" + script, encoding="ascii", newline="")
        src = tmp / "a.c"
        src.write_text("int f(int a) { return a + 1; }\n")
        env = dict(os.environ, FAKE_STATE=str(tmp), REAL_DOSBOX=self.REAL)
        result = subprocess.run([sys.executable, str(self.BUILD), "--source", str(src), "--dosbox", str(fake),
                                 "--build-root", str(tmp / "b")], capture_output=True, text=True, env=env)
        return result, list((tmp / "b").iterdir()) if (tmp / "b").exists() else []

    def test_crash_is_retried_twice_then_reported_as_crash(self):
        with tempfile.TemporaryDirectory() as tmp:
            result, jobs = self.run_build(tmp, "exit /b 3221226505\r\n")
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stderr.count("DOSBox-X crashed"), 2)
            self.assertIn("toolchain crashed at MISSING", result.stderr)
            self.assertEqual(len(jobs), 1)  # crashed attempts leave no job directories behind

    def test_compile_error_is_never_retried(self):
        with tempfile.TemporaryDirectory() as tmp:
            result, _ = self.run_build(tmp, "echo ERROR:CC1>STATUS.TXT\r\nexit /b 0\r\n")
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("crashed", result.stderr)
            self.assertIn("failed at ERROR:CC1", result.stderr)

    def test_error_status_with_abnormal_exit_is_still_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            result, _ = self.run_build(tmp, "echo ERROR:AS>STATUS.TXT\r\nexit /b 3221226505\r\n")
            self.assertEqual(result.stderr.count("DOSBox-X crashed"), 0)
            self.assertIn("failed at ERROR:AS", result.stderr)

    def test_crash_then_success_uses_a_fresh_job(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = ('if exist "%FAKE_STATE%\\seen" goto real\r\n'
                      'echo x> "%FAKE_STATE%\\seen"\r\nexit /b 3221226505\r\n'
                      ':real\r\n"%REAL_DOSBOX%" %*\r\nexit /b %errorlevel%\r\n')
            result, jobs = self.run_build(tmp, script)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr.count("DOSBox-X crashed"), 1)
            self.assertEqual(len(jobs), 1)
            self.assertTrue(json.loads(result.stdout)["object"].endswith("INPUT.OBJ"))

class DataExtentTest(unittest.TestCase):
    """One extent rule in gate.py and data_check.py (tools/data_extent.py): an item is the bytes
    its directives emit, the .align padding after it belongs to no item, and an unnamed item is
    keyed by its position. Each case runs through both tools, and the witness where it applies."""

    PROJECT = Path(__file__).resolve().parents[1]
    # The password TU: f_471d0, f_47404 and f_47584 with their jump tables at 0x800129e0 (32 bytes),
    # 0x80012a00 and 0x80012a18 (20 bytes each, each followed by 4 bytes of .align 3 fill).
    PASSWORD_SOURCES = ("fresh_r1_06", "game_40000_3", "game_40000_6")
    # Frozen copies of the password units as they were before the save_password merge (a217290),
    # so the test does not depend on how live units are later reorganised.
    FIXTURES = Path(__file__).resolve().parent / "testdata" / "password"
    TABLES = {".rdata+0x0": (0x800129e0, 32), ".rdata+0x20": (0x80012a00, 20), ".rdata+0x38": (0x80012a18, 20)}

    @classmethod
    def setUpClass(cls):
        cls.contract, cls.raw = gate.load_contract()
        cls.layout = gate.section_layout.load_layout()

    def _unit(self, tmp, unit, source, spec):
        root = Path(tmp) / unit
        root.mkdir()
        (root / f"{unit}.c").write_text(source)
        mapping = root / f"{unit}.json"
        mapping.write_text(json.dumps(spec))
        (root / "objects").mkdir()
        _, object_path, asm = gate.build_unit((unit, root / f"{unit}.c", mapping), root / "objects")
        return root, object_path, mapping, asm.read_text(encoding="latin1")

    def _data_check(self, root, unit, object_path, mapping, defs):
        (root / "data.json").write_text(json.dumps({unit: defs}))
        reference, out = root / "reference.exe", root / "data_check.json"
        if not reference.exists():
            reference.write_bytes(self.raw)
        subprocess.run([sys.executable, str(Path(gate.TOOLS) / "data_check.py"), "--object", str(object_path),
                        "--unit", unit, "--symbol-map", str(mapping), "--map", str(root / "data.json"),
                        "--reference", str(reference), "--json-report", str(out)], capture_output=True)
        return json.loads(out.read_text())

    def _gate(self, unit, object_path, spec, defs, asm):
        """(unit object with its generated placements added, findings), as the gate derives them."""
        item = gate.unit_object(unit, object_path, spec, defs, set())
        placed, findings = gate.generated_placements(item, asm, self.raw, self.contract, self.layout)
        item.placements += placed
        return item, findings

    def _password_unit(self, tmp):
        source = "".join((self.FIXTURES / f"{name}.c").read_text() for name in self.PASSWORD_SOURCES)
        spec = {"functions": {}, "symbols": {}, "gp": "0x8008cd74"}
        for name in self.PASSWORD_SOURCES:
            part = json.loads((self.FIXTURES / f"{name}.json").read_text())
            spec["functions"].update(part["functions"])
            spec["symbols"].update(part.get("symbols", {}))
        for name in spec["functions"]:
            spec["symbols"].pop(name, None)
        spec["sections"] = {".rdata": "0x800129e0"}
        return spec, self._unit(tmp, "password", source, spec)

    def _table_defs(self):
        return {key: {"address": hex(address), "size": size} for key, (address, size) in self.TABLES.items()}

    @staticmethod
    def _report(spec, object_path, mapping):
        return {"checks": [{"unit": "password", "function": n, "address": f["address"], "size": f["size"],
                            "status": "match", "object": str(object_path), "symbol_map": str(mapping)}
                           for n, f in spec["functions"].items()], "data_checks": []}

    def test_two_anonymous_jump_tables_in_one_unit(self):
        with tempfile.TemporaryDirectory() as tmp:
            spec, (root, object_path, mapping, asm) = self._password_unit(tmp)
            items = gate.asm_data_items(asm)[".rdata"]["items"]
            self.assertEqual([(i["offset"], i["size"]) for i in items], [(0, 32), (0x20, 20), (0x38, 20)])
            self.assertTrue(all(i["label"].startswith("$L") for i in items))  # compiler labels only
            # Placed by data.json entries keyed by position: both tools agree.
            defs = self._table_defs()
            check = self._data_check(root, "password", object_path, mapping, defs)
            self.assertEqual(check["status"], "match", check.get("error"))
            self.assertEqual([(d["symbol"], d["size"]) for d in check["definitions"]],
                             [(k, n) for k, (_, n) in self.TABLES.items()])
            item, findings = self._gate("password", object_path, spec, defs, asm)
            self.assertEqual(sorted((f["status"], f["symbol"]) for f in findings),
                             sorted(("confirmed", k) for k in self.TABLES))
            linker = gate.Linker(self.contract, self.raw)
            linker.link([item])
            self.assertEqual(linker.attribute(self.raw), [])
            self.assertEqual(linker.counts()["data"], 72)
            for fill in (0x80012a14, 0x80012a2c):  # .align fill belongs to no item: fallback bytes
                at = self.contract["header_size"] + fill - linker.load
                self.assertEqual({linker.provenance[i] for i in range(at, at + 4)}, {1})
            # Placed by relocation anchors alone: the gate generates each table, the witness agrees.
            spec.pop("sections")
            item, findings = self._gate("password", object_path, spec, {}, asm)
            self.assertEqual(findings, [])
            self.assertEqual(sorted((p.address, p.size) for p in item.placements if p.kind == "generated"),
                             sorted(self.TABLES.values()))
            linker = gate.Linker(self.contract, self.raw)
            linker.link([item])
            self.assertEqual(linker.attribute(self.raw), [])
            claims = [{**o, "label": o["symbol"]} for o in linker.owners if o["kind"] == "generated"]
            self.assertEqual(gate.generated_witness.witness(claims, self._report(spec, object_path, mapping),
                                                            self.raw, linker.load), (72, []))

    def test_short_with_padding_after_it(self):
        # g_8d058 (game_40000_2): a short, then .align 2 fill, then g_8d05c.
        source = "short g_8d058 = -1;\nint g_8d05c = 0;\n"
        spec = {"functions": {}, "symbols": {}, "gp": "0x8008cd74"}
        defs = {"g_8d058": {"address": "0x8008d058", "size": 2}, "g_8d05c": {"address": "0x8008d05c", "size": 4}}
        with tempfile.TemporaryDirectory() as tmp:
            root, object_path, mapping, asm = self._unit(tmp, "short", source, spec)
            self.assertEqual([(i["label"], i["offset"], i["size"])
                              for i in gate.asm_data_items(asm)[".sdata"]["items"]],
                             [("g_8d058", 0, 2), ("g_8d05c", 4, 4)])
            check = self._data_check(root, "short", object_path, mapping, defs)
            self.assertEqual(check["status"], "match", check.get("error"))
            item, findings = self._gate("short", object_path, spec, defs, asm)
            self.assertEqual(findings, [])
            linker = gate.Linker(self.contract, self.raw)
            linker.link([item])
            self.assertEqual(linker.attribute(self.raw), [])
            self.assertEqual(linker.counts()["data"], 6)
            at = self.contract["header_size"] + 0x8008d05a - linker.load
            self.assertEqual((linker.provenance[at], linker.provenance[at + 1]), (1, 1))
            # Claiming the padding is rejected by both tools.
            wide = {**defs, "g_8d058": {"address": "0x8008d058", "size": 4}}
            check = self._data_check(root, "short", object_path, mapping, wide)
            self.assertEqual(check["status"], "error")
            self.assertIn("item extent 2 differs from expected 4", check["error"])
            _, findings = self._gate("short", object_path, spec, wide, asm)
            self.assertEqual([(f["status"], f["label"]) for f in findings], [("extent", "g_8d058")])

    def test_misplaced_table_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            spec, (root, object_path, mapping, asm) = self._password_unit(tmp)
            defs = self._table_defs()
            # At the wrong address: the retail bytes differ, and the code's relocations imply 0x80012a00.
            moved = {**defs, ".rdata+0x20": {"address": "0x80012a04", "size": 20}}
            check = self._data_check(root, "password", object_path, mapping, moved)
            self.assertNotEqual(check["status"], "match")
            _, findings = self._gate("password", object_path, spec, moved, asm)
            conflicts = [f for f in findings if f["status"] != "confirmed"]
            self.assertEqual([(f["status"], f["offset"]) for f in conflicts], [("conflict", "0x20")])
            self.assertIn("imply ['0x80012a00'] but .rdata+0x20 is placed at 0x80012a04", conflicts[0]["reason"])
            # At an offset where no item starts (the .align fill after the second table).
            fill = {**defs, ".rdata+0x34": {"address": "0x80012a14", "size": 4}}
            check = self._data_check(root, "password", object_path, mapping, fill)
            self.assertEqual(check["status"], "error")
            self.assertIn("no assembly item starts at .rdata+0x34", check["error"])
            _, findings = self._gate("password", object_path, spec, fill, asm)
            self.assertIn(("extent", ".rdata+0x34"), [(f["status"], f.get("label")) for f in findings])
            # Swallowing the fill after it.
            padded = {**defs, ".rdata+0x20": {"address": "0x80012a00", "size": 24}}
            check = self._data_check(root, "password", object_path, mapping, padded)
            self.assertIn("item extent 20 differs from expected 24", check.get("error", ""))
            _, findings = self._gate("password", object_path, spec, padded, asm)
            self.assertIn(("extent", ".rdata+0x20"), [(f["status"], f.get("label")) for f in findings])
            # An invented name for an unnamed table is refused by both tools.
            named = {"switch_table_f_47404": {"section": ".rdata", "offset": 0x20,
                                              "address": "0x80012a00", "size": 20}}
            check = self._data_check(root, "password", object_path, mapping, named)
            self.assertIn("keyed by its position", check.get("error", ""))
            with self.assertRaises(gate.GateError):
                gate.unit_object("password", object_path, spec, named, set())
            # The witness rejects a generated claim moved off its anchors or into the fill.
            spec.pop("sections")
            item, _ = self._gate("password", object_path, spec, {}, asm)
            linker = gate.Linker(self.contract, self.raw)
            linker.link([item])
            claims = [{**o, "label": o["symbol"]} for o in linker.owners if o["kind"] == "generated"]
            second = next(c for c in claims if c["address"] == "0x80012a00")
            report = self._report(spec, object_path, mapping)
            for bad in ({**second, "address": "0x80012a04"},
                        {**second, "offset": "0x34", "size": 4, "address": "0x80012a14"}):
                witnessed, problems = gate.generated_witness.witness([bad], report, self.raw, linker.load)
                self.assertEqual(witnessed, 0)
                self.assertTrue(problems)

    # f_18820 (game_10000_5) copies "Delete " and "Accept " with an inline strcpy that reads
    # each 8-byte literal as two gp-relative words ($LCn and $LCn+4), and "Quit " as a word
    # and a half ($LC2+4). Its .sdata literals sit at 0x8008cf5c..0x8008cf7a.
    LITERALS = {".sdata+0x0": (0x8008cf5c, 8), ".sdata+0x8": (0x8008cf64, 8), ".sdata+0x10": (0x8008cf6c, 6),
                ".sdata+0x18": (0x8008cf74, 2), ".sdata+0x1c": (0x8008cf78, 2)}

    def _keyboard_unit(self, tmp):
        # Frozen copy of f_18820's unit from before the frontend merge (f2926fd0), so the test
        # does not depend on how live units are later reorganised.
        fixtures = Path(__file__).resolve().parent / "testdata" / "keyboard"
        entry = ("game_10000_5", fixtures / "game_10000_5.c", fixtures / "game_10000_5.json")
        root = Path(tmp)
        (root / "objects").mkdir()
        unit, object_path, asm = gate.build_unit(entry, root / "objects")
        spec = json.loads(entry[2].read_text())
        return root, spec, object_path, entry[2], asm.read_text(encoding="latin1")

    @staticmethod
    def _section_targets(obj, sid):
        return {ref[1] for patches in obj.patches.values() for kind, _, expr in patches
                for ref in [gate.section_reference(expr[2] if kind == 30 and expr[0] == 46 else expr)]
                if ref and ref[0] == sid}

    def test_interior_literal_reference_is_one_item(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, spec, object_path, mapping, asm = self._keyboard_unit(tmp)
            item = gate.unit_object("game_10000_5", object_path, spec, {}, set())
            sdata = next(s for s, n in item.obj.sections.items() if n == ".sdata")
            self.assertTrue({0x4, 0xc, 0x14} <= self._section_targets(item.obj, sdata))  # $LCn+4
            # Gate: one generated item per literal; the +4 operands imply the same addresses.
            placed, findings = gate.generated_placements(item, asm, self.raw, self.contract, self.layout)
            self.assertEqual(findings, [])
            self.assertEqual(sorted((p.address, p.size) for p in placed), sorted(self.LITERALS.values()))
            item.placements += placed
            linker = gate.Linker(self.contract, self.raw)
            linker.link([item])
            self.assertEqual(linker.attribute(self.raw), [])
            self.assertEqual(linker.counts()["generated"], 26)
            # Witness: the +4 targets are interior references, not item boundaries.
            report = {"checks": [{"unit": "game_10000_5", "function": n, "address": f["address"],
                                  "size": f["size"], "status": "match", "object": str(object_path),
                                  "symbol_map": str(mapping)} for n, f in spec["functions"].items()],
                      "data_checks": []}
            claims = [{**o, "label": o["symbol"]} for o in linker.owners if o["kind"] == "generated"]
            self.assertEqual(gate.generated_witness.witness(claims, report, self.raw, linker.load), (26, []))
            # data_check and the gate size each literal as one whole item when data.json places it.
            defs = {key: {"address": hex(a), "size": n} for key, (a, n) in self.LITERALS.items()}
            check = self._data_check(root, "game_10000_5", object_path, mapping, defs)
            self.assertEqual(check["status"], "match", check.get("error"))
            _, findings = self._gate("game_10000_5", object_path, spec, defs, asm)
            self.assertEqual(sorted((f["status"], f["symbol"]) for f in findings),
                             sorted(("confirmed", k) for k in self.LITERALS))

    def test_interior_data_word_into_literal(self):
        # A data word at a literal's +4 resolves through that literal (reference bent so that
        # g_difficulty_choices[0..1] point at "Acid Reign {" and 4 bytes into it).
        source = 'const char *const t[] = { "Acid Reign {", "Acid Reign {" + 4 };\n'
        defs = {"t": {"address": "0x8006c1d8", "size": 8}}
        spec = {"functions": {}, "symbols": {}, "gp": "0x8008cd74"}
        bent = bytearray(self.raw)
        at = self.contract["header_size"] + 0x8006c1d8 - 0x80010000
        struct.pack_into("<II", bent, at, 0x800113a8, 0x800113ac)
        bent = bytes(bent)
        with tempfile.TemporaryDirectory() as tmp:
            root, object_path, mapping, asm = self._unit(tmp, "interior", source, spec)
            (root / "reference.exe").write_bytes(bent)
            check = self._data_check(root, "interior", object_path, mapping, defs)
            self.assertEqual(check["status"], "match", check.get("error"))
            self.assertEqual([r["address"] for r in check["definitions"][0]["applied_relocations"]],
                             ["0x800113a8", "0x800113ac"])
            item = gate.unit_object("interior", object_path, spec, defs, set())
            placed, findings = gate.generated_placements(item, asm, bent, self.contract, self.layout)
            self.assertEqual([f["status"] for f in findings], [])
            self.assertEqual([(p.address, p.size) for p in placed], [(0x800113a8, 13)])
            item.placements += placed
            linker = gate.Linker(self.contract, bent)
            linker.link([item])
            self.assertEqual(linker.attribute(bent), [])
            report = {"checks": [], "data_checks": [{**check, "object": str(object_path),
                                                      "map_snapshot": str(mapping)}]}
            claims = [{**o, "label": o["symbol"]} for o in linker.owners if o["kind"] == "generated"]
            self.assertEqual(gate.generated_witness.witness(claims, report, bent, linker.load), (13, []))

    def test_claim_over_the_next_item_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, spec, object_path, mapping, asm = self._keyboard_unit(tmp)
            # "Quit " widened over "[" (its fill and the next literal): both listing-based tools refuse it.
            wide = {".sdata+0x10": {"address": "0x8008cf6c", "size": 12}}
            check = self._data_check(root, "game_10000_5", object_path, mapping, wide)
            self.assertEqual(check["status"], "error")
            self.assertIn("item extent 6 differs from expected 12", check["error"])
            _, findings = self._gate("game_10000_5", object_path, spec, wide, asm)
            self.assertIn(("extent", ".sdata+0x10"), [(f["status"], f.get("label")) for f in findings])
            # The witness refuses it next to the gate's own claim of "[".
            item, _ = self._gate("game_10000_5", object_path, spec, {}, asm)
            linker = gate.Linker(self.contract, self.raw)
            linker.link([item])
            claims = [{**o, "label": o["symbol"]} for o in linker.owners if o["kind"] == "generated"]
            claims = [{**c, "size": 12} if c["offset"] == "0x10" else c for c in claims]
            report = {"checks": [{"unit": "game_10000_5", "function": n, "address": f["address"],
                                  "size": f["size"], "status": "match", "object": str(object_path),
                                  "symbol_map": str(mapping)} for n, f in spec["functions"].items()],
                      "data_checks": []}
            _, problems = gate.generated_witness.witness(claims, report, self.raw, linker.load)
            self.assertTrue(any("overlap" in p for p in problems), problems)
            # ... and over a label the object keeps, by its own sizer.
            obj = gate.generated_witness.psyqobj.load(str(object_path))[0]
            sdata = next(s for s, n in obj.sections.items() if n == ".sdata")
            self.assertIsNone(gate.generated_witness._object_extent(obj, sdata, 0x10, 6))
            obj.xdefs.append(("g_next", sdata, 0x18))
            self.assertEqual(gate.generated_witness._object_extent(obj, sdata, 0x10, 12),
                             "claim runs past the next item boundary at +0x18")
            # The documented blind spot: without that label or the gate's claim of "[", a merge of
            # two unlabelled items is indistinguishable from one item with an interior reference.
            obj.xdefs.pop()
            self.assertIsNone(gate.generated_witness._object_extent(obj, sdata, 0x10, 12))

    def test_gp_relative_literals_need_no_section_base(self):
        # f_18820 reads its literals through $gp. With no .sdata base in the map, or one that is
        # wrong for them (a unit whose .sdata has holes), the gate and bytecheck.py both resolve
        # every GP-relative operand through the placed literal and agree with retail.
        with tempfile.TemporaryDirectory() as tmp:
            root, spec, object_path, _, asm = self._keyboard_unit(tmp)
            (root / "reference.exe").write_bytes(self.raw)
            (root / "INPUT.S").write_text(asm, encoding="latin1")
            for base in (None, "0x8008cf00"):
                bent = {**spec, "sections": {".sdata": base}} if base else {k: v for k, v in spec.items()
                                                                            if k != "sections"}
                mapping = root / "map.json"
                mapping.write_text(json.dumps(bent))
                item, findings = self._gate("game_10000_5", object_path, bent, {}, asm)
                self.assertEqual(findings, [])
                linker = gate.Linker(self.contract, self.raw)
                linker.link([item])
                self.assertEqual(linker.attribute(self.raw), [])
                out = root / "f_18820.json"
                subprocess.run([sys.executable, str(Path(gate.TOOLS) / "bytecheck.py"), "--object", str(object_path),
                                "--symbol-map", str(mapping), "--reference", str(root / "reference.exe"),
                                "--asm", str(root / "INPUT.S"), "--function", "f_18820",
                                "--json-report", str(out)], capture_output=True)
                check = json.loads(out.read_text())
                self.assertEqual(check["status"], "match", check.get("error"))


class AsmUnitTest(unittest.TestCase):
    """Hand-written assembly units (tools/asm_units.py): linked as kind "asm", counted apart from
    source bytes, confined to the GTE/renderer region, and attributed when a byte is wrong."""

    @classmethod
    def setUpClass(cls):
        cls.contract, cls.raw = gate.load_contract()
        cls.entry = next(e for e in gate.verify.ASM_UNITS if e[0] == "ot_depth")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.unit, cls.object_path, cls.listing = gate.build_unit(cls.entry, Path(cls.tmp.name))
        cls.spec = json.loads(cls.entry[2].read_text())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def item(self, spec=None):
        return gate.asm_unit_object(self.unit, self.object_path, spec or self.spec, {},
                                    self.listing.read_text(encoding="latin1"))

    def test_asm_unit_links_and_is_counted_as_asm(self):
        item = self.item()
        self.assertEqual({p.kind for p in item.placements}, {"asm"})
        linker = gate.Linker(self.contract, self.raw)
        linker.link([item])
        self.assertEqual(linker.attribute(self.raw), [])
        counts = linker.counts()
        self.assertEqual(counts["asm"], 56 + 76)
        self.assertNotIn("function", counts)
        report = {"checks": [{"unit": "ot_depth", "kind": "asm", "function": n, "address": f["address"],
                              "size": f["size"], "status": "match"} for n, f in self.spec["functions"].items()],
                  "data_checks": [], "byte_matched_asm_bytes": 132}
        specs = {"ot_depth": self.spec}
        self.assertEqual(gate.verify.asm_units.cross_check(linker.owners, report, specs, ["ot_depth"]), [])
        # verify.py counting the routine as C, or not at all, is a disagreement.
        as_c = {**report, "checks": [{**c, "kind": "c"} for c in report["checks"]], "byte_matched_asm_bytes": 0}
        self.assertTrue(gate.verify.asm_units.cross_check(linker.owners, as_c, specs, ["ot_depth"]))

    def test_out_of_range_asm_unit_is_refused(self):
        lo, hi = gate.verify.ASM_REGION
        moved = json.loads(json.dumps(self.spec))
        moved["functions"]["ot_index_tri"]["address"] = hex(lo - 4)  # one word below the region
        with self.assertRaises(gate.GateError) as caught:
            self.item(moved)
        self.assertIn("outside the hand-written assembly region", str(caught.exception))
        # The registry check refuses the same map before anything is built ...
        with tempfile.TemporaryDirectory() as tmp:
            mapping = Path(tmp) / "ot_depth.json"
            mapping.write_text(json.dumps(moved))
            with self.assertRaises(gate.verify.asm_units.AsmPolicyError):
                gate.verify.asm_units.validate_registry([], [("ot_depth", self.entry[1], mapping)], {})
            # ... and a C source registered as an asm unit is refused too.
            with self.assertRaises(gate.verify.asm_units.AsmPolicyError):
                gate.verify.asm_units.validate_registry([], [("x", Path(tmp) / "x.c", self.entry[2])], {})
        # The linker refuses an asm placement past the region's end even if it got this far.
        item = self.item()
        item.placements[0].address = hi
        with self.assertRaises(gate.GateError):
            gate.Linker(self.contract, self.raw).link([item])

    def test_data_directives_in_asm_text_are_refused(self):
        with self.assertRaises(gate.GateError):
            gate.asm_unit_object(self.unit, self.object_path, self.spec, {},
                                 "\t.text\n\t.globl\tf_48f54\nf_48f54:\n\t.word\t0x48028800\n")

    def test_corrupted_asm_byte_is_attributed(self):
        item = self.item()
        target = next(p for p in item.placements if p.symbol == "ot_index_quad")
        item.obj.code[target.section][target.offset + 12] ^= 0x01
        linker = gate.Linker(self.contract, self.raw)
        linker.link([item])
        blame = linker.attribute(self.raw)
        self.assertEqual(len(blame), 1)
        owner, offsets = blame[0]
        self.assertEqual((owner["kind"], owner["unit"], owner["symbol"]), ("asm", "ot_depth", "ot_index_quad"))
        self.assertEqual(offsets, [self.contract["header_size"] + 0x80048f8c + 12 - 0x80010000])

    # The first table record retail keeps in .text after f_5533c: one 12-byte sprite frame
    # (x -18, y -65, 84 x 68, pixel offset 0) at 0x800558e8.
    TABLE = "\t.text\r\n\t.globl\tg_558e8\r\ng_558e8:\r\n\t.half\t-18, -65, 84, 68\r\n\t.word\t0\r\n"

    def _table_unit(self, tmp, address):
        tmp = Path(tmp)
        (tmp / "tbl.s").write_bytes(self.TABLE.encode("ascii"))
        spec = {"functions": {"g_558e8": {"address": hex(address), "size": 12}}, "symbols": {}}
        (tmp / "tbl.json").write_text(json.dumps(spec))
        (tmp / "objects").mkdir()
        unit, object_path, listing = gate.build_unit(("tbl", tmp / "tbl.s", tmp / "tbl.json"), tmp / "objects")
        return unit, object_path, spec, listing.read_text(encoding="latin1")

    def test_table_inside_window_is_accepted_and_counted(self):
        with tempfile.TemporaryDirectory() as tmp:
            unit, object_path, spec, listing = self._table_unit(tmp, 0x800558e8)
            self.assertEqual(gate.verify.asm_units.text_data_owners(listing), {"g_558e8"})
            item = gate.asm_unit_object(unit, object_path, spec, {}, listing)
            linker = gate.Linker(self.contract, self.raw)
            linker.link([item])
            self.assertEqual(linker.attribute(self.raw), [])
            self.assertEqual(linker.counts()["asm"], 12)

    def test_table_outside_window_is_refused(self):
        lo, _ = gate.verify.asm_units.TEXT_DATA_WINDOW
        with tempfile.TemporaryDirectory() as tmp:
            unit, object_path, spec, listing = self._table_unit(tmp, lo - 12)  # inside the asm region, before the window
            with self.assertRaises(gate.GateError) as caught:
                gate.asm_unit_object(unit, object_path, spec, {}, listing)
            self.assertIn("outside the table window", str(caught.exception))



class LibraryAsmUnitTest(unittest.TestCase):
    """Library assembly units (tools/asm_units.py, category "library_asm"): linked as their own
    kind with their .data, counted apart from source bytes and from hand-written asm, confined
    to the library ranges of config/asm_regions.json, and attributed when a byte is wrong."""

    # Frozen copy of the LIBSPU S_GKS unit: SpuGetKeyStatus (148 bytes at 0x800590a8) and its
    # module-local _spu_RXX word in .data at 0x80073afc; imports _spu_keystat (0x80073d4c).
    FIXTURES = Path(__file__).resolve().parent / "testdata" / "library_asm"
    UNIT = "gks_fixture"

    @classmethod
    def setUpClass(cls):
        cls.contract, cls.raw = gate.load_contract()
        cls.tmp = tempfile.TemporaryDirectory()
        cls.entry = (cls.UNIT, cls.FIXTURES / "libspu_s_gks.s", cls.FIXTURES / "libspu_s_gks.json")
        root = Path(cls.tmp.name) / "objects"
        root.mkdir()
        cls.unit, cls.object_path, cls.listing = gate.build_unit(cls.entry, root)
        cls.spec = json.loads(cls.entry[2].read_text())
        cls.defs = json.loads((cls.FIXTURES / "libspu_s_gks.data.json").read_text())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def item(self, spec=None, defs=None, category="library_asm"):
        return gate.asm_unit_object(self.unit, self.object_path, spec or self.spec,
                                    self.defs if defs is None else defs,
                                    self.listing.read_text(encoding="latin1"), category)

    def report(self, kind="library_asm"):
        prefix = gate.verify.asm_units.REPORT_PREFIX[kind]
        return {"checks": [{"unit": self.UNIT, "kind": kind, "function": "SpuGetKeyStatus",
                            "address": "0x800590a8", "size": 148, "status": "match"}],
                "data_checks": [{"unit": self.UNIT, "kind": kind, "status": "match", "definitions": [
                    {"symbol": "_spu_RXX", "address": "0x80073afc", "size": 4, "status": "match"}]}],
                f"{prefix}_bytes": 152}

    def test_links_with_its_data_and_counts_as_library_asm(self):
        item = self.item()
        self.assertEqual({(p.symbol, p.kind) for p in item.placements},
                         {("SpuGetKeyStatus", "library_asm"), ("_spu_RXX", "library_asm")})
        linker = gate.Linker(self.contract, self.raw)
        linker.link([item])
        # Every byte, including the lui/lw pair resolved through the .data static, is retail's.
        self.assertEqual(linker.attribute(self.raw), [])
        counts = linker.counts()
        self.assertEqual(counts["library_asm"], 152)
        self.assertNotIn("asm", counts)
        self.assertNotIn("function", counts)
        self.assertNotIn("data", counts)
        cross = gate.verify.asm_units.cross_check
        specs = {self.UNIT: self.spec}
        self.assertEqual(cross(linker.owners, self.report(), specs, [self.UNIT], "library_asm"), [])
        self.assertEqual(cross(linker.owners, {"checks": [], "data_checks": []}, specs, [], "asm"), [])
        # verify.py counting it as hand-written asm, or as C, is a disagreement on both sides.
        as_asm = self.report("asm")
        self.assertTrue(cross(linker.owners, as_asm, specs, [self.UNIT], "library_asm"))
        self.assertTrue(cross(linker.owners, as_asm, specs, [], "asm"))
        dropped = {**self.report(), "data_checks": []}
        problems = cross(linker.owners, dropped, specs, [self.UNIT], "library_asm")
        self.assertTrue(any("datum gks_fixture:_spu_RXX" in p for p in problems))

    def test_hand_written_category_refuses_it(self):
        with self.assertRaises(gate.GateError) as caught:
            self.item(category="asm")
        self.assertIn("outside the hand-written assembly region", str(caught.exception))

    def test_placements_outside_library_ranges_are_refused(self):
        moved = json.loads(json.dumps(self.spec))
        moved["functions"]["SpuGetKeyStatus"]["address"] = "0x80061310"  # LIBCD C_010: certifiable 3.3
        with self.assertRaises(gate.GateError) as caught:
            self.item(moved)
        self.assertIn("library assembly text ranges", str(caught.exception))
        moved["functions"]["SpuGetKeyStatus"]["address"] = "0x80058000"   # the renderer region
        with self.assertRaises(gate.GateError):
            self.item(moved)
        # Data must lie in the library data ranges: memcard's g_card_event_messages is game data.
        with self.assertRaises(gate.GateError) as caught:
            self.item(defs={"_spu_RXX": {"address": "0x80073af4", "size": 4}})
        self.assertIn("library assembly data ranges", str(caught.exception))
        # The linker refuses the same placements even if they got this far.
        for symbol, address in (("SpuGetKeyStatus", 0x80058000), ("_spu_RXX", 0x80073af4)):
            item = self.item()
            next(p for p in item.placements if p.symbol == symbol).address = address
            with self.assertRaises(gate.GateError):
                gate.Linker(self.contract, self.raw).link([item])

    def test_registry_keeps_categories_apart(self):
        validate = gate.verify.asm_units.validate_registry
        validate([], [], {self.UNIT: self.defs}, [self.entry])
        with self.assertRaises(gate.verify.asm_units.AsmPolicyError):
            validate([], [self.entry], {self.UNIT: self.defs}, [])        # as hand-written asm
        hand = next(e for e in gate.verify.ASM_UNITS if e[0] == "ot_depth")
        with self.assertRaises(gate.verify.asm_units.AsmPolicyError):
            validate([], [], {}, [hand])                                  # renderer code as library
        with self.assertRaises(gate.verify.asm_units.AsmPolicyError):
            validate([], [self.entry], {}, [self.entry])                  # registered twice

    def test_corrupted_byte_is_attributed(self):
        item = self.item()
        target = next(p for p in item.placements if p.symbol == "_spu_RXX")
        item.obj.code[target.section][target.offset] ^= 0x01
        linker = gate.Linker(self.contract, self.raw)
        linker.link([item])
        (owner, offsets), = linker.attribute(self.raw)
        self.assertEqual((owner["kind"], owner["unit"], owner["symbol"]), ("library_asm", self.UNIT, "_spu_RXX"))
        self.assertEqual(offsets, [self.contract["header_size"] + 0x80073afc - 0x80010000])


class StaticSymbolTest(unittest.TestCase):
    """Static functions and static data (tools/lnk_symbols.py): located by local-symbol record,
    linked and counted as source, audited for ownership, and scoped to their unit."""

    # Frozen copy of the tasker unit with its four list helpers static, as in Tasker.c, and the
    # headers it needs inlined: 9 functions, 636 bytes at 0x80015cb0, and the statics
    # TaskMEM/TaskFree/TaskUsed in .sbss at 0x8008d088/090/098.
    FIXTURES = Path(__file__).resolve().parent / "testdata" / "statics"
    STATIC = ("AddTASKToFree", "GetTASKFromFree", "AddTASKToInUse", "RemoveTASKFromInUse")

    @classmethod
    def setUpClass(cls):
        cls.contract, cls.raw = gate.load_contract()
        cls.tmp = tempfile.TemporaryDirectory()
        cls.entry = ("tasker", cls.FIXTURES / "tasker.c", cls.FIXTURES / "tasker.json")
        root = Path(cls.tmp.name) / "objects"
        root.mkdir()
        cls.unit, cls.object_path, cls.listing = gate.build_unit(cls.entry, root)
        cls.spec = json.loads(cls.entry[2].read_text())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def item(self, spec=None, unit="tasker", object_path=None):
        return gate.unit_object(unit, object_path or self.object_path, spec or self.spec, {}, set())

    def changed(self, edit):
        spec = json.loads(json.dumps(self.spec))
        edit(spec)
        return spec

    def test_static_functions_link_and_count_as_source(self):
        obj = gate.lnk_symbols.load(self.object_path)
        self.assertEqual(sorted(n for n, _, _ in obj.locals),
                         sorted(self.STATIC + ("TaskMEM", "TaskFree", "TaskUsed")))
        self.assertFalse(set(self.STATIC) & {n for n, _, _ in obj.xdefs})
        item = self.item()
        self.assertEqual({p.symbol: p.kind for p in item.placements},
                         {name: "function" for name in self.spec["functions"]})
        linker = gate.Linker(self.contract, self.raw)
        linker.link([item])
        self.assertEqual(linker.attribute(self.raw), [])
        self.assertEqual(linker.counts()["function"], 636)
        self.assertFalse(set(self.STATIC) & set(linker.symbols))  # never bound globally
        self.assertIn("TaskerInit", linker.symbols)
        placed = {o["symbol"] for o in linker.owners if o["kind"] == "function"}
        self.assertTrue(set(self.STATIC) <= placed)
        # Calls into a static are relocations to .text+offset, resolved through the static's own
        # placement: a wrong static address shows up as wrong bytes in its callers.
        moved = self.changed(lambda s: s["functions"]["AddTASKToFree"].update(address="0x80015cb4"))
        with self.assertRaises(gate.GateError):  # it now overlaps TaskerInit
            gate.Linker(self.contract, self.raw).link([self.item(moved)])
        alone = gate.Linker(self.contract, self.raw)
        item = self.item(moved)
        item.placements = [p for p in item.placements if p.symbol != "TaskerInit"]
        alone.link([item])
        self.assertIn(("tasker", "TaskDie"), {(o["unit"], o["symbol"]) for o, _ in alone.attribute(self.raw)})

    def test_bytecheck_locates_statics(self):
        tmp = Path(self.tmp.name)
        reference = tmp / "reference.exe"
        reference.write_bytes(self.raw)

        def bytecheck(function, mapping):
            out = tmp / f"{function}.json"
            subprocess.run([sys.executable, str(Path(gate.TOOLS) / "bytecheck.py"), "--object", str(self.object_path),
                            "--symbol-map", str(mapping), "--reference", str(reference), "--function", function,
                            "--json-report", str(out)], capture_output=True)
            return json.loads(out.read_text())

        for function in self.spec["functions"]:
            self.assertEqual(bytecheck(function, self.entry[2])["status"], "match", function)
        exported = tmp / "exported.json"
        exported.write_text(json.dumps(self.changed(lambda s: s["functions"]["AddTASKToInUse"].pop("static"))))
        self.assertIn("static in the object", bytecheck("AddTASKToInUse", exported)["error"])
        self.assertIn("not inside a mapped function", bytecheck("TaskCreate", exported)["error"])
        # verify.py's ownership audit, which runs before any byte comparison.
        gate.verify.validate_object_symbols(self.object_path, self.spec)
        with self.assertRaisesRegex(RuntimeError, "static in the object but the map declares it exported"):
            gate.verify.validate_object_symbols(self.object_path, json.loads(exported.read_text()))

    def test_ownership_audit_refuses_any_disagreement(self):
        cases = {
            "static in the object but the map declares it exported":
                lambda s: s["functions"]["AddTASKToFree"].pop("static"),
            "exported in the object but the map declares it static":
                lambda s: s["functions"]["TaskerInit"].update(static=True),
            "text symbols without a map entry": lambda s: s["functions"].pop("RemoveTASKFromInUse"),
            "is defined 0 times": lambda s: s["functions"].update(
                Ghost={"address": "0x80015f2c", "size": 4, "static": True}),
            "TaskFree (+0x8, 8 bytes) has no static_data entry": lambda s: s["static_data"].pop("TaskFree"),
            "resolves to 0x8008d098": lambda s: s["static_data"]["TaskUsed"].update(address="0x8008d0a0"),
            "occupies 8 bytes, the map says 4": lambda s: s["static_data"]["TaskMEM"].update(size=4),
            "is in .sbss, the map says .bss": lambda s: s["static_data"]["TaskMEM"].update(section=".bss"),
            "Ghost is not a local symbol": lambda s: s["static_data"].update(
                Ghost={"address": "0x8008d0a0", "size": 8, "section": ".sbss"}),
            "needs the unit's .sbss base": lambda s: s.pop("sections"),
        }
        for message, edit in cases.items():
            with self.subTest(message):
                with self.assertRaisesRegex(gate.GateError, re.escape(message)):
                    self.item(self.changed(edit))

    def test_same_static_name_in_two_units_is_per_unit(self):
        # A second unit with its own static AddTASKToFree, called by an exported function and
        # mapped elsewhere: its bytes are not retail's, so it alone may be blamed.
        tmp = Path(self.tmp.name) / "other"
        tmp.mkdir()
        (tmp / "other.c").write_text("typedef struct TASK { int Priority; struct TASK *Next; } TASK;\n"
                                     "extern TASK *g_free;\n"
                                     "static void AddTASKToFree(TASK *t) { t->Next = g_free; g_free = t; }\n"
                                     "void other_free(TASK *t) { AddTASKToFree(t); }\n")
        (tmp / "objects").mkdir()
        spec = {"gp": "0x8008cd74", "functions": {}, "symbols": {"g_free": "0x8008d0a0"}}
        (tmp / "other.json").write_text(json.dumps(spec))
        _, object_path, _ = gate.build_unit(("other", tmp / "other.c", tmp / "other.json"), tmp / "objects")
        obj = gate.lnk_symbols.load(object_path)
        text = sorted((off, n) for n, _, off in obj.xdefs + obj.locals if n in ("AddTASKToFree", "other_free"))
        ends = [off for off, _ in text[1:]] + [len(obj.code[next(s for s, n in obj.sections.items() if n == ".text")])]
        base = 0x80020000
        spec["functions"] = {n: {"address": hex(base + off), "size": end - off} for (off, n), end in zip(text, ends)}
        spec["functions"]["AddTASKToFree"]["static"] = True
        (tmp / "other.json").write_text(json.dumps(spec))
        other = self.item(spec, "other", object_path)
        linker = gate.Linker(self.contract, self.raw)
        linker.link([self.item(), other])
        self.assertNotIn("AddTASKToFree", linker.symbols)
        blamed = {(o["unit"], o["symbol"]) for o, _ in linker.attribute(self.raw)}
        self.assertTrue(blamed)
        self.assertEqual({u for u, _ in blamed}, {"other"})  # the tasker's calls still reach its own static
        caller = next(p for p in other.placements if p.symbol == "other_free")
        start = self.contract["header_size"] + caller.address - linker.load
        words = struct.unpack(f"<{caller.size // 4}I", bytes(linker.image[start:start + caller.size]))
        static_b = int(spec["functions"]["AddTASKToFree"]["address"], 16)
        self.assertEqual([w for w in words if w >> 26 == 3], [0x0c000000 | (static_b >> 2) & 0x03ffffff])
        # The registry: two units may each own a static of one name, never a global one.
        units = [("tasker", self.entry[1], self.entry[2]), ("other", tmp / "other.c", tmp / "other.json")]
        gate.verify.validate_function_ownership(units)
        gate.verify.validate_symbol_bindings(units)
        exported = tmp / "exported.json"
        exported.write_text(json.dumps({**spec, "functions": {
            **spec["functions"], "AddTASKToFree": {k: v for k, v in spec["functions"]["AddTASKToFree"].items()
                                                   if k != "static"}}}))
        global_tasker = tmp / "tasker.json"
        global_tasker.write_text(json.dumps(self.changed(lambda s: s["functions"]["AddTASKToFree"].pop("static"))))
        with self.assertRaisesRegex(RuntimeError, "registered in both"):
            gate.verify.validate_function_ownership([("tasker", self.entry[1], global_tasker),
                                                     ("other", tmp / "other.c", exported)])

    def test_no_global_binds_to_a_static(self):
        tmp = Path(self.tmp.name) / "bindings"
        tmp.mkdir()

        def units(symbols, static_data=None):
            spec = {"functions": {}, "symbols": symbols}
            if static_data:
                spec["static_data"] = static_data
            (tmp / "c.json").write_text(json.dumps(spec))
            return [("tasker", self.entry[1], self.entry[2]), ("c", self.entry[1], tmp / "c.json")]

        # The same name for a different global elsewhere is legal; the static's own storage is not.
        gate.verify.validate_symbol_bindings(units({"AddTASKToFree": "0x80055550"}))
        for symbols in ({"AddTASKToFree": "0x80015cb0"}, {"f_15cb0": "0x80015cb0"}, {"g_8d094": "0x8008d094"}):
            with self.subTest(symbols), self.assertRaisesRegex(RuntimeError, "a static has no external name"):
                gate.verify.validate_symbol_bindings(units(symbols))
        with self.assertRaisesRegex(RuntimeError, "overlap"):
            gate.verify.validate_symbol_bindings(
                units({}, {"s": {"address": "0x8008d09c", "size": 8, "section": ".sbss"}}))

    def test_bss_layout_models_unit_statics(self):
        layout = gate.section_layout.load_layout()
        bss_config = json.loads(gate.section_layout.BSS_CONFIG.read_text())
        start, end = (int(bss_config["range"][k], 16) for k in ("start", "end"))
        ev = types.SimpleNamespace(reference_sha256=layout["reference_sha256"])
        statics = [(0x8008d088 + 8 * i, 8, ".sbss", "tasker", n)
                   for i, n in enumerate(("TaskMEM", "TaskFree", "TaskUsed"))]
        symbols = {0x8008d0a0: {"g_next": ["u"]}}
        sbss_end, common = (0x8008d500, 0x8008d600), (0x80090000, 0x80091000)
        bss = gate.section_layout.derive_bss(ev, layout, [], [], {}, symbols, start, end, sbss_end, common, statics)
        rows = [(e["kind"], e["names"], e["owner"], e["size"], e["region"]) for e in bss["entries"]]
        self.assertEqual(rows[:3], [("unit_static", [n], "tasker", 8, ".sbss")
                                    for n in ("TaskMEM", "TaskFree", "TaskUsed")])
        self.assertEqual(bss["counts"]["unit_statics"], 3)
        self.assertEqual(gate.section_layout.validate(layout, bss), [])
        bss["entries"][0]["section"] = ".bss"
        self.assertTrue(any("is declared .bss" in p for p in gate.section_layout.validate(layout, bss)))
        with self.assertRaises(gate.section_layout.LayoutError):
            gate.section_layout.derive_bss(ev, layout, [], [], {}, {}, start, end, sbss_end, common,
                                           statics + [(0x8008d094, 8, ".sbss", "other", "s")])

    def test_witness_respects_static_labels(self):
        obj = types.SimpleNamespace(code={1: bytearray(16)}, xdefs=[], locals=[("s", 1, 8)], patches={})
        self.assertIn("runs past the next item boundary at +0x8",
                      gate.generated_witness._object_extent(obj, 1, 0, 16))
        self.assertIsNone(gate.generated_witness._object_extent(obj, 1, 8, 8))



class InitialisedStaticTest(unittest.TestCase):
    """Statics of the initialised sections (tools/lnk_symbols.py, ``initialised_statics``): ASPSX
    writes no local-symbol record for them, so the listing's unexported data labels locate them;
    the data definition of the name places them, and they are linked and counted as source data."""

    # Frozen copy of the texture_upload unit with its project headers inlined: 6 functions,
    # 1508 bytes at 0x800218a4, the static g_unpack_hblanks in .sdata at 0x8008cfa8 (2 bytes)
    # and six .sbss statics at 0x8008d0d0.
    FIXTURES = Path(__file__).resolve().parent / "testdata" / "statics"
    STATIC = "g_unpack_hblanks"

    @classmethod
    def setUpClass(cls):
        cls.contract, cls.raw = gate.load_contract()
        cls.tmp = tempfile.TemporaryDirectory()
        cls.entry = ("texture_upload", cls.FIXTURES / "texture_upload.c", cls.FIXTURES / "texture_upload.json")
        root = Path(cls.tmp.name) / "objects"
        root.mkdir()
        cls.unit, cls.object_path, asm_path = gate.build_unit(cls.entry, root)
        cls.listing = asm_path.read_text(encoding="latin1")
        cls.spec = json.loads(cls.entry[2].read_text())
        cls.data_map = cls.FIXTURES / "texture_upload.data.json"
        cls.defs = json.loads(cls.data_map.read_text())["texture_upload"]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def item(self, spec=None, defs=None, listing=True):
        return gate.unit_object("texture_upload", self.object_path, spec or self.spec,
                                self.defs if defs is None else defs, set(),
                                listing=self.listing if listing else None)

    def changed(self, value, edit):
        value = json.loads(json.dumps(value))
        edit(value)
        return value

    def test_listing_locates_the_static(self):
        obj = gate.lnk_symbols.load(self.object_path)
        self.assertNotIn(self.STATIC, {n for n, _, _ in obj.xdefs + obj.locals})  # no record of it
        self.assertEqual(gate.data_extent.static_labels(self.listing), {self.STATIC: (".sdata", 0)})
        self.assertEqual(gate.lnk_symbols.named_data(obj, self.spec, {self.STATIC: (".sdata", 0)})[self.STATIC],
                         (".sdata", 0))
        gate.verify.validate_object_symbols(self.object_path, self.spec)  # the .sbss audit is unchanged

    def test_static_links_as_source_data(self):
        item = self.item()
        data = [p for p in item.placements if p.kind == "data"]
        self.assertEqual([(p.symbol, p.address, p.size) for p in data], [(self.STATIC, 0x8008cfa8, 2)])
        linker = gate.Linker(self.contract, self.raw)
        linker.link([item])
        self.assertEqual(linker.attribute(self.raw), [])
        self.assertEqual(linker.counts()["data"], 2)
        self.assertEqual(linker.counts()["function"], 1508)
        self.assertNotIn(self.STATIC, linker.symbols)  # never bound globally
        _, findings = gate.generated_placements(item, self.listing, self.raw, self.contract,
                                                gate.section_layout.load_layout())
        self.assertIn(("confirmed", self.STATIC), {(f["status"], f.get("symbol")) for f in findings})
        # Its readers reach it through its own placement ($gp-relative): moving the static in
        # both the map and its definition moves their operands, which no longer match retail.
        moved = lambda v: v[self.STATIC].update(address="0x8008cfaa")
        item = self.item(self.changed(self.spec, lambda s: moved(s["static_data"])), self.changed(self.defs, moved))
        linker = gate.Linker(self.contract, self.raw)
        linker.link([item])
        self.assertEqual({o["symbol"] for o, _ in linker.attribute(self.raw)}, {"f_21a80", "f_21ccc"})

    def test_audit_refuses_any_disagreement(self):
        spec_cases = {
            "(+0x0) has no static_data entry": lambda s: s["static_data"].pop(self.STATIC),
            "is in .sdata, the map says .data": lambda s: s["static_data"][self.STATIC].update(section=".data"),
            "Ghost is not an unexported .sdata label": lambda s: s["static_data"].update(
                Ghost={"address": "0x8008cfaa", "size": 2, "section": ".sdata"}),
            "is 0x8008cfaa+2 in the map but 0x8008cfa8+2": lambda s: s["static_data"][self.STATIC].update(
                address="0x8008cfaa"),
            "needs a section among": lambda s: s["static_data"][self.STATIC].update(section=".text"),
        }
        for message, edit in spec_cases.items():
            with self.subTest(message):
                with self.assertRaisesRegex(gate.GateError, re.escape(message)):
                    self.item(self.changed(self.spec, edit))
        with self.assertRaisesRegex(gate.GateError, "has no data definition of its unit"):
            self.item(defs={})
        with self.assertRaisesRegex(gate.GateError, "not an unexported .sdata label"):
            self.item(listing=False)  # without the listing nothing locates it
        # A definition one item cannot hold is an extent finding, which fails the gate.
        spec = self.changed(self.spec, lambda s: s["static_data"][self.STATIC].update(size=4))
        defs = self.changed(self.defs, lambda d: d[self.STATIC].update(size=4))
        _, findings = gate.generated_placements(self.item(spec, defs), self.listing, self.raw, self.contract,
                                                gate.section_layout.load_layout())
        self.assertIn(("extent", self.STATIC), {(f["status"], f.get("label")) for f in findings})

    def test_verify_witnesses_agree(self):
        tmp = Path(self.tmp.name)
        reference = tmp / "reference.exe"
        reference.write_bytes(self.raw)
        listing = Path(self.object_path).with_name("INPUT.S")

        def run(tool, *args):
            out = tmp / f"{tool}.json"
            subprocess.run([sys.executable, str(Path(gate.TOOLS) / tool), "--object", str(self.object_path),
                            "--reference", str(reference), "--json-report", str(out), "--asm", str(listing),
                            *map(str, args)], capture_output=True)
            return json.loads(out.read_text())

        report = run("data_check.py", "--unit", "texture_upload", "--symbol-map", self.entry[2],
                     "--map", self.data_map)
        self.assertEqual(report["status"], "match")
        self.assertEqual([(d["symbol"], d["address"], d["size"], d["section"]) for d in report["definitions"]],
                         [(self.STATIC, "0x8008cfa8", 2, ".sdata")])
        for function in ("f_21a80", "f_21ccc"):
            self.assertEqual(run("bytecheck.py", "--symbol-map", self.entry[2], "--function", function,
                                 "--unit", "texture_upload", "--data-map", self.data_map)["status"], "match")
        undeclared = tmp / "undeclared.json"
        undeclared.write_text(json.dumps(self.changed(self.spec, lambda s: s["static_data"].pop(self.STATIC))))
        self.assertIn("has no static_data entry", run("data_check.py", "--unit", "texture_upload",
                                                      "--symbol-map", undeclared, "--map", self.data_map)["error"])

    def test_no_global_binds_to_an_initialised_static(self):
        tmp = Path(self.tmp.name) / "bindings"
        tmp.mkdir(exist_ok=True)
        (tmp / "c.json").write_text(json.dumps({"functions": {}, "symbols": {"g_hblanks": "0x8008cfa8"}}))
        units = [("texture_upload", self.entry[1], self.entry[2]), ("c", self.entry[1], tmp / "c.json")]
        with self.assertRaisesRegex(RuntimeError, "a static has no external name"):
            gate.verify.validate_symbol_bindings(units)
        both = tmp / "both.json"
        both.write_text(json.dumps(self.changed(self.spec, lambda s: s["symbols"].update(
            {self.STATIC: "0x8008cfa8"}))))
        with self.assertRaisesRegex(RuntimeError, "both as statics and as referenced globals"):
            gate.verify.validate_symbol_bindings([("texture_upload", self.entry[1], both)])

    def test_static_labels_rule(self):
        listing = "\n".join([
            "\t.globl\tg_exported", "\t.sdata", "\t.align\t1", "g_exported:", "\t.half\t1",
            "\t.align\t1", "s_file:", "\t.half\t2",
            "\t.data", "\t.align\t2", "s_table:", "\t.word\t1,2,3,4,5",
            "\t.rdata", "\t.align\t2", "$LC0:", '\t.ascii\t"ab\\000"', "\t.align\t2", "buf.12:", "\t.word\t0",
            "\t.text", "f:", "\t.word\t0", ""])
        self.assertEqual(gate.data_extent.static_labels(listing),
                         {"s_file": (".sdata", 2), "s_table": (".data", 0), "buf.12": (".rdata", 4)})


class SplitRelocationTest(unittest.TestCase):
    """Split HI16/LO16 halves (asm/lib/hilo.h: PsyQ types 26 and 30 written by ASPSX, and the
    R_MIPS_HI16/LO16 pairs of a GNU as unit converted by tools/elf_lnk.py), and end labels."""

    FIXTURES = Path(__file__).resolve().parent / "testdata"

    @classmethod
    def setUpClass(cls):
        cls.contract, cls.raw = gate.load_contract()
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name) / "objects"
        root.mkdir()
        # Frozen copy of the LIBGTE MTX_00 unit (ASPSX): PushMatrix/PopMatrix hold split pairs.
        lib = cls.FIXTURES / "library_asm"
        cls.mtx_entry = ("mtx_fixture", lib / "libgte_mtx_00.s", lib / "libgte_mtx_00.json")
        cls.mtx_unit, cls.mtx_object, cls.mtx_listing = gate.build_unit(cls.mtx_entry, root)
        cls.mtx_spec = json.loads(cls.mtx_entry[2].read_text())
        cls.mtx_defs = json.loads((lib / "libgte_mtx_00.data.json").read_text())
        # Frozen GNU as object of LIBGTE PATCHGTE, converted as tools/build_asm.py does.
        elf_spec = importlib.util.spec_from_file_location("elf_lnk", Path(__file__).resolve().parent / "elf_lnk.py")
        elf_lnk = importlib.util.module_from_spec(elf_spec)
        elf_spec.loader.exec_module(elf_lnk)
        gnu = cls.FIXTURES / "gnu_as"
        cls.pg_object = root / "PATCHGTE.OBJ"
        cls.pg_object.write_bytes(elf_lnk.convert((gnu / "patchgte.elf").read_bytes()))
        cls.pg_spec = json.loads((gnu / "patchgte.json").read_text())
        cls.pg_listing = (gnu / "patchgte.i").read_text(encoding="latin1")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_split_shapes(self):
        e = (2, 9)
        hi = (60, ("c", 16), (44, ("c", 0x8000), e))
        self.assertEqual(gate.split_relocation(26, hi), (82, e))
        self.assertEqual(gate.split_relocation(30, (46, (58, ("c", 16), hi), e)), (84, e))
        folded = (60, ("c", 16), (44, (4, 3), ("c", 0x8010)))
        self.assertEqual(gate.split_relocation(26, folded), (82, (44, ("c", 0x10), (4, 3))))
        self.assertEqual(gate.split_relocation(30, (46, (58, ("c", 16), folded), (44, (4, 3), ("c", 0x10)))),
                         (84, (44, ("c", 0x10), (4, 3))))
        self.assertEqual(gate.split_relocation(26, e), (26, e))  # left for the linker to refuse
        gp = (46, (12, 4), e)
        self.assertEqual(gate.split_relocation(30, gp), (30, gp))  # GP-relative stays GP-relative

    def test_mtx_00_links_with_its_split_pairs(self):
        item = gate.asm_unit_object(self.mtx_unit, self.mtx_object, self.mtx_spec, self.mtx_defs,
                                    self.mtx_listing.read_text(encoding="latin1"), "library_asm")
        linker = gate.Linker(self.contract, self.raw)
        linker.link([item])
        self.assertEqual(linker.attribute(self.raw), [])
        self.assertEqual(linker.counts()["library_asm"], 2592 + 784)
        kinds = {kind for kind, _, _ in item.obj.patches[2]}
        self.assertTrue({26, 30} <= kinds)

    def test_unpaired_split_half_is_refused(self):
        item = gate.asm_unit_object(self.mtx_unit, self.mtx_object, self.mtx_spec, self.mtx_defs,
                                    self.mtx_listing.read_text(encoding="latin1"), "library_asm")
        item.obj.patches[2] = [p for p in item.obj.patches[2] if not (p[0] == 30 and p[1] == 0x820)]
        with self.assertRaisesRegex(gate.GateError, "have no partner"):
            gate.Linker(self.contract, self.raw).link([item])

    def test_gnu_as_unit_with_end_label_and_sbss_links(self):
        item = gate.asm_unit_object("patchgte_fixture", self.pg_object, self.pg_spec, {},
                                    self.pg_listing, "library_asm")
        linker = gate.Linker(self.contract, self.raw)
        linker.link([item])
        self.assertEqual(linker.attribute(self.raw), [])  # k1 = .text + 0xa0, one past _patch_gte
        self.assertEqual(linker.counts()["library_asm"], 160)


if __name__ == "__main__":
    unittest.main()
