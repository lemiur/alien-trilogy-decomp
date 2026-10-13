"""Run the DOS PsyQ compiler in an isolated job directory.

DOSBox-X captures emulator logs and the runner redirects each tool's stdout.
Its built-in DOS shell did not reliably support per-command stderr redirection,
so guest compiler stderr is not separately captured.

Headers: the Runtime 3.3 include directory pinned by tree hash in
config/toolchain.lock.json (``selected_include_dir``) is verified, mounted
read-only as drive E: and passed to CPPPSX as an include path, so units write
``#include <sys/types.h>``, ``<LIBGTE.H>``, ``<LIBGPU.H>`` and so on. CPPPSX gets
the 3.3 CCPSX driver's C predefines (including ``LANGUAGE_C``) from PREDEF.H.

Project headers: ``include/`` (shared type definitions, one class one definition) is
mounted read-only as drive F: and passed to CPPPSX after the SDK path, so
``#include "camera.h"`` and ``#include <camera.h>`` both resolve while an SDK header of
the same name keeps precedence. DOS sees 8.3 names only, so every file under include/
must have an 8.3 name; a longer name fails the build instead of being mangled. The
directory's tree hash is recorded in the build report (``project_include_dir``).

Crash versus error: the guest batch file writes STATUS.TXT ("OK" or "ERROR:<stage>").
A real compile, assemble or link error always leaves an "ERROR:" status and is
reported at once, never retried. A toolchain *crash* (DOSBox-X dying with an abnormal
host exit code such as 0xC0000409 = 3221226505 when many builds run at once) leaves
no STATUS.TXT and a non-zero exit code. Only crashes are retried, at most
``--retries`` times (default 2), each in a fresh job directory, and every retry is
logged to stderr. A retry can only repeat the build; the caller still judges the
result from exit codes and, for the gate, from the final hash, so a retry cannot
admit a wrong result. If DOSBox-X is interrupted or times out its whole process
tree is killed before build.py exits.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proctree  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
PROJECT = Path(__file__).resolve().parents[1]
LOCK = PROJECT / "config" / "toolchain.lock.json"
DEFAULT_TOOLS = ROOT / "toolchain"
# The pinned SDK include directory is mounted read-only as this DOS drive and
# passed to CPPPSX with -I, so units can #include <LIBGPU.H> and friends.
INCLUDE_DRIVE = "E"
# The project's shared headers (include/) are mounted read-only as this drive and
# passed to CPPPSX after the SDK path, so SDK names keep precedence.
PROJECT_INCLUDE = PROJECT / "include"
PROJECT_INCLUDE_DRIVE = "F"
# A DOS 8.3 name: 1-8 name characters and an optional extension of 1-3.
DOS_NAME = re.compile(r"[A-Za-z0-9_$~!#%&'()@^`{}-]{1,8}(?:\.[A-Za-z0-9_$~!#%&'()@^`{}-]{1,3})?")
# Preprocessor predefines that the Runtime 3.3 driver (PSYQ/CCPSX.EXE of DTL-S2190)
# passes to CPPPSX for a C source; the SDK headers test LANGUAGE_C. __OPTIMIZE__
# is added when the options optimize. They reach CPPPSX through -imacros because
# the DOS command tail is limited to 127 bytes.
CPP_PREDEFINES = ("__GNUC__=2", "mips", "__mips__", "__mips", "psx", "__psx__", "__psx",
                  "__EXTENSIONS__", "_MIPSEL", "__CHAR_UNSIGNED__", "_LANGUAGE_C", "LANGUAGE_C")
COMPILERS = ("C257", "C260A", "C260B", "C272", "C272S16")
ASSEMBLERS = ("A107", "A110", "AFLOP", "A221", "A234", "A256D")


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def kill_tree(proc: subprocess.Popen) -> None:
    """Terminate proc and everything it spawned, then reap it."""
    if proc.poll() is None:
        if sys.platform == "win32":
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        else:
            proc.kill()
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def is_crash(returncode: int, status_text: str) -> bool:
    """A toolchain crash: no tool result written and an abnormal host exit code."""
    return status_text == "MISSING" and returncode != 0


def run_dosbox(dosbox: Path, conf: Path, job: Path) -> subprocess.CompletedProcess:
    tree = proctree.Tree([str(dosbox), "-conf", str(conf), "-silent"], cwd=str(job),
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    proc = tree.proc
    try:
        out, err = proc.communicate(timeout=300)
    except BaseException as exc:  # timeout, KeyboardInterrupt, SystemExit: never leave DOSBox-X behind
        tree.kill()
        out, err = proc.communicate()
        tree.close()
        if isinstance(exc, subprocess.TimeoutExpired):
            raise subprocess.TimeoutExpired(proc.args, 300, out, err)
        raise
    tree.kill()  # a DOSBox-X that outlived its launcher must not outlive the build
    tree.close()
    return subprocess.CompletedProcess(proc.args, proc.returncode, out, err)


def tree_sha256(root: Path) -> tuple[str, int]:
    """Hash a directory as toolchain.lock.json defines tree_sha256."""
    files = sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: p.relative_to(root).as_posix())
    lines = b"".join(p.relative_to(root).as_posix().encode() + b"\0" +
                     hashlib.sha256(p.read_bytes()).hexdigest().encode() + b"\n" for p in files)
    return hashlib.sha256(lines).hexdigest(), len(files)


def pinned_include_dir() -> tuple[str, Path, str]:
    """Return (name, path, tree hash) of the lock's selected SDK include directory, verified."""
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    name = lock.get("selected_include_dir")
    entry = lock.get("include_dirs", {}).get(name) if name else None
    if not entry:
        fail(f"{LOCK} selects no pinned include directory")
    path = (ROOT / entry["path"]).resolve()
    if not path.is_dir():
        fail(f"pinned include directory {name} does not exist: {path}")
    digest, count = tree_sha256(path)
    if digest != entry["tree_sha256"] or count != entry["files"]:
        fail(f"include directory {name} differs from lock: {count} files, tree sha256 {digest}")
    return name, path, digest


