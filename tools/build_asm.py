"""Assemble one hand-written ``.s`` unit with ASPSX under DOSBox-X in an isolated job directory.

Assembly is admitted only inside the ranges of ``config/asm_regions.json`` (AGENTS.md,
"Hand-written assembly" and "Library-variant assembly"); ``tools/asm_units.py``, ``tools/gate.py``
and ``tools/verify.py`` enforce the placement and count the bytes per category ("asm" or
"library_asm"), never as matched C. This tool only assembles.

The unit goes through the two stages the PsyQ driver gives a ``.s`` file: the C preprocessor
(``CPPPSX -undef``, from the locked C257 toolchain) and then ``ASPSX -q``, the same assembler
invocation tools/build.py uses for compiled C. Unlike a C source it gets none of the driver's
C predefines (``<asm.h>`` must not see LANGUAGE_C). The preprocessor supplies, from the lock's
pinned Runtime Library 3.3 include directory (``build.pinned_include_dir``), the ``<asm.h>``
register names (``v0``, ``a3``, ``t5`` ...; ASPSX itself only accepts ``$n``) and the
unit's own ``#include "..."`` headers from the unit's directory; comments are written as
``/* */`` so the preprocessor removes them. Every input line must end in CRLF (ASPSX reads DOS
text). The job keeps INPUT.S (the unit, byte for byte), INPUT.I (what ASPSX assembled) and
INPUT.OBJ.

DOSBox-X handling (crash retry, process-tree kill, exit-code verdict) is build.py's, imported
rather than copied; build.py itself is not modified.

The assembler is chosen per unit by ``config/asm_build.json`` (``units``, keyed by the unit's
path relative to the project, each with a ``reason``; a unit not listed gets ASPSX):
``"ASPSX"`` (default: ``--assembler``, ASPSX 2.34 for every unit verify.py and the gate build),
``"A208"`` or ``"GNU_AS"``. The whole file is validated on every run: an unknown assembler or
option, or a listed unit without a reason, is refused. Every DOS assembler a run uses is
verified first against config/toolchain.lock.json (its name in ``assemblers`` and the sha256 of
``toolchain/<name>/ASPSX.EXE`` in ``files``).

A208 is ASPSX 2.08 (Runtime Library 3.0 tools), only for a library-variant unit whose retail
bytes are its output: its signed ``div``/``rem`` macros end in the overflow trap
``tge zero, zero, 0x5d`` (0x00001770) where 2.34 writes ``break 6``, and it inserts no hazard
``nop`` under ``.set noreorder``. It is run exactly like the default ASPSX, from
``toolchain/A208``.

GNU_AS is only for a library unit that no pinned ASPSX can express: an absolute reference to
a ``.sbss`` block (ASPSX addresses those $gp-relative). Such a unit is preprocessed by
CPPPSX as above, assembled on the host by the GNU assembler pinned in
config/toolchain.lock.json (``gnu_as``: path, version, options, sha256 of the executable and
of the release zip, verified before every run), and its ELF object (INPUT.ELF) is converted by
tools/elf_lnk.py into the PsyQ LNK object INPUT.OBJ that every other tool reads. Any
assembler output (a warning) fails the build, as an ASPSX warning does. The job keeps
INPUT.ELF and AS.LOG beside INPUT.OBJ.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import time

_spec = importlib.util.spec_from_file_location("psyq_build", Path(__file__).resolve().parent / "build.py")
build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(build)

_spec = importlib.util.spec_from_file_location("elf_lnk", Path(__file__).resolve().parent / "elf_lnk.py")
elf_lnk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(elf_lnk)

ASSEMBLERS = build.ASSEMBLERS
fail = build.fail
UNIT_DRIVE = "G"  # the unit's own directory, for its #include "..." headers
ASM_BUILD = build.PROJECT / "config" / "asm_build.json"
UNIT_ASSEMBLERS = ("ASPSX", "A208", "GNU_AS")
# Unit assemblers that name a DOS ASPSX directory of the toolchain (run like the default).
UNIT_DOS_ASSEMBLERS = ("A208",)


def load_unit_config(config: Path = ASM_BUILD) -> dict:
    """The validated ``units`` table of config/asm_build.json ({} when the file is absent)."""
    if not config.is_file():
        return {}
    table = json.loads(config.read_text(encoding="utf-8"))
    unknown = set(table) - {"units"}
    if unknown:
        fail(f"{config}: unknown keys {sorted(unknown)}")
    entries = table.get("units", {})
    for key, entry in entries.items():
        if not isinstance(entry, dict):
            fail(f"{config}: entry for {key} is not an object")
        unknown = set(entry) - {"assembler", "reason"}
        if unknown:
            fail(f"{config}: unknown options {sorted(unknown)} for {key}")
        assembler = entry.get("assembler", "ASPSX")
        if assembler not in UNIT_ASSEMBLERS:
            fail(f"{config}: assembler {assembler!r} for {key} is not one of {UNIT_ASSEMBLERS}")
        if not entry.get("reason"):
            fail(f"{config}: {key} needs a 'reason' for its non-default options")
    return entries


def unit_options(source: Path, config: Path = ASM_BUILD) -> dict:
    """{"assembler": "ASPSX"|"A208"|"GNU_AS"} of one unit (module docstring)."""
    entries = load_unit_config(config)
    try:
        key = source.resolve().relative_to(build.PROJECT.resolve()).as_posix()
    except ValueError:
        key = None
    entry = entries.get(key, {}) if key else {}
    return {"assembler": entry.get("assembler", "ASPSX")}


def dos_assembler(options: dict, default: str) -> str | None:
    """The toolchain directory of the DOS ASPSX a unit runs (None for GNU_AS)."""
    if options["assembler"] == "GNU_AS":
        return None
    return options["assembler"] if options["assembler"] in UNIT_DOS_ASSEMBLERS else default


def pinned_dos_assembler(name: str, tools: Path = build.DEFAULT_TOOLS, lock_path: Path = build.LOCK) -> Path:
    """``tools/<name>/ASPSX.EXE`` after verifying it against the lock (module docstring)."""
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    if name not in lock.get("assemblers", {}):
        fail(f"{lock_path} pins no assembler {name!r} (assemblers)")
    key = f"toolchain/{name}/ASPSX.EXE"
    expected = lock.get("files", {}).get(key)
    if not expected:
        fail(f"{lock_path} has no sha256 for {key} (files)")
    path = tools / name / "ASPSX.EXE"
    if not path.is_file():
        fail(f"required file does not exist: {path}")
    digest = build.hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != expected:
        fail(f"{path} differs from the lock: sha256 {digest}")
    return path


def pinned_gnu_as() -> tuple[Path, list, dict]:
    """(executable, options, lock entry) of the lock's GNU assembler, after verifying its hashes."""
    lock = json.loads(build.LOCK.read_text(encoding="utf-8"))
    entry = lock.get("gnu_as")
    if not entry:
        fail(f"{build.LOCK} pins no GNU assembler (gnu_as)")
    for key in ("path", "zip"):
        path = build.ROOT / entry[key]
        if not path.is_file():
            fail(f"pinned GNU assembler file does not exist: {path}")
        digest = build.hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != entry[f"{key}_sha256"]:
            fail(f"{path} differs from the lock: sha256 {digest}")
    return build.ROOT / entry["path"], list(entry["options"]), entry


