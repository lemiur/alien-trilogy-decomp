"""Run the report chain in its one valid order and stop at the first failure.

    1. tools/gate.py            whole-executable gate; writes builds/gate/provenance.json
    2. tools/verify.py          independent per-function witness; writes reports/latest.json
    3. analysis/runtime_symbols.py, runtime_relocations.py, runtime_data.py
                                Sony Runtime Library 3.3 certification
    4. analysis/link_plan.py    range union and uncovered gaps; writes reports/link_plan.json
    5. tools/rules.py --report-only --match-only
                                rule check: [match] findings and matched under the rules
                                (never fails the chain; progress.py repeats the count)
    6. tools/progress.py        counts from the gate record, cross-checks steps 2-4;
                                writes reports/progress.json and PROGRESS.md

The gate runs first because it refuses a dirty tree and step 3 rewrites tracked evidence
(analysis/runtime_*.json, config/runtime_bindings.json). Any such change is listed at the
end: it is new evidence to review and commit, not something this tool decides.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
TRACKED_OUTPUTS = ("analysis/runtime_symbols.json", "analysis/runtime_linked.json",
                   "config/runtime_bindings.json")


def steps(jobs, skip_gate):
    out = [] if skip_gate else [["tools/gate.py", "--jobs", str(jobs)]]
    out += [["tools/verify.py", "--jobs", str(jobs)],
            ["analysis/runtime_symbols.py"], ["analysis/runtime_relocations.py"],
            ["analysis/runtime_data.py"], ["analysis/link_plan.py"],
            ["tools/rules.py", "--report-only", "--match-only"], ["tools/progress.py"]]
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jobs", type=int, default=min(12, os.cpu_count() or 1))
    ap.add_argument("--skip-gate", action="store_true",
                    help="reuse builds/gate from an earlier gate run; progress.py still refuses a "
                         "record built from other sources than reports/latest.json")
    args = ap.parse_args(argv)
    for script, *rest in steps(args.jobs, args.skip_gate):
        print(f"== {script} {' '.join(rest)}".rstrip(), flush=True)
        code = subprocess.run([sys.executable, str(PROJECT / script), *rest], cwd=PROJECT).returncode
        if code:
            print(f"reports: {script} exited {code}; stopping", file=sys.stderr)
            return code
    changed = subprocess.run(["git", "-C", str(PROJECT), "status", "--porcelain", "--", *TRACKED_OUTPUTS],
                             capture_output=True, text=True).stdout.strip()
    if changed:
        print("reports: runtime evidence changed; review and commit it:\n" + changed)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