def check_dos_names(root: Path) -> None:
    """Fail unless every file and directory below root has a DOS 8.3 name."""
    for path in sorted(root.rglob("*")):
        for part in path.relative_to(root).parts:
            if not DOS_NAME.fullmatch(part):
                fail(f"{path}: project header names must be DOS 8.3 (got {part!r})")


def project_include_dir(root: Path = PROJECT_INCLUDE) -> tuple[Path, str, int]:
    """Return (path, tree sha256, file count) of the project include directory, 8.3-checked."""
    root = root.resolve()
    if not root.is_dir():
        fail(f"project include directory does not exist: {root}")
    check_dos_names(root)
    digest, count = tree_sha256(root)
    return root, digest, count


def prepare_job(args, source: Path, tools: Path, include: Path,
                project_include: Path = PROJECT_INCLUDE) -> Path:
    """Create a fresh job directory holding the input, RUN.BAT and dosbox.conf."""
    job = Path(tempfile.mkdtemp(prefix="job-", dir=args.build_root)).resolve()
    shutil.copyfile(source, job / "INPUT.C")
    predefines = list(CPP_PREDEFINES)
    if re.search(r"(?:^|\s)-O(?![0-9])|(?:^|\s)-O[1-9]", args.opt):
        predefines.insert(1, "__OPTIMIZE__")
    (job / "PREDEF.H").write_text(
        "".join("#define {} {}\r\n".format(*(d.split("=", 1) + ["1"])[:2]) for d in predefines),
        encoding="ascii", newline="")
    (job / "RUN.BAT").write_text(
        "@echo off\r\n"
        "set PATH=C:\\BIN\r\n"
        "echo CPP>STAGE.TXT\r\n"
        f"C:\\{args.compiler}\\CPPPSX -undef -imacros PREDEF.H -I{INCLUDE_DRIVE}:\\ -I{PROJECT_INCLUDE_DRIVE}:\\ "
        "INPUT.C INPUT.I > CPP.LOG\r\n"
        "if errorlevel 1 goto failed_cpp\r\n"
        "echo CC1>STAGE.TXT\r\n"
        f"C:\\{args.compiler}\\CC1PSX {args.opt} INPUT.I -o INPUT.S > CC1.LOG\r\n"
        "if errorlevel 1 goto failed_cc1\r\n"
        "echo AS>STAGE.TXT\r\n"
        f"C:\\{args.assembler}\\ASPSX -q INPUT.S -o INPUT.OBJ > AS.LOG\r\n"
        "if errorlevel 1 goto failed_as\r\n"
        "echo OK>STATUS.TXT\r\n"
        "goto done\r\n"
        ":failed_cpp\r\n"
        "echo ERROR:CPP>STATUS.TXT\r\n"
        "goto done\r\n"
        ":failed_cc1\r\n"
        "echo ERROR:CC1>STATUS.TXT\r\n"
        "goto done\r\n"
        ":failed_as\r\n"
        "echo ERROR:AS>STATUS.TXT\r\n"
        ":done\r\n",
        encoding="ascii", newline="")

    conf = job / "dosbox.conf"
    conf.write_text(
        "[sdl]\noutput=surface\n\n[dosbox]\nmemsize=64\n\n[cpu]\ncore=dynamic\ncycles=max\n\n"
        "[dos]\nxms=true\nems=true\numb=true\n\n[autoexec]\n"
        f'mount c "{tools}" -ro\nmount d "{job}"\nmount {INCLUDE_DRIVE.lower()} "{include}" -ro\n'
        f'mount {PROJECT_INCLUDE_DRIVE.lower()} "{project_include}" -ro\n'
        "d:\ncall RUN.BAT\nexit\n", encoding="ascii")
    return job