def gnu_assemble(job: Path) -> dict:
    """Assemble the job's INPUT.I with the pinned GNU assembler and convert it to INPUT.OBJ."""
    executable, options, entry = pinned_gnu_as()
    proc = build.subprocess.run([str(executable), *options, "-o", "INPUT.ELF", "INPUT.I"], cwd=job,
                                stdout=build.subprocess.PIPE, stderr=build.subprocess.STDOUT, timeout=120)
    (job / "AS.LOG").write_bytes(proc.stdout)
    if proc.returncode != 0 or proc.stdout.strip():
        fail(f"GNU assembler failed (exit {proc.returncode}): "
             f"{proc.stdout.decode(errors='replace').strip()[-800:]}; inspect logs in {job}")
    try:
        (job / "INPUT.OBJ").write_bytes(elf_lnk.convert((job / "INPUT.ELF").read_bytes()))
    except (elf_lnk.ElfError, ValueError, IndexError, struct.error) as exc:
        fail(f"elf_lnk.py: {exc} ({job})")
    return {"version": entry["version"], "path": entry["path"], "options": options,
            "elf": str(job / "INPUT.ELF")}


def prepare_job(args, source: Path, tools: Path, include: Path,
                project_include: Path = build.PROJECT_INCLUDE, options: dict | None = None) -> Path:
    """Create a fresh job directory holding INPUT.S, RUN.BAT and dosbox.conf.

    C: is the toolchain, D: the job, build.INCLUDE_DRIVE the lock's pinned Runtime 3.3
    include directory, build.PROJECT_INCLUDE_DRIVE the project's include/ and UNIT_DRIVE
    the unit's own directory (all read-only). Search order: the unit's directory, the
    SDK, then the project headers. The DOS assembler is args.assembler, or the unit's own
    (A208); a GNU_AS unit's batch stops after the preprocessor.
    """
    options = options or {"assembler": "ASPSX"}
    name = dos_assembler(options, args.assembler)
    assemble = (f"C:\\{name}\\ASPSX -q INPUT.I -o INPUT.OBJ > AS.LOG\r\n"
                "if errorlevel 1 goto failed_as\r\n") if name else ""
    job = Path(tempfile.mkdtemp(prefix="asm-", dir=args.build_root)).resolve()
    shutil.copyfile(source, job / "INPUT.S")
    (job / "RUN.BAT").write_text(
        "@echo off\r\n"
        "set PATH=C:\\BIN\r\n"
        "echo CPP>STAGE.TXT\r\n"
        f"C:\\C257\\CPPPSX -undef -I{UNIT_DRIVE}:\\ -I{build.INCLUDE_DRIVE}:\\ "
        f"-I{build.PROJECT_INCLUDE_DRIVE}:\\ INPUT.S INPUT.I > CPP.LOG\r\n"
        "if errorlevel 1 goto failed_cpp\r\n"
        "echo AS>STAGE.TXT\r\n"
        f"{assemble}"
        "echo OK>STATUS.TXT\r\n"
        "goto done\r\n"
        ":failed_cpp\r\n"
        "echo ERROR:CPP>STATUS.TXT\r\n"
        "goto done\r\n"
        ":failed_as\r\n"
        "echo ERROR:AS>STATUS.TXT\r\n"
        ":done\r\n",
        encoding="ascii", newline="")
    (job / "dosbox.conf").write_text(
        "[sdl]\noutput=surface\n\n[dosbox]\nmemsize=64\n\n[cpu]\ncore=dynamic\ncycles=max\n\n"
        "[dos]\nxms=true\nems=true\numb=true\n\n[autoexec]\n"
        f'mount c "{tools}" -ro\nmount d "{job}"\nmount {build.INCLUDE_DRIVE.lower()} "{include}" -ro\n'
        f'mount {build.PROJECT_INCLUDE_DRIVE.lower()} "{project_include}" -ro\n'
        f'mount {UNIT_DRIVE.lower()} "{source.parent}" -ro\nd:\ncall RUN.BAT\nexit\n', encoding="ascii")
    return job


