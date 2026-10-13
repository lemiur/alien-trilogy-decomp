"""Recompile recovered C and strictly compare every declared function byte.

Assembly units are assembled with tools/build_asm.py and compared the same way, but reported
apart, never as C: hand-written GTE/renderer code (ASM_UNITS, kind "asm", byte_matched_asm_*)
and Sony Runtime Library variant modules (LIBRARY_ASM_UNITS, kind "library_asm",
byte_matched_library_asm_*). tools/asm_units.py confines each to config/asm_regions.json.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from datetime import datetime, timezone

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import proctree  # noqa: E402

WORKSPACE = PROJECT.parent
TOOLS = PROJECT / "tools"
UNITS = (
    ("main", PROJECT / "src/main.c", PROJECT / "config/main.json"),
    ("actor", PROJECT / "src/actor.c", PROJECT / "config/actor.json"),
    ("map_objects", PROJECT / "src/map_objects.c", PROJECT / "config/map_objects.json"),
    ("tasker", PROJECT / "src/tasker.c", PROJECT / "config/tasker.json"),
    ("audio_control", PROJECT / "src/audio_control.c", PROJECT / "config/audio_control.json"),
    ("level_triggers", PROJECT / "src/level_triggers.c", PROJECT / "config/level_triggers.json"),
    ("music_control", PROJECT / "src/music_control.c", PROJECT / "config/music_control.json"),
    ("text", PROJECT / "src/text.c", PROJECT / "config/text.json"),
    ("doors", PROJECT / "src/doors.c", PROJECT / "config/doors.json"),
    ("game_helpers7", PROJECT / "src/game_helpers7.c", PROJECT / "config/game_helpers7.json"),
    ("memcard", PROJECT / "src/memcard.c", PROJECT / "config/memcard.json"),
    ("sprites", PROJECT / "src/sprites.c", PROJECT / "config/sprites.json"),
    ("model_draw", PROJECT / "src/model_draw.c", PROJECT / "config/model_draw.json"),
    ("geometry", PROJECT / "src/geometry.c", PROJECT / "config/geometry.json"),
    ("effects", PROJECT / "src/effects.c", PROJECT / "config/effects.json"),
    ("debris", PROJECT / "src/debris.c", PROJECT / "config/debris.json"),
    ("texture_upload", PROJECT / "src/texture_upload.c", PROJECT / "config/texture_upload.json"),
    ("lights", PROJECT / "src/lights.c", PROJECT / "config/lights.json"),
    ("particle_hits", PROJECT / "src/particle_hits.c", PROJECT / "config/particle_hits.json"),
    ("countdown_timer", PROJECT / "src/countdown_timer.c", PROJECT / "config/countdown_timer.json"),
    ("lift", PROJECT / "src/lift.c", PROJECT / "config/lift.json"),
    ("object_render", PROJECT / "src/object_render.c", PROJECT / "config/object_render.json"),
    ("particle", PROJECT / "src/particle.c", PROJECT / "config/particle.json"),
    ("game_20000_2", PROJECT / "src/game_20000_2.c", PROJECT / "config/game_20000_2.json"),
    ("game_20000_3", PROJECT / "src/game_20000_3.c", PROJECT / "config/game_20000_3.json"),
    ("game_20000_8", PROJECT / "src/game_20000_8.c", PROJECT / "config/game_20000_8.json"),
    ("movie", PROJECT / "src/movie.c", PROJECT / "config/movie.json"),
    ("command_sequencer", PROJECT / "src/command_sequencer.c", PROJECT / "config/command_sequencer.json"),
    ("pause_menu", PROJECT / "src/pause_menu.c", PROJECT / "config/pause_menu.json"),
    ("audio", PROJECT / "src/audio.c", PROJECT / "config/audio.json"),
    ("weapon_slots", PROJECT / "src/weapon_slots.c", PROJECT / "config/weapon_slots.json"),
    ("hud", PROJECT / "src/hud.c", PROJECT / "config/hud.json"),
    ("player", PROJECT / "src/player.c", PROJECT / "config/player.json"),
    ("save_password", PROJECT / "src/save_password.c", PROJECT / "config/save_password.json"),
    ("frontend", PROJECT / "src/frontend.c", PROJECT / "config/frontend.json"),
)

# Hand-written assembly units (AGENTS.md, "Hand-written assembly"): real .s files assembled by
# ASPSX through tools/build_asm.py, admitted only inside the "asm" ranges of
# config/asm_regions.json (the GTE/renderer code). Their bytes are reported as asm, never as C.
ASM_UNITS = (
    ("ot_depth", PROJECT / "asm/ot_depth.s", PROJECT / "config/ot_depth.json"),
    ("poly_emit", PROJECT / "asm/poly_emit.s", PROJECT / "config/poly_emit.json"),
    ("subdiv_ft4", PROJECT / "asm/subdiv_ft4.s", PROJECT / "config/subdiv_ft4.json"),
    ("subdiv_ft4_mirror", PROJECT / "asm/subdiv_ft4_mirror.s", PROJECT / "config/subdiv_ft4_mirror.json"),
    ("subdiv_ft4_nocull", PROJECT / "asm/subdiv_ft4_nocull.s", PROJECT / "config/subdiv_ft4_nocull.json"),
    ("subdiv_ft4_nocull_mirror", PROJECT / "asm/subdiv_ft4_nocull_mirror.s", PROJECT / "config/subdiv_ft4_nocull_mirror.json"),
    ("subdiv_ft4_abe", PROJECT / "asm/subdiv_ft4_abe.s", PROJECT / "config/subdiv_ft4_abe.json"),
    ("subdiv_ft4_abe_mirror", PROJECT / "asm/subdiv_ft4_abe_mirror.s", PROJECT / "config/subdiv_ft4_abe_mirror.json"),
    ("subdiv_f4_abe", PROJECT / "asm/subdiv_f4_abe.s", PROJECT / "config/subdiv_f4_abe.json"),
    ("view_cells", PROJECT / "asm/view_cells.s", PROJECT / "config/view_cells.json"),
    ("sprite_anims", PROJECT / "asm/sprite_anims.s", PROJECT / "config/sprite_anims.json"),
)
# Library-variant assembly units (AGENTS.md, "Library-variant assembly"): one .s unit per Sony
# Runtime Library module that retail links as a rebuild matching no archive, admitted only
# inside the "library_asm" ranges of config/asm_regions.json (text, and data for their
# .rdata/.data/.sdata). Reported as library assembly: never matched C, never certified library.
LIBRARY_ASM_UNITS = (
    ("libspu_s_gks", PROJECT / "asm/lib/libspu_s_gks.s", PROJECT / "config/libspu_s_gks.json"),
    ("libgpu_ext", PROJECT / "asm/lib/libgpu_ext.s", PROJECT / "config/libgpu_ext.json"),
    ("libetc_intr_dma", PROJECT / "asm/lib/libetc_intr_dma.s", PROJECT / "config/libetc_intr_dma.json"),
    ("libspu_s_sra", PROJECT / "asm/lib/libspu_s_sra.s", PROJECT / "config/libspu_s_sra.json"),
    ("libspu_spu_io", PROJECT / "asm/lib/libspu_spu_io.s", PROJECT / "config/libspu_spu_io.json"),
    ("libspu_spu", PROJECT / "asm/lib/libspu_spu.s", PROJECT / "config/libspu_spu.json"),
    ("libgpu_sys", PROJECT / "asm/lib/libgpu_sys.s", PROJECT / "config/libgpu_sys.json"),
    ("libspu_s_stm", PROJECT / "asm/lib/libspu_s_stm.s", PROJECT / "config/libspu_s_stm.json"),
    ("libspu_s_srv", PROJECT / "asm/lib/libspu_s_srv.s", PROJECT / "config/libspu_s_srv.json"),
    ("libspu_s_stsa", PROJECT / "asm/lib/libspu_s_stsa.s", PROJECT / "config/libspu_s_stsa.json"),
    ("libspu_s_sk", PROJECT / "asm/lib/libspu_s_sk.s", PROJECT / "config/libspu_s_sk.json"),
    ("libspu_s_itc", PROJECT / "asm/lib/libspu_s_itc.s", PROJECT / "config/libspu_s_itc.json"),
    ("libspu_s_sr", PROJECT / "asm/lib/libspu_s_sr.s", PROJECT / "config/libspu_s_sr.json"),
    ("libspu_s_m_m", PROJECT / "asm/lib/libspu_s_m_m.s", PROJECT / "config/libspu_s_m_m.json"),
    ("libspu_s_crwa", PROJECT / "asm/lib/libspu_s_crwa.s", PROJECT / "config/libspu_s_crwa.json"),
    ("libspu_s_ini", PROJECT / "asm/lib/libspu_s_ini.s", PROJECT / "config/libspu_s_ini.json"),
    ("libspu_s_sca", PROJECT / "asm/lib/libspu_s_sca.s", PROJECT / "config/libspu_s_sca.json"),
    ("libspu_s_sva", PROJECT / "asm/lib/libspu_s_sva.s", PROJECT / "config/libspu_s_sva.json"),
    ("libcd_sys", PROJECT / "asm/lib/libcd_sys.s", PROJECT / "config/libcd_sys.json"),
    ("libcd_bios", PROJECT / "asm/lib/libcd_bios.s", PROJECT / "config/libcd_bios.json"),
    ("libcd_cdread2", PROJECT / "asm/lib/libcd_cdread2.s", PROJECT / "config/libcd_cdread2.json"),
    ("libcd_c_011", PROJECT / "asm/lib/libcd_c_011.s", PROJECT / "config/libcd_c_011.json"),
    ("libcd_c_009", PROJECT / "asm/lib/libcd_c_009.s", PROJECT / "config/libcd_c_009.json"),
    ("libcd_c_004", PROJECT / "asm/lib/libcd_c_004.s", PROJECT / "config/libcd_c_004.json"),
    ("libgte_mtx_00", PROJECT / "asm/lib/libgte_mtx_00.s", PROJECT / "config/libgte_mtx_00.json"),
    ("libgte_patchgte", PROJECT / "asm/lib/libgte_patchgte.s", PROJECT / "config/libgte_patchgte.json"),
    ("libgte_fog_00", PROJECT / "asm/lib/libgte_fog_00.s", PROJECT / "config/libgte_fog_00.json"),
    ("libspu_s_n2p", PROJECT / "asm/lib/libspu_s_n2p.s", PROJECT / "config/libspu_s_n2p.json"),
    ("libspu_s_srmp", PROJECT / "asm/lib/libspu_s_srmp.s", PROJECT / "config/libspu_s_srmp.json"),
)
_asm_spec = importlib.util.spec_from_file_location("asm_units", TOOLS / "asm_units.py")
asm_units = importlib.util.module_from_spec(_asm_spec)
_asm_spec.loader.exec_module(asm_units)
ASM_REGION = asm_units.ASM_REGION
ASM_CATEGORY = {**{entry[0]: "asm" for entry in ASM_UNITS},
                **{entry[0]: "library_asm" for entry in LIBRARY_ASM_UNITS}}
# build.py owns the project include directory (include/) and its 8.3/tree-hash rules.
_build_spec = importlib.util.spec_from_file_location("psyq_build", TOOLS / "build.py")
psyq_build = importlib.util.module_from_spec(_build_spec)
_build_spec.loader.exec_module(psyq_build)


def is_asm_unit(unit):
    """An assembly unit of either category (hand-written or library assembly)."""
    return unit in ASM_CATEGORY


def asm_category(unit):
    """"asm" or "library_asm" for an assembly unit, None for a C unit."""
    return ASM_CATEGORY.get(unit)


def validate_asm_registry(data_manifest=None):
    """Unit kinds (.c / .s) and asm placement inside each category's ranges; RuntimeError on violation."""
    data = data_manifest if data_manifest is not None else json.loads(
        (PROJECT / "config/data.json").read_text(encoding="utf-8"))
    asm_units.validate_registry(UNITS, ASM_UNITS, data, LIBRARY_ASM_UNITS)