def main() -> int:
    start = time.monotonic()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", required=True, type=Path)
    ap.add_argument("--compiler", choices=COMPILERS, default="C257")
    ap.add_argument("--assembler", choices=ASSEMBLERS, default="A234")
    ap.add_argument("--opt", default="-O2 -G8", help="compiler options (default: -O2 -G8)")
    ap.add_argument("--tool-root", type=Path, default=DEFAULT_TOOLS)
    ap.add_argument("--dosbox", type=Path, default=Path(r"C:\DOSBox-X\dosbox-x.exe"))
    ap.add_argument("--build-root", type=Path, default=Path(__file__).resolve().parents[1] / "builds")
    ap.add_argument("--retries", type=int, default=2,
                    help="retry a DOSBox-X crash (never a compile error) up to N times (default 2)")
    ap.add_argument("--json-report", type=Path)
    args = ap.parse_args()

    source = args.source.resolve()
    tools = args.tool_root.resolve()
    dosbox = args.dosbox.resolve()
    for p in (source, dosbox, tools / args.compiler / "CPPPSX.EXE",
              tools / args.compiler / "CC1PSX.EXE", tools / args.assembler / "ASPSX.EXE"):
        if not p.is_file():
            fail(f"required file does not exist: {p}")
    if not re.fullmatch(r"(?:-[A-Za-z0-9]+(?:=[A-Za-z0-9]+)?\s*)+", args.opt):
        fail("--opt accepts whitespace-separated compiler flags containing letters, digits, and '=' only")

    args.build_root = args.build_root.resolve()
    args.build_root.mkdir(parents=True, exist_ok=True)
    include_name, include, include_sha = pinned_include_dir()
    project_include, project_sha, project_files = project_include_dir()
    attempts = max(0, args.retries) + 1
    for attempt in range(1, attempts + 1):
        job = prepare_job(args, source, tools, include, project_include)
        try:
            proc = run_dosbox(dosbox, job / "dosbox.conf", job)
        except subprocess.TimeoutExpired as exc:
            (job / "dosbox.stdout.log").write_bytes(exc.stdout or b"")
            (job / "dosbox.stderr.log").write_bytes(exc.stderr or b"")
            fail(f"DOSBox-X timed out; isolated job retained at {job}")
        (job / "dosbox.stdout.log").write_bytes(proc.stdout)
        (job / "dosbox.stderr.log").write_bytes(proc.stderr)
        status = job / "STATUS.TXT"
        status_text = status.read_text(errors="replace").strip() if status.is_file() else "MISSING"
        if is_crash(proc.returncode, status_text) and attempt < attempts:
            print(f"build.py: DOSBox-X crashed (exit {proc.returncode}, no tool result) on attempt "
                  f"{attempt}/{attempts} for {source.name}; retrying in a fresh job directory "
                  f"(crashed job {job.name})", file=sys.stderr, flush=True)
            shutil.rmtree(job, ignore_errors=True)
            continue
        if proc.returncode != 0 or status_text != "OK":
            kind = "crashed" if is_crash(proc.returncode, status_text) else "failed"
            fail(f"DOS toolchain {kind} at {status_text} (exit {proc.returncode}); inspect per-tool stdout and DOSBox logs in {job}")
        break
    obj = job / "INPUT.OBJ"
    if not obj.is_file() or obj.stat().st_size < 4:
        fail(f"toolchain reported success without a valid object: {job}")
    if project_include_dir()[1] != project_sha:
        fail(f"project headers changed during the build; retry once include/ is stable ({job})")
    report = {"job": str(job), "source": str(source), "compiler": args.compiler,
              "assembler": args.assembler, "options": args.opt, "object": str(obj),
              "include_dir": {"name": include_name, "path": str(include), "tree_sha256": include_sha},
              "project_include_dir": {"path": str(project_include), "tree_sha256": project_sha,
                                      "files": project_files},
              "assembly": str(job / "INPUT.S"), "preprocessed": str(job / "INPUT.I"),
              "logs": {"cpp": str(job / "CPP.LOG"), "compiler": str(job / "CC1.LOG"),
                       "assembler": str(job / "AS.LOG"),
                       "dosbox_stdout": str(job / "dosbox.stdout.log"), "dosbox_stderr": str(job / "dosbox.stderr.log")},
              "elapsed_seconds": round(time.monotonic() - start, 3)}
    serialized = json.dumps(report, indent=2)
    print(serialized)
    if args.json_report:
        report_path = args.json_report.resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(serialized + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
