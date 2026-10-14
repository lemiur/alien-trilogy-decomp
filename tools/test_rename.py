"""Tests for tools/rename.py on the frozen fixture tree tools/testdata/rename/."""
import copy
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rename  # noqa: E402

DATA = Path(__file__).resolve().parent / "testdata" / "rename"
SDK = {"u_long", "printf", "a_mode", "TASK"}


class RenameFixture(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.root = self.tmp / "project"
        shutil.copytree(DATA / "input", self.root)
        self.map = self.root / "config" / "style_renames.json"

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_tool(self, dry_run=False, sdk=SDK, check_git=False, map_path=None):
        lines = []
        rc = rename.run(self.root, map_path or self.map, dry_run, sdk=set(sdk), lib=set(), check_git=check_git,
                        out=lines.append)
        return rc, "\n".join(lines)

    def files(self, base):
        return {p.relative_to(base).as_posix(): p.read_bytes() for p in sorted(base.rglob("*")) if p.is_file()}

    def test_apply_matches_expected(self):
        rc, log = self.run_tool()
        self.assertEqual(rc, 0, log)
        got = self.files(self.root)
        got.pop("config/style_renames.json")
        self.assertEqual(got, self.files(DATA / "expected"))

    def test_dry_run_writes_nothing_and_counts(self):
        before = self.files(self.root)
        rc, log = self.run_tool(dry_run=True)
        self.assertEqual(rc, 0, log)
        self.assertEqual(self.files(self.root), before)
        self.assertIn("src/doors.c", log)
        self.assertIn("collisions: none", log)
        self.assertIn("ambiguous (left for manual handling): a_mode", log)

    def test_strings_and_one_word_prose_untouched(self):
        self.run_tool()
        c = (self.root / "src" / "doors.c").read_text()
        self.assertIn('"doors_build: %d doors\\n"', c)
        self.assertIn("a Door is a record", c)
        self.assertIn("record->a_mode", c)
        self.assertIn("f_1234a();", c)
        self.assertIn("random is an English word", (self.root / "config" / "doors.json").read_text())

    def test_tag_and_typedef_take_their_own_names(self):
        self.run_tool()
        h = (self.root / "include" / "DOOR.H").read_text()
        self.assertIn("typedef struct doorRecord_s doorRecord_t;", h)
        self.assertIn("} door_t;", h)

    def write_map(self, change):
        data = json.loads(self.map.read_text())
        change(data)
        path = self.tmp / "map.json"
        path.write_text(json.dumps(data))
        return path

    def test_collision_two_olds_one_new(self):
        path = self.write_map(lambda d: d["functions"]["door_link"].update(new="Door_Build"))
        before = self.files(self.root)
        rc, log = self.run_tool(map_path=path)
        self.assertEqual(rc, 1)
        self.assertIn("Door_Build: several old names", log)
        self.assertEqual(self.files(self.root), before)

    def test_collision_with_sdk_name_and_keyword(self):
        path = self.write_map(lambda d: (d["functions"]["door_link"].update(new="printf"),
                                         d["globals"]["g_door_count"].update(new="while")))
        rc, log = self.run_tool(map_path=path)
        self.assertEqual(rc, 1)
        self.assertIn("printf: an SDK/library name", log)
        self.assertIn("while: a C keyword", log)

    def test_collision_with_existing_identifier(self):
        path = self.write_map(lambda d: d["globals"]["g_door_count"].update(new="f_1234a"))
        rc, log = self.run_tool(map_path=path)
        self.assertEqual(rc, 1)
        self.assertIn("f_1234a: already an identifier", log)

    def test_member_with_two_new_names_is_ambiguous(self):
        def change(d):
            d["members"]["Door"]["members"]["next_door"] = {"new": "following"}
        path = self.write_map(change)
        rc, log = self.run_tool(map_path=path)
        self.assertEqual(rc, 1)
        self.assertIn("member next_door: different new names per owner", log)

    def test_refuses_dirty_tree(self):
        if shutil.which("git") is None:
            self.skipTest("git not available")
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        before = self.files(self.root)
        rc, log = self.run_tool(check_git=True)
        self.assertEqual(rc, 2)
        self.assertIn("dirty tree", log)
        self.assertEqual({k: v for k, v in self.files(self.root).items() if not k.startswith(".git/")},
                         {k: v for k, v in before.items() if not k.startswith(".git/")})


class LocalRule(unittest.TestCase):
    def test_local_rule(self):
        m = rename.RenameMap({"_rules": {"acronyms": ["cd", "ft4"], "acronym_forms": {"nocull": "NoCull"}}},
                             {"u_long"}, set())
        self.assertEqual(m.mechanical("next_think"), "nextThink")
        self.assertEqual(m.mechanical("cd_volume"), "cdVolume")
        self.assertEqual(m.mechanical("draw_ft4_nocull"), "drawFT4NoCull")
        self.assertEqual(m.mechanical("anim_2_frames"), "anim2Frames")
        self.assertIsNone(m.mechanical("u_long"))
        self.assertIsNone(m.mechanical("f_5c6b0"))
        self.assertIsNone(m.mechanical("i"))
        self.assertIsNone(m.mechanical("MAX_COUNT"))
        self.assertIsNone(m.mechanical("__attribute__"))


class MapMerge(unittest.TestCase):
    def test_merge_keeps_approved_adds_new_drops_gone(self):
        import rename_map
        empty = {k: {} for k in rename_map.KINDS}
        fresh = dict(copy.deepcopy(empty), _rules={}, members={"Door": {"header": "h", "owner_type_new": "door_t",
                                                                        "members": {"next_door": {"new": "nextDoor"}}}})
        fresh["functions"] = {"door_open": {"unit": "doors", "new": "Door_OpenFresh"},
                              "door_new": {"unit": "doors", "new": "Door_New"}}
        approved = dict(copy.deepcopy(empty), members={"Gone": {"members": {}}})
        approved["functions"] = {"door_open": {"unit": "doors", "new": "Door_Open", "asm_prefix_proposed": True},
                                 "door_gone": {"unit": "doors", "new": "Door_Gone"}}
        out, added, dropped = rename_map.merge(fresh, approved)
        self.assertEqual(out["functions"]["door_open"], {"unit": "doors", "new": "Door_Open"})
        self.assertIn(("functions", "door_new", "Door_New"), added)
        self.assertIn(("members", "Door.next_door", "nextDoor"), added)
        self.assertIn(("functions", "door_gone", None), dropped)
        self.assertIn(("members", "Gone", None), dropped)


if __name__ == "__main__":
    unittest.main()