def library_asm_text_ranges():
    """[(unit, function, start, end)] of every registered library-assembly routine, from the
    unit maps (the runtime reports use it so that no range is also claimed as library)."""
    out = []
    for unit, _, mapping in LIBRARY_ASM_UNITS:
        for name, entry in json.loads(mapping.read_text(encoding="utf-8"))["functions"].items():
            start = int(entry["address"], 0)
            out.append((unit, name, start, start + int(entry["size"])))
    return sorted(out, key=lambda r: r[2])


_symbols_spec = importlib.util.spec_from_file_location("lnk_symbols", TOOLS / "lnk_symbols.py")
lnk_symbols = importlib.util.module_from_spec(_symbols_spec)
_symbols_spec.loader.exec_module(lnk_symbols)


def validate_object_symbols(object_path, spec):
    """Ownership audit of one compiled unit (rule: tools/lnk_symbols.py): every mapped function
    is located once with its declared linkage (exported XDEF or static local symbol), every
    .text byte belongs to one mapped function, and every .sbss/.bss byte to one static_data entry.
    Initialised statics (unexported data labels of the listing) are audited by data_check.py."""
    try:
        lnk_symbols.audit(lnk_symbols.load(object_path), spec)
    except lnk_symbols.SymbolError as exc:
        raise RuntimeError(f"Compiled definitions differ from certified ownership: {exc}")


