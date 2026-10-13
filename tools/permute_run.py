"""Run decomp-permuter on a directory made by tools/permute_setup.py, for a bounded time.

    python tools/permute_run.py builds/permute/f_40edc --minutes 45 -j 4 [-- permuter flags]

The permuter is the pinned checkout in ../shared-tools/decomp-permuter (commit in
docs/notes/permuter.md), run by its own venv's Python in isolated mode (-I). Two seams adapt
it to this host without editing it:
  * its preprocess step calls a host ``cpp``; base.c is already preprocessed by CPPPSX, so the
    launcher replaces that step with reading the file;
  * its compile step runs ``bash compile.sh``; Git for Windows' bash is put first on PATH.
Scores are taken with --stack-diffs, since frame layout is part of the match. The whole
process tree (permuter workers and their DOSBox-X jobs) is killed when the time is up.
Workers are capped at 4 because every compile is a DOSBox-X process and other lanes share
the machine.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading

TOOLS = Path(__file__).resolve().parent
PERMUTER = TOOLS.parents[1] / "shared-tools" / "decomp-permuter"
VENV_PYTHON = PERMUTER / "venv" / "Scripts" / "python.exe"
MAX_WORKERS = 4
# The decomp-permuter commit this glue was written and validated against.
PINNED_COMMIT = "8556c81d80d1c1af98a858c8f4dc951357f29139"

if __name__ in ("__main__", "__mp_main__") and "--inner" in sys.argv:
    # Inside the permuter's venv: also runs in every spawned worker on Windows.
    sys.path.insert(0, str(PERMUTER))
    import src.main as permuter_main  # noqa: E402

    def _read_preprocessed(filename, cpp_args=()):
        with open(filename, encoding="utf-8") as f:
            return f.read()

    permuter_main.preprocess = _read_preprocessed


def outer(argv: list[str]) -> int:
    import argparse
    sys.path.insert(0, str(TOOLS))
    import proctree
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("directory", type=Path)
    ap.add_argument("--minutes", type=float, required=True)
    ap.add_argument("-j", "--jobs", type=int, default=MAX_WORKERS)
    ap.add_argument("--quiet", action="store_true", help="write progress to <directory>/run.log only")
    rest = argv[argv.index("--") + 1:] if "--" in argv else []
    argv = argv[:argv.index("--")] if "--" in argv else argv
    a = ap.parse_args(argv)
    if not 1 <= a.jobs <= MAX_WORKERS:
        ap.error(f"-j must be 1..{MAX_WORKERS}")
    if not VENV_PYTHON.is_file():
        raise SystemExit(f"error: no permuter venv at {VENV_PYTHON}; see docs/notes/permuter.md")
    head = subprocess.run(["git", "-C", str(PERMUTER), "rev-parse", "HEAD"], capture_output=True, text=True)
    if head.stdout.strip() != PINNED_COMMIT:
        raise SystemExit(f"error: {PERMUTER} is at {head.stdout.strip() or '?'}, not the pinned {PINNED_COMMIT}")
    dirty = subprocess.run(["git", "-C", str(PERMUTER), "status", "--porcelain", "--untracked-files=no"],
                           capture_output=True, text=True)
    if dirty.stdout.strip():
        raise SystemExit(f"error: {PERMUTER} has local modifications")
    env = dict(os.environ)
    git_bash = Path(r"C:\Program Files\Git\usr\bin")
    if (git_bash / "bash.exe").is_file():
        env["PATH"] = str(git_bash) + os.pathsep + env.get("PATH", "")
    if not shutil.which("bash", path=env["PATH"]):
        raise SystemExit("error: no bash on PATH (install Git for Windows)")
    cmd = [str(VENV_PYTHON), "-I", str(Path(__file__).resolve()), "--inner",
           str(a.directory.resolve()), "-j", str(a.jobs), "--stack-diffs", *rest]
    # The venv launcher drops inherited console handles under CREATE_NO_WINDOW, so the output
    # is piped and relayed, and also kept in <directory>/run.log.
    tree = proctree.Tree(cmd, cwd=str(a.directory.resolve()), env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log = open(a.directory / "run.log", "ab")

    def relay():
        for chunk in iter(lambda: tree.proc.stdout.read1(4096), b""):
            log.write(chunk)
            log.flush()
            if not a.quiet:
                sys.stdout.buffer.write(chunk)
                sys.stdout.flush()

    reader = threading.Thread(target=relay, daemon=True)
    reader.start()
    try:
        tree.proc.wait(timeout=a.minutes * 60)
    except subprocess.TimeoutExpired:
        print(f"\npermute_run: {a.minutes} minutes elapsed; stopping", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        tree.kill()
        tree.close()
        reader.join(timeout=5)
        log.close()
        shutil.rmtree(a.directory / "jobs", ignore_errors=True)  # compiles killed mid-flight
    outputs = sorted(a.directory.glob("output-*"), key=lambda p: int(p.name.split("-")[1]))
    print(f"permute_run: best outputs: {[p.name for p in outputs[:5]]}")
    return 0


if __name__ == "__main__":
    if "--inner" in sys.argv:
        sys.argv.remove("--inner")
        sys.argv[0] = str(PERMUTER / "permuter.py")
        permuter_main.main()
    else:
        raise SystemExit(outer(sys.argv[1:]))
