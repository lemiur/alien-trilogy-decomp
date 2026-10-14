"""Progress counted from the gate's own record of where every byte came from.

Primary source: ``builds/gate/provenance.json``, written by ``tools/gate.py``. Every payload
byte has one owner kind there, and this tool reports them as separate categories:

- ``c_function``          matched C functions (gate kind "function");
- ``c_verbatim``          C functions or data whose bytes come from inline asm ("verbatim");
                          never decompiled C, listed so the total adds up;
- ``c_data``              initialized data defined in C (gate kind "data");
- ``generated_data``      compiler-generated data of C units (string pools, jump tables);
- ``asm``                 hand-written assembly units (ASM_UNITS, the GTE/renderer code);
                          never decompiled C;
- ``library_asm``         library assembly units (LIBRARY_ASM_UNITS): Sony Runtime Library
                          variant modules written as annotated .s units; never decompiled C,
                          never certified library, never part of ``asm``;
- ``library_text`` / ``library_data``
                          bytes the gate takes from the fallback that the runtime reports
                          certify as Sony Runtime Library 3.3 object text and initialized
                          sections (not linked from library objects yet);
- ``fallback``            everything else.

"Decompiled game C" is c_function + c_data + generated_data, the gate's ``source_bytes``.

Independent second witnesses; any disagreement is an error (AGENTS.md, "The byte gate"):
- ``reports/latest.json`` (verify.py): the matched function, data and asm placements (each asm
  category apart) must be
  exactly the gate's, its byte totals must equal the record's, its sources must still hash
  as verified, and its unit digests must equal those of the gate's own verify run;
- ``tools/generated_witness.py`` must accept every generated claim, checked against
  ``reports/latest.json``;
- ``analysis/link_plan.py`` builds its range union from verify.py, the runtime reports and
  the witnessed generated claims; its per-category bytes must equal the record's.

Matched under the rules (the headline): ``tools/rules.py`` counts the byte-matched functions
that are not MATCH-PENDING and have no unexcused [match] rule finding (AGENTS.md, "Project
profile"). Its byte-matched count (the symbol maps of the registered units) must equal the
gate's matched C functions.

Run the whole chain with ``python tools/reports.py``.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parent
DEFAULT_PROVENANCE = PROJECT / "builds/gate/provenance.json"
DEFAULT_LATEST = PROJECT / "reports/latest.json"
CATEGORIES = ("c_function", "c_verbatim", "c_data", "generated_data", "asm", "library_asm",
              "library_text", "library_data", "fallback")
DECOMPILED_C = ("c_function", "c_data", "generated_data")
GATE_KIND = {"function": "c_function", "verbatim": "c_verbatim", "data": "c_data",
             "generated": "generated_data", "asm": "asm", "library_asm": "library_asm",
             "fallback": "fallback"}
ASM_KINDS = ("asm", "library_asm")  # tools/asm_units.py CATEGORIES: gate kind = progress category
REPORT_PREFIX = {"asm": "byte_matched_asm", "library_asm": "byte_matched_library_asm"}


class ProgressError(ValueError):
    pass


def num(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_module(name, path, register=False):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    if register:  # dataclasses look their module up in sys.modules
        sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


# ------------------------------------------------------------------ counting

def categorize(runs, load, payload_size, library_text, library_data):
    """Bytes per category over the payload.

    ``runs`` are the gate's provenance runs (address, size, kind); ``library_text`` and
    ``library_data`` are (start, size) ranges certified by the runtime reports. A library
    range must lie on fallback bytes only: a library byte the gate places from a unit is a
    double claim and raises, and so does a byte claimed as both library text and data.
    """
    end = load + payload_size
    codes = {name: i + 1 for i, name in enumerate(CATEGORIES)}
    owner = bytearray(payload_size)  # code of CATEGORIES; 0 = unset
    for run in runs:
        if run["address"] is None:
            continue  # the EXE header
        start, size = num(run["address"]), int(run["size"])
        if start < load or start + size > end:
            raise ProgressError(f"provenance run at {start:#x}+{size:#x} lies outside the payload")
        kind = GATE_KIND.get(run["kind"])
        if kind is None:
            raise ProgressError(f"unknown provenance kind {run['kind']!r}")
        offset = start - load
        if any(owner[offset:offset + size]):
            raise ProgressError(f"provenance runs overlap at {start:#x}")
        owner[offset:offset + size] = bytes([codes[kind]]) * size
    if not all(owner):
        raise ProgressError(f"provenance does not cover payload byte {load + owner.index(0):#x}")
    for name, ranges in (("library_text", library_text), ("library_data", library_data)):
        for start, size in ranges:
            if start < load or start + size > end:
                raise ProgressError(f"{name} range {start:#x}+{size:#x} lies outside the payload")
            for i in range(start - load, start - load + size):
                current = CATEGORIES[owner[i] - 1]
                if current == name:
                    continue  # ranges of one category may repeat (aliases)
                if current != "fallback":
                    raise ProgressError(f"{name} range {start:#x}+{size:#x} overlaps {current} "
                                        f"bytes at {load + i:#x}")
                owner[i] = codes[name]
    totals = {name: owner.count(codes[name]) for name in CATEGORIES}
    if sum(totals.values()) != payload_size:
        raise ProgressError("category totals do not add up to the payload")
    return totals


def asm_table_labels(spec_units=None):
    """{unit: set(labels)} of the asm data tables in .text, from tools/asm_units.py over each
    ASM_UNITS and LIBRARY_ASM_UNITS source (a table is a mapped label that owns data directives
    in .text inside its category's table window)."""
    verify = load_module("verify_progress", PROJECT / "tools/verify.py")
    return {unit: verify.asm_units.table_labels(source.read_text(encoding="utf-8"),
                                                json.loads(mapping.read_text(encoding="utf-8")), category)
            for category, registry in (("asm", verify.ASM_UNITS), ("library_asm", verify.LIBRARY_ASM_UNITS))
            for unit, source, mapping in registry}


def split_asm(functions, tables):
    """(routines, tables) of the asm function placements {(unit, symbol, address, size)}."""
    is_table = lambda item: item[1] in tables.get(item[0], ())
    return ({i for i in functions if not is_table(i)}, {i for i in functions if is_table(i)})


def gate_placements(owners, functions_by_unit):
    """{category: {(unit, symbol, address, size)}} for the placed (non-fallback) owners.
    verbatim and asm owners (of both asm categories) are split into ":function" and ":data"."""
    out = {}
    for o in owners:
        if o["kind"] == "fallback":
            continue
        kind = GATE_KIND[o["kind"]]
        if kind in ("c_verbatim",) + ASM_KINDS:
            role = "function" if o["symbol"] in functions_by_unit.get(o["unit"], ()) else "data"
            kind = f"{kind}:{role}"
        out.setdefault(kind, set()).add((o["unit"], o["symbol"], num(o["address"]), int(o["size"])))
    return out


def report_placements(latest):
    """The same sets from verify.py's report, keyed by (unit, symbol, address, size):
    positional data names such as ".rdata+0x0" recur across units."""
    out = {"functions": set(), "data": set()}
    for kind in ASM_KINDS:
        out[f"{kind}:function"], out[f"{kind}:data"] = set(), set()
    for c in latest["checks"]:
        if c["status"] == "match":
            kind = c.get("kind")
            out[f"{kind}:function" if kind in ASM_KINDS else "functions"].add(
                (c["unit"], c["function"], num(c["address"]), int(c["size"])))
    for group in latest["data_checks"]:
        if group["status"] != "match":
            continue
        kind = group.get("kind")
        for d in group["definitions"]:
            out[f"{kind}:data" if kind in ASM_KINDS else "data"].add(
                (group["unit"], d["symbol"], num(d["address"]), int(d["size"])))
    return out


def compare_placements(gate, report):
    """Problems where the gate's placements and verify.py's matches differ."""
    problems = []
    linked_functions = gate.get("c_function", set()) | gate.get("c_verbatim:function", set())
    linked_data = gate.get("c_data", set()) | gate.get("c_verbatim:data", set())
    for label, linked, verified in (("function", linked_functions, report["functions"]),
                                    ("datum", linked_data, report["data"]),
                                    ("asm function", gate.get("asm:function", set()), report["asm:function"]),
                                    ("asm datum", gate.get("asm:data", set()), report["asm:data"]),
                                    ("library asm function", gate.get("library_asm:function", set()),
                                     report["library_asm:function"]),
                                    ("library asm datum", gate.get("library_asm:data", set()),
                                     report["library_asm:data"])):
        for item in sorted(linked - verified):
            problems.append(f"gate placed {label} {item[0]}:{item[1]} at {item[2]:#x} that verify.py did not match")
        for item in sorted(verified - linked):
            problems.append(f"verify.py matched {label} {item[0]}:{item[1]} at {item[2]:#x} that the gate did not place")
    return problems


# ----------------------------------------------------------------- witnesses

def unit_digests(report):
    out = {}
    for check in report.get("checks", []) + report.get("data_checks", []):
        out.setdefault(check["unit"], set()).add(
            (check["source_sha256"], check["map_sha256"], check["object_file_sha256"]))
    return out


def check_latest_fresh(latest, lock, data_manifest):
    if not latest["all_selected_checks_pass"]:
        raise ProgressError("reports/latest.json contains failing checks")
    if latest["reference_sha256"] != lock["reference_sha256"]:
        raise ProgressError("reports/latest.json was made against a different original")
    if digest(data_manifest) != latest["data_manifest_sha256"]:
        raise ProgressError("config/data.json changed after verify.py ran")
    for check in latest["checks"] + latest["data_checks"]:
        for path, name in (("source", "source_sha256"), ("symbol_map", "map_sha256")):
            if digest(check[path]) != check[name]:
                raise ProgressError(f"{check[path]} changed after verify.py ran")
        if digest(check["object"]) != check["object_file_sha256"]:
            raise ProgressError(f"{check['object']} changed after verify.py ran")


def library_ranges(runtime_linked, runtime_data):
    text = [(num(m["address"]), int(m["text_size"])) for m in runtime_linked["module_results"]
            if m["status"] == "full_match"]
    sections = [s for s in runtime_data["section_results"] + runtime_data.get("supplemental_data_only_section_results", [])
                if s["status"] == "full_match"]
    return text, [(num(s["address"]), int(s["size"])) for s in sections], sections


def collect(provenance_path=DEFAULT_PROVENANCE, latest_path=DEFAULT_LATEST):
    """Count every category from the gate record and run every second witness."""
    lock = json.loads((PROJECT / "config/toolchain.lock.json").read_text(encoding="utf-8"))
    contract = json.loads((PROJECT / "config/gate.json").read_text(encoding="utf-8"))
    provenance_path = Path(provenance_path)
    if not provenance_path.is_file():
        raise ProgressError(f"{provenance_path} is missing; run python tools/gate.py first")
    record = json.loads(provenance_path.read_text(encoding="utf-8"))
    gate_dir = provenance_path.parent
    if record["contract"] != contract or contract["sha256"] != lock["reference_sha256"]:
        raise ProgressError("provenance record belongs to a different gate contract")
    exe = gate_dir / contract["binary"]
    if not exe.is_file() or digest(exe) != contract["sha256"]:
        raise ProgressError("the gate's linked executable is missing or does not hash to the contract; "
                            "the record is not from a green gate")
    raw = (WORKSPACE / contract["reference"]).read_bytes()
    if hashlib.sha256(raw).hexdigest() != contract["sha256"]:
        raise ProgressError("configured original no longer has the contract hash")
    load, payload = num(contract["load_address"]), int(contract["payload_size"])

    latest = json.loads(Path(latest_path).read_text(encoding="utf-8"))
    check_latest_fresh(latest, lock, PROJECT / "config/data.json")
    gate_verify_path = gate_dir / "verify.json"
    if not gate_verify_path.is_file():
        raise ProgressError(f"{gate_verify_path} is missing; the record cannot be tied to the tree")
    gate_verify = json.loads(gate_verify_path.read_text(encoding="utf-8"))
    if unit_digests(gate_verify) != unit_digests(latest) or sorted(gate_verify["units"]) != sorted(latest["units"]):
        raise ProgressError("the gate record and reports/latest.json were built from different sources; "
                            "re-run python tools/reports.py")

    runtime_linked = json.loads((PROJECT / "analysis/runtime_linked.json").read_text(encoding="utf-8"))
    runtime_audit = json.loads((PROJECT / "reports/runtime_latest.json").read_text(encoding="utf-8"))
    runtime_data = json.loads((PROJECT / "reports/runtime_data.json").read_text(encoding="utf-8"))
    load_module("runtime_data", PROJECT / "analysis/runtime_data.py").validate_report(runtime_data)
    if runtime_linked["executable_sha256"] != contract["sha256"]:
        raise ProgressError("runtime reports refer to a different executable")
    for library, sha in runtime_audit["runtime_library_sha256"].items():
        if digest(WORKSPACE / library) != sha:
            raise ProgressError(f"runtime library changed: {library}")
    lib_text, lib_data, lib_sections = library_ranges(runtime_linked, runtime_data)

    totals = categorize(record["runs"], load, payload, lib_text, lib_data)
    problems = []

    # Witness 1: verify.py's per-function and per-datum results.
    functions_by_unit = {}
    for check in latest["checks"]:
        functions_by_unit.setdefault(check["unit"], set()).add(check["function"])
    placed = gate_placements(record["owners"], functions_by_unit)
    problems += compare_placements(placed, report_placements(latest))
    if totals["c_function"] + totals["c_verbatim"] + totals["c_data"] != (
            latest["byte_matched_function_bytes"] + latest["byte_matched_initialized_data_bytes"]):
        problems.append("C function and data bytes differ between the gate record and verify.py")
    source = totals["c_function"] + totals["c_data"] + totals["generated_data"]
    labels = asm_table_labels()
    asm_split = {}
    for kind in ASM_KINDS:
        prefix = REPORT_PREFIX[kind]
        if totals[kind] != latest.get(f"{prefix}_bytes", 0):
            problems.append(f"{kind} bytes: gate record {totals[kind]}, verify.py {latest.get(f'{prefix}_bytes', 0)}")
        routines, tables = split_asm(placed.get(f"{kind}:function", set()), labels)
        data = placed.get(f"{kind}:data", set())
        asm_split[kind] = (routines, tables, data)
        routine_bytes, table_bytes, data_bytes = (sum(i[3] for i in s) for s in (routines, tables, data))
        for key, value in ((f"{prefix}_routines", len(routines)),
                           (f"{prefix}_routine_bytes", routine_bytes),
                           (f"{prefix}_tables", len(tables)),
                           (f"{prefix}_table_bytes", table_bytes),
                           (f"{prefix}_data_definitions", len(data)),
                           (f"{prefix}_data_bytes", data_bytes)):
            if key in latest and latest[key] != value:
                problems.append(f"{key}: gate record {value}, verify.py {latest[key]}")
    if (source != record["source_bytes"] or totals["asm"] != record["asm_bytes"]
            or totals["library_asm"] != record.get("library_asm_bytes", 0)):
        problems.append("byte-level totals disagree with the record's own source_bytes/asm_bytes/library_asm_bytes")

    # Witness 2: generated_witness.py against verify.py's objects.
    claims = [{**o, "label": o["symbol"]} for o in record["owners"] if o["kind"] == "generated"]
    witness = load_module("generated_witness", PROJECT / "tools/generated_witness.py")
    witnessed, issues = witness.witness(claims, latest, raw, load, contract["header_size"]) if claims else (0, [])
    problems += [f"generated-data witness: {issue}" for issue in issues]
    if witnessed != totals["generated_data"]:
        problems.append(f"generated bytes: gate record {totals['generated_data']}, witness {witnessed}")

    # Witness 3: the link plan's range union over verify.py, runtime reports and witnessed claims.
    planner = load_module("link_plan_progress", PROJECT / "analysis/link_plan.py")
    plan = planner.build_plan(planner.DEFAULT_LATEST, planner.DEFAULT_RUNTIME_LINKED,
                              planner.DEFAULT_RUNTIME_SYMBOLS, planner.DEFAULT_LOCK,
                              provenance_path=provenance_path)
    covered = payload - totals["fallback"]
    if plan["payload"]["covered_union_bytes"] != covered:
        problems.append(f"covered bytes: gate record + runtime reports {covered}, "
                        f"link plan {plan['payload']['covered_union_bytes']}")
    for kind, category in (("recovered_c_function", ("c_function", "c_verbatim")),
                           ("recovered_c_initialized_data", ("c_data",)),
                           ("compiler_generated_data", ("generated_data",)),
                           ("asm", ("asm",)),
                           ("library_asm", ("library_asm",)),
                           ("runtime_object_text", ("library_text",)),
                           ("runtime_initialized_section", ("library_data",))):
        planned = plan["category_bytes"].get(kind, 0)
        expected = sum(totals[c] for c in category)
        if planned != expected:
            problems.append(f"{kind}: link plan {planned} bytes, gate record {expected}")
    if problems:
        raise ProgressError("gate record and second witnesses disagree:\n  " + "\n  ".join(problems))

    counts = {
        "c_function": len(placed.get("c_function", ())),
        "c_verbatim": len(placed.get("c_verbatim:function", ())) + len(placed.get("c_verbatim:data", ())),
        "c_data": len(placed.get("c_data", ())),
        "generated_data": len(claims),
        "library_text": len(lib_text),
        "library_data": len(lib_sections),
    }
    for kind in ASM_KINDS:
        routines, tables, data = asm_split[kind]
        counts.update({kind: len(routines) + len(tables) + len(data),
                       f"{kind}_functions": len(routines) + len(tables),
                       f"{kind}_routines": len(routines),
                       f"{kind}_tables": len(tables),
                       f"{kind}_data": len(data),
                       f"{kind}_routine_bytes": sum(i[3] for i in routines),
                       f"{kind}_table_bytes": sum(i[3] for i in tables),
                       f"{kind}_data_bytes": sum(i[3] for i in data)})
    return {"contract": contract, "latest": latest, "totals": totals, "counts": counts, "plan": plan,
            "runtime_unresolved": [m["module"] for m in runtime_linked["module_results"]
                                   if m["status"] not in ("full_match", "library_asm")],
            "runtime_library_asm": [f"{m['module']} ({', '.join(m.get('library_asm_units', []))})"
                                    for m in runtime_linked["module_results"] if m["status"] == "library_asm"]}


def rules_summary(c_functions):
    """tools/rules.py's summary; its byte-matched functions must be the gate's matched C functions."""
    summary = load_module("rules", PROJECT / "tools/rules.py", register=True).check(decimal=False).summary()
    if summary["byte_matched_functions"] != c_functions:
        raise ProgressError(f"byte-matched functions: rules.py (symbol maps) {summary['byte_matched_functions']}, "
                            f"gate record {c_functions}")
    return summary


def rules_lines(rules):
    """The PROGRESS.md headline: matched under the rules, then bytes matched."""
    if not rules:
        return []
    failing = ", ".join(f"{r} {n}" for r, n in rules["failing_functions_by_rule"].items())
    return [f"**Matched under the rules: {rules['matched_under_the_rules']:,} of "
            f"{rules['byte_matched_functions']:,} byte-matched game C functions.** "
            f"MATCH-PENDING: {rules['match_pending_functions']:,}; with unexcused [match] findings: "
            f"{rules['functions_with_unexcused_match_findings']:,}" + (f" ({failing})" if failing else "")
            + ". Counted by `tools/rules.py` (AGENTS.md, \"Project profile\"): a byte-matched function counts "
            "only when it is not MATCH-PENDING and passes every [match] rule.", ""]


# -------------------------------------------------------------------- output

ROWS = (
    ("c_function", "Game C functions", "functions"),
    ("c_data", "Game C initialized data", "definitions"),
    ("generated_data", "Compiler-generated data of game C units", "items"),
    ("asm", "Hand-written assembly (GTE/renderer; not C)", "functions"),
    ("library_asm", "Library assembly (Sony Runtime Library variant modules; not C, not certified)", "items"),
    ("c_verbatim", "Inline asm in C units (\"verbatim\"; not C)", "items"),
    ("library_text", "Sony Runtime Library 3.3 text (certified; linked from fallback)", "modules"),
    ("library_data", "Sony Runtime Library 3.3 initialized data (certified; linked from fallback)", "sections"),
    ("fallback", "Fallback: original bytes not yet accounted for", ""),
)


def render(result):
    totals, counts, payload = result["totals"], result["counts"], result["contract"]["payload_size"]
    pct = lambda n: f"{100 * n / payload:.2f}%"
    decompiled = sum(totals[c] for c in DECOMPILED_C)
    lines = ["# Alien Trilogy progress", "",
             "Counted by `tools/progress.py` from the per-byte record of a green `tools/gate.py` run "
             "(`builds/gate/provenance.json`) and cross-checked against `verify.py`, "
             "`generated_witness.py`, the runtime reports and `analysis/link_plan.py`.", "",
             f"Payload: {payload:,} bytes. Compiler: {result['latest']['compiler']}, ASPSX 2.34, "
             "`-O2 -G8`; Sony Runtime Library 3.3.", ""] + rules_lines(result.get("rules")) + [
             "| Category | Count | Bytes | Payload |", "|---|---:|---:|---:|"]
    for key, label, unit in ROWS:
        count = counts.get(key)
        shown = f"{count:,} {unit}" if unit else ""
        if key == "asm":
            shown = (f"{counts['asm_routines']:,} routines + {counts['asm_tables']:,} tables "
                     f"({counts['asm_routine_bytes']:,} + {counts['asm_table_bytes']:,} bytes)")
        if key == "library_asm":
            shown = (f"{counts['library_asm_routines']:,} routines + {counts['library_asm_data']:,} data "
                     f"({counts['library_asm_routine_bytes']:,} + {counts['library_asm_data_bytes']:,} bytes)")
        lines.append(f"| {label} | {shown} | {totals[key]:,} | {pct(totals[key])} |")
    lines += [f"| **Total** | | **{payload:,}** | 100.00% |", "",
              f"**Decompiled game C: {decompiled:,} bytes ({pct(decompiled)} of the payload).** It is the "
              "sum of the first three rows and equals the gate's `source_bytes`. Assembly, library "
              "assembly, inline asm and the Sony library are separate figures and never part of it. "
              "Library assembly is the gate's `library_asm_bytes`: Sony Runtime Library modules that "
              "match no archived version, written as annotated `.s` units. The certified library rows "
              "are original bytes that the runtime reports match against Sony's objects; the gate still "
              "takes them from the fallback.", "",
              "Runtime candidates with conflicting symbol bindings, not certified: "
              + (", ".join(result["runtime_unresolved"]) or "none") + ".", "",
              "Runtime candidates rebuilt as library assembly (archive text with different "
              "relocations; not certified): "
              + (", ".join(result["runtime_library_asm"]) or "none") + ".", "",
              "Reproduce: `python tools/reports.py` (gate, verify.py, runtime reports, link plan, "
              "this file). `reports/link_plan.json` lists the uncovered gaps.", ""]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--provenance", type=Path, default=DEFAULT_PROVENANCE)
    ap.add_argument("--latest", type=Path, default=DEFAULT_LATEST)
    args = ap.parse_args(argv)
    result = collect(args.provenance, args.latest)
    result["rules"] = rules_summary(result["counts"]["c_function"])
    totals, plan, payload = result["totals"], result["plan"], result["contract"]["payload_size"]
    report = {"generated_utc": datetime.now(timezone.utc).isoformat(),
              "reference_sha256": result["contract"]["sha256"],
              "payload_size": payload,
              "primary_source": "builds/gate/provenance.json",
              "second_witnesses": ["reports/latest.json", "tools/generated_witness.py",
                                   "reports/runtime_latest.json", "reports/runtime_data.json",
                                   "analysis/link_plan.py"],
              "matched_under_the_rules": result["rules"]["matched_under_the_rules"],
              "rules": {k: v for k, v in result["rules"].items() if k != "match_pending"},
              "match_pending": result["rules"]["match_pending"],
              "bytes": totals, "counts": result["counts"],
              "decompiled_game_c_bytes": sum(totals[c] for c in DECOMPILED_C),
              "covered_bytes": payload - totals["fallback"],
              "combined_distinct_certified_payload_bytes": payload - totals["fallback"],
              "uncovered_payload_gap_count": len(plan["uncovered_payload_gaps"]),
              "uncovered_or_unresolved_reference_count": len(plan["external_references"]["uncovered_or_unresolved"]),
              "external_reference_status_counts": plan["external_references"]["status_counts"],
              "runtime_unresolved_candidates": result["runtime_unresolved"],
              "runtime_library_asm_candidates": result["runtime_library_asm"],
              "note": "decompiled_game_c_bytes = c_function + c_data + generated_data (the gate's source_bytes). "
                      "asm, library_asm, c_verbatim and library bytes are separate and never part of it; "
                      "library_asm is the gate's library_asm_bytes and never certified library."}
    (PROJECT / "reports/progress.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (PROJECT / "PROGRESS.md").write_text(render(result), encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("matched_under_the_rules", "bytes", "counts", "decompiled_game_c_bytes",
                                             "covered_bytes")}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ProgressError as exc:
        print(f"progress: {exc}", file=sys.stderr)
        raise SystemExit(1)