def validate_object_text_exports(object_path, functions):
    """The .text part of validate_object_symbols, for a functions map alone."""
    try:
        obj = lnk_symbols.load(object_path)
        lnk_symbols.text_partition(obj, functions, lnk_symbols.text_symbols(obj, functions))
    except lnk_symbols.SymbolError as exc:
        raise RuntimeError(f"Compiled text definitions differ from certified ownership: {exc}")


def validate_object_text_partition(obj, functions):
    """Account for every text byte, including prefixes without a symbol."""
    try:
        lnk_symbols.text_partition(obj, functions, lnk_symbols.text_symbols(obj, functions))
    except lnk_symbols.SymbolError as exc:
        raise RuntimeError(str(exc))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_function_ownership(units):
    """A function must have exactly one owning translation unit. A static function's name is
    scoped to its unit, so it is owned as (unit, name); two units may each define one."""
    owners = {}
    for unit, source, mapping in units:
        if not source.is_file() or not mapping.is_file():
            raise RuntimeError(f"unit {unit} requires source {source} and map {mapping}")
        spec = json.loads(mapping.read_text(encoding="utf-8"))
        for function, entry in spec["functions"].items():
            try:
                key = (unit, function) if lnk_symbols.is_static(entry) else function
            except lnk_symbols.SymbolError as exc:
                raise RuntimeError(f"{unit}: function {function}: {exc}")
            if key in owners:
                raise RuntimeError(
                    f"function {function} is registered in both {owners[key]} and {unit}; "
                    "choose one owning translation unit before verification")
            owners[key] = unit