def main() -> int:
    start = time.monotonic()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", required=True, type=Path, help="hand-written .s unit")
    ap.add_argument("--assembler", choices=ASSEMBLERS, default="A234")
    ap.add_argument("--tool-root", type=Path, default=build.DEFAULT_TOOLS)
    ap.add_argument("--dosbox", type=Path, default=Path(r"C:\DOSBox-X\dosbox-x.exe"))
    ap.add_argument("--build-root", type=Path, default=Path(__file__).resolve().parents[1] / "builds")
    ap.add_argument("--retries", type=int, default=2,
                    help="retry a DOSBox-X crash (never an assembler error) up to N times (default 2)")
    ap.add_argument("--json-report", type=Path)
    args = ap.parse_args()

    source = args.source.resolve()
    tools = args.tool_root.resolve()
    dosbox = args.dosbox.resolve()
    if source.suffix.lower() != ".s":
        fail(f"an assembly unit must be a .s file: {source}")
    for p in (source, dosbox, tools / "C257" / "CPPPSX.EXE"):
        if not p.is_file():
            fail(f"required file does not exist: {p}")
    _, include, _ = build.pinned_include_dir()  # verified against config/toolchain.lock.json
    project_include, project_sha, project_files = build.project_include_dir()
    for path in [source, *sorted(source.parent.glob("*.h"))]:
        text = path.read_bytes().replace(b"\r\n", b"")
        if b"\n" in text or b"\r" in text:
            fail(f"{path.name}: ASPSX reads DOS text; every line must end in CRLF")

    options = unit_options(source)
    assembler = dos_assembler(options, args.assembler)
    if assembler:
        pinned_dos_assembler(assembler, tools)  # verified before the build starts
    else:
        pinned_gnu_as()  # verified before the build starts

    args.build_root = args.build_root.resolve()
    args.build_root.mkdir(parents=True, exist_ok=True)
    attempts = max(0, args.retries) + 1
    for attempt in range(1, attempts + 1):
        job = prepare_job(args, source, tools, include, project_include, options)
        try:
            proc = build.run_dosbox(dosbox, job / "dosbox.conf", job)
        except build.subprocess.TimeoutExpired as exc:
            (job / "dosbox.stdout.log").write_bytes(exc.stdout or b"")
            (job / "dosbox.stderr.log").write_bytes(exc.stderr or b"")
            fail(f"DOSBox-X timed out; isolated job retained at {job}")
        (job / "dosbox.stdout.log").write_bytes(proc.stdout)
        (job / "dosbox.stderr.log").write_bytes(proc.stderr)
        status = job / "STATUS.TXT"
        status_text = status.read_text(errors="replace").strip() if status.is_file() else "MISSING"
        if build.is_crash(proc.returncode, status_text) and attempt < attempts:
            print(f"build.py: DOSBox-X crashed (exit {proc.returncode}, no tool result) on attempt "
                  f"{attempt}/{attempts} for {source.name}; retrying in a fresh job directory "
                  f"(crashed job {job.name})", file=sys.stderr, flush=True)
            shutil.rmtree(job, ignore_errors=True)
            continue
        if proc.returncode != 0 or status_text != "OK":
            kind = "crashed" if build.is_crash(proc.returncode, status_text) else "failed"
            logs = " ".join((job / log).read_text(errors="replace").strip()[-800:]
                            for log in ("CPP.LOG", "AS.LOG") if (job / log).is_file())
            fail(f"DOS assembler {kind} at {status_text} (exit {proc.returncode}): {logs} "
                 f"; inspect logs in {job}")
        break
    gnu = gnu_assemble(job) if options["assembler"] == "GNU_AS" else None
    obj = job / "INPUT.OBJ"
    if not obj.is_file() or obj.stat().st_size < 4:
        fail(f"assembler reported success without a valid object: {job}")
    if build.project_include_dir()[1] != project_sha:
        fail(f"project headers changed during the build; retry once include/ is stable ({job})")
    report = {"job": str(job), "source": str(source), "kind": "asm", "assembler": assembler,
              "object": str(obj),
              "project_include_dir": {"path": str(project_include), "tree_sha256": project_sha,
                                      "files": project_files}, "assembly": str(job / "INPUT.I"), "source_copy": str(job / "INPUT.S"),
              "logs": {"preprocessor": str(job / "CPP.LOG"), "assembler": str(job / "AS.LOG"),
                       "dosbox_stdout": str(job / "dosbox.stdout.log"),
                       "dosbox_stderr": str(job / "dosbox.stderr.log")},
              "elapsed_seconds": round(time.monotonic() - start, 3)}
    if gnu:
        report["assembler"] = "GNU_AS"
        report["gnu_as"] = gnu
    serialized = json.dumps(report, indent=2)
    print(serialized)
    if args.json_report:
        report_path = args.json_report.resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(serialized + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