def validate_symbol_bindings(units):
    """One global name cannot denote different addresses in a complete link, and a static
    (a static function or static_data entry) has no external name: its name binds nothing
    outside its unit, and no global name, in its unit or another, may bind to its storage."""
    bindings, statics = {}, []
    for unit, source, mapping in units:
        spec = json.loads(mapping.read_text(encoding="utf-8"))
        try:
            local = lnk_symbols.static_names(spec)
        except lnk_symbols.SymbolError as exc:
            raise RuntimeError(f"{unit}: {exc}")
        statics += [(address, size, name, unit) for name, (address, size) in local.items()]
        overlap = sorted(set(local) & set(spec.get("symbols", {})))
        if overlap:
            raise RuntimeError(f"{unit} maps {overlap} both as statics and as referenced globals")
        for name, value in list(spec["functions"].items()) + list(spec.get("symbols", {}).items()):
            if name in local:
                continue
            address = int(value["address"] if isinstance(value, dict) else value, 0)
            if name in bindings and bindings[name][0] != address:
                previous_address, previous_unit = bindings[name]
                raise RuntimeError(
                    f"symbol {name} maps to {hex(previous_address)} in {previous_unit} "
                    f"and {hex(address)} in {unit}; reconcile global naming before linking")
            bindings[name] = (address, unit)
    ordered = sorted(statics)
    for (a, n, first, u), (b, _, second, v) in zip(ordered, ordered[1:]):
        if b < a + n:
            raise RuntimeError(f"static {first} of {u} and static {second} of {v} overlap at {hex(b)}")
    for name, (address, unit) in sorted(bindings.items()):
        for start, size, static, owner in statics:
            if start <= address < start + max(size, 1):
                raise RuntimeError(
                    f"global {name} of {unit} binds to {hex(address)}, inside static {static} of {owner}; "
                    "a static has no external name")


def run_tree(argv, **kwargs):
    """subprocess.run that, on any interruption, kills the child's whole process tree
    (build.py and the DOSBox-X below it), not just the direct child, and waits for all of it."""
    tree = proctree.Tree(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kwargs)
    proc = tree.proc
    try:
        out, err = proc.communicate()
    except BaseException:
        tree.kill()
        tree.close()
        raise
    tree.kill()
    tree.close()
    return subprocess.CompletedProcess(argv, proc.returncode, out, err)


def invoke(script, *args):
    result = run_tree([sys.executable, str(TOOLS / script), *map(str, args)], cwd=WORKSPACE)
    for line in result.stderr.splitlines():
        if line.startswith("build.py: DOSBox-X crashed"):
            print(line, file=sys.stderr, flush=True)  # retries stay visible
    try:
        value = json.loads(result.stdout)
    except json.JSONDecodeError:
        raise RuntimeError(f"{script} failed: {result.stderr or result.stdout}")
    return result.returncode, value


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--assemblers", nargs="+", choices=["A221", "A234"], default=["A234"])
    every = UNITS + ASM_UNITS + LIBRARY_ASM_UNITS
    ap.add_argument("--units", nargs="+", choices=[x[0] for x in every], default=[x[0] for x in every],
                    help="C units (UNITS), hand-written asm units (ASM_UNITS) and library asm units "
                         "(LIBRARY_ASM_UNITS); default: all")
    ap.add_argument("--reference", type=Path, default=WORKSPACE / "games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07")
    ap.add_argument("--output", type=Path, default=PROJECT / "reports/latest.json")
    ap.add_argument("--build-root", type=Path, help="directory for build jobs (default: build.py's builds/)")
    ap.add_argument("--jobs", type=int, default=min(12, os.cpu_count() or 1), help="parallel unit builds")
    args = ap.parse_args()
    args.output = args.output.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    validate_asm_registry()
    validate_function_ownership([entry for entry in every if entry[0] in args.units])
    validate_symbol_bindings(every)
    lock = json.loads((PROJECT / "config/toolchain.lock.json").read_text())
    if digest(args.reference) != lock["reference_sha256"]:
        raise RuntimeError("reference hash differs from the supported USA SLUS_000.07 executable")
    for name, sha in lock["files"].items():
        if digest(WORKSPACE / name) != sha:
            raise RuntimeError(f"toolchain file differs from lock: {name}")
    raw = args.reference.read_bytes()
    load_address, payload_size = struct.unpack_from("<II", raw, 0x18)
    checks = []
    data_checks = []
    failed = False
    data_manifest = PROJECT / "config/data.json"
    data_bytes = data_manifest.read_bytes()
    data_sha = hashlib.sha256(data_bytes).hexdigest()
    # Shared headers are build input: their tree hash is recorded on every check, and a
    # header change while units build fails the run instead of mixing two header states.
    _, include_sha, _ = psyq_build.project_include_dir()
    selected =[entry for entry in every if entry[0] in args.units]
    if not selected:
        raise RuntimeError("empty work list: no registered unit selected")
    build_args = ("--build-root", args.build_root.resolve()) if args.build_root else ()

    def verify_unit(entry):
        unit, source, mapping = entry
        if not source.is_file() or not mapping.is_file():
            raise RuntimeError(f"unit {unit} requires source {source} and map {mapping}")
        lines, unit_checks, unit_data, unit_failed = [], [], [], False
        map_bytes = mapping.read_bytes()
        map_sha = hashlib.sha256(map_bytes).hexdigest()
        spec = json.loads(map_bytes)
        source_sha = digest(source)
        asm = is_asm_unit(unit)
        kind = asm_category(unit) or "c"
        # An assembly unit is assembled once, with the project's ASPSX 2.34 or the assembler
        # config/asm_build.json selects for it, whatever --assemblers selects for compiled C.
        for assembler in ([asm_units.ASM_ASSEMBLER] if asm else args.assemblers):
            if asm:
                code, build = invoke("build_asm.py", "--source", source, "--assembler", assembler, *build_args)
                # A unit assembled by ASPSX 2.08 or GNU as (config/asm_build.json) says so.
                lines.append(f"Assembling {unit}: {asm_units.LABELS[kind]}/{build.get('assembler', assembler)}")
                copy = "INPUT.S"
            else:
                lines.append(f"Building {unit}: C257/{assembler}")
                code, build = invoke("build.py", "--source", source, "--assembler", assembler, *build_args)
                copy = "INPUT.C"
            if code:
                raise RuntimeError(f"build failed: {build}")
            job = Path(build["job"])
            if digest(job / copy) != source_sha:
                raise RuntimeError(f"source {unit} changed during build; retry after it is stable")
            if build.get("project_include_dir", {}).get("tree_sha256") != include_sha:
                raise RuntimeError(f"include/ changed while {unit} built; retry after it is stable")
            snapshot_map = job / "symbols.json"
            snapshot_map.write_bytes(map_bytes)
            snapshot_data = job / "data_definitions.json"
            snapshot_data.write_bytes(data_bytes)
            object_file_sha = digest(Path(build["object"]))
            validate_object_symbols(Path(build["object"]), spec)
            (job / "build.json").write_text(json.dumps(build, indent=2) + "\n")
            # The listing ASPSX read: the compiler's output, or the preprocessed assembly unit
            # (an assembly unit's INPUT.S is its commented source, not what was assembled).
            listing = ("--asm", job / ("INPUT.I" if asm else "INPUT.S"))
            data_code, data = invoke("data_check.py", "--object", build["object"], "--unit", unit,
                                     "--reference", args.reference, "--symbol-map", snapshot_map,
                                     "--map", snapshot_data, *listing,
                                     "--json-report", job / "data.json")
            data.update({"assembler": build.get("assembler", assembler), "object": build["object"],
                         "source": str(source), "source_sha256": source_sha,
                         "project_include_sha256": include_sha,
                         "symbol_map": str(mapping), "map_sha256": map_sha,
                         "source_snapshot": str(job / copy), "map_snapshot": str(snapshot_map),
                         "object_file_sha256": object_file_sha, "kind": kind,
                         "data_manifest_snapshot": str(snapshot_data)})
            unit_data.append(data)
            unit_failed |= data_code != 0
            for function in spec["functions"]:
                code, check = invoke("bytecheck.py", "--object", build["object"],
                                     "--symbol-map", snapshot_map, "--reference", args.reference,
                                     "--unit", unit, "--data-map", snapshot_data, *listing,
                                     "--function", function, "--json-report", job / f"{function}.json")
                check.update({"unit": unit, "kind": kind,
                              "compiler": None if asm else "C257", "assembler": build.get("assembler", assembler),
                              "source_sha256": source_sha, "map_sha256": map_sha,
                              "project_include_sha256": include_sha,
                              "source": str(source), "symbol_map": str(mapping),
                              "source_snapshot": str(job / copy), "map_snapshot": str(snapshot_map),
                              "object_file_sha256": object_file_sha,
                              "object": build["object"], "report": str(job / f"{function}.json")})
                unit_checks.append(check)
                unit_failed |= code != 0
                lines.append(f"  {function}: {check['status']}")
        return lines, unit_checks, unit_data, unit_failed

    # Units are independent DOSBox-X jobs; results are merged in registry order.
    pool = ThreadPoolExecutor(max_workers=max(1, args.jobs))
    try:
        for lines, unit_checks, unit_data, unit_failed in pool.map(verify_unit, selected):
            print("\n".join(lines), flush=True)
            checks.extend(unit_checks)
            data_checks.extend(unit_data)
            failed |= unit_failed
    except BaseException:
        pool.shutdown(wait=False, cancel_futures=True)  # queued units never start; running ones are killed by run_tree
        raise
    else:
        pool.shutdown()
    # Count a function once only if it passes every requested assembler (an assembly unit:
    # its one ASPSX 2.34 build). C and each asm category are counted apart; asm is never C.
    def expected(unit):
        return 1 if is_asm_unit(unit) else len(args.assemblers)

    names = {(x["unit"], x["function"]) for x in checks}
    matched, matched_asm = [], {category: [] for category in asm_units.CATEGORIES}
    for key in sorted(names):
        group = [x for x in checks if (x["unit"], x["function"]) == key]
        if len(group) == expected(group[0]["unit"]) and all(x["status"] == "match" for x in group):
            (matched_asm[group[0]["kind"]] if group[0]["kind"] in matched_asm else matched).append(group[0])
    ranges = sorted((int(x["address"], 0), int(x["size"]))
                    for x in matched + [x for group in matched_asm.values() for x in group])
    for (a, n), (b, _) in zip(ranges, ranges[1:]):
        if a + n > b:
            raise RuntimeError("verified function ranges overlap; refusing progress count")
    verified_bytes = sum(int(x["size"]) for x in matched)
    verified_data, verified_asm_data = [], {category: [] for category in asm_units.CATEGORIES}
    for unit in args.units:
        group = [x for x in data_checks if x["unit"] == unit]
        if len(group) == expected(unit) and all(x["status"] == "match" for x in group):
            (verified_asm_data[asm_category(unit)] if is_asm_unit(unit) else verified_data).extend(
                group[0]["definitions"])
    asm_fields = {}
    for category, registry in (("asm", ASM_UNITS), ("library_asm", LIBRARY_ASM_UNITS)):
        tables = {}  # unit -> labels that are data tables in .text (asm_units.table_labels)
        for unit, source, mapping in registry:
            tables[unit] = asm_units.table_labels(source.read_text(encoding="utf-8"),
                                                  json.loads(mapping.read_text(encoding="utf-8")), category)
        found = matched_asm[category]
        for x in found:
            if not asm_units.inside(int(x["address"], 0), int(x["size"]), category):
                raise RuntimeError(f"{asm_units.LABELS[category]} function {x['function']} verified outside "
                                   f"its {category} text ranges")
        for x in verified_asm_data[category]:
            if not asm_units.inside(int(x["address"], 0), int(x["size"]), category, text=False):
                raise RuntimeError(f"{asm_units.LABELS[category]} datum {x['symbol']} verified outside "
                                   f"its {category} data ranges")
        table_rows = [x for x in found if x["function"] in tables.get(x["unit"], ())]
        prefix = asm_units.REPORT_PREFIX[category]
        function_bytes = sum(int(x["size"]) for x in found)
        table_bytes = sum(int(x["size"]) for x in table_rows)
        data_bytes = sum(x["size"] for x in verified_asm_data[category])
        asm_fields.update({f"{prefix}_functions": len(found),
                           f"{prefix}_routines": len(found) - len(table_rows),
                           f"{prefix}_routine_bytes": function_bytes - table_bytes,
                           f"{prefix}_tables": len(table_rows),
                           f"{prefix}_table_bytes": table_bytes,
                           f"{prefix}_data_definitions": len(verified_asm_data[category]),
                           f"{prefix}_data_bytes": data_bytes,
                           f"{prefix}_bytes": function_bytes + data_bytes})
    report = {"generated_utc": datetime.now(timezone.utc).isoformat(),
              "reference": str(args.reference.resolve()), "reference_sha256": digest(args.reference),
              "load_address": hex(load_address), "payload_size": payload_size,
              "compiler": "GCC 2.5.7", "assemblers": args.assemblers,
              "options": "-O2 -G8", "runtime": "3.3", "units": args.units,
              "data_manifest": str(data_manifest), "data_manifest_sha256": data_sha,
              "project_include_sha256": include_sha,
              "byte_matched_functions": len(matched), "byte_matched_function_bytes": verified_bytes,
              "byte_matched_data_definitions": len(verified_data),
              "byte_matched_initialized_data_bytes": sum(x["size"] for x in verified_data),
              **asm_fields,
              "asm_note": "byte_matched_asm_* is hand-written assembly (ASM_UNITS, kind asm, the GTE/renderer "
                          "region); byte_matched_library_asm_* is library assembly (LIBRARY_ASM_UNITS, kind "
                          "library_asm, Sony Runtime Library variant modules). Ranges: config/asm_regions.json. "
                          "<prefix>_functions counts every mapped label (routines + tables in .text); "
                          "<prefix>_bytes adds the unit's data definitions. Both are counted only here: "
                          "excluded from every C count, from payload_byte_coverage_percent and from the "
                          "certified library.",
              "payload_byte_coverage_percent": round(100 * verified_bytes / payload_size, 6),
              "coverage_note": "Payload includes data, padding and runtime code. This is a conservative measured byte coverage; the complete game function denominator is not established.",
              "full_decompilation": False, "all_selected_checks_pass": not failed,
              "checks": checks, "data_checks": data_checks}
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{len(matched)} C functions / {verified_bytes} bytes verified; "
          f"{asm_fields['byte_matched_asm_functions']} asm functions / {asm_fields['byte_matched_asm_bytes']} asm bytes; "
          f"{asm_fields['byte_matched_library_asm_functions']} library asm functions + "
          f"{asm_fields['byte_matched_library_asm_data_definitions']} data / "
          f"{asm_fields['byte_matched_library_asm_bytes']} library asm bytes; report: {args.output}")
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, ValueError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
