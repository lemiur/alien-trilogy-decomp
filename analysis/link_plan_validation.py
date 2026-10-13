"""Check coverage arithmetic and rejection of edited link-plan evidence."""
from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("link_plan", PROJECT / "analysis/link_plan.py")
link_plan = importlib.util.module_from_spec(spec)
spec.loader.exec_module(link_plan)


def main():
    latest = link_plan.read_json(link_plan.DEFAULT_LATEST)
    runtime_data = link_plan.read_json(link_plan.DEFAULT_RUNTIME_DATA)
    plan = link_plan.build_plan(link_plan.DEFAULT_LATEST, link_plan.DEFAULT_RUNTIME_LINKED,
                                link_plan.DEFAULT_RUNTIME_SYMBOLS, link_plan.DEFAULT_LOCK)
    payload = plan["payload"]
    if payload["covered_union_bytes"] + payload["uncovered_gap_bytes"] != payload["size_bytes"]:
        raise AssertionError("certified ranges and uncovered gaps do not partition the payload")
    progress = link_plan.read_json(PROJECT / "reports/progress.json")
    if payload["covered_union_bytes"] != progress["combined_distinct_certified_payload_bytes"]:
        raise AssertionError("progress and link plan disagree on distinct coverage")
    # The independently byte-checked initialized definitions describe each
    # relocated word's source and target. Ensure the link backlog preserves
    # both, even when separate definitions have discontiguous placement.
    initialized_sites = []
    for group in latest["data_checks"]:
        for definition in group["definitions"]:
            for patch in definition.get("applied_relocations", []):
                expected_site = hex(int(definition["address"], 0) + patch["offset"])
                refs = [r for r in plan["external_references"]["all_references"]
                        if group["unit"] in r["units"] and r["address"] == patch["address"]]
                # Generated switch tables use local section relocations,
                # which are not external-reference records.
                if group["unit"] not in ("menu_data", "menu_choices", "menu_records6", "menu_boolean_choices6", "menu_input7", "menu_sound_record7", "menu_card_records7"):
                    continue
                if not any(s.get("definition") == definition["symbol"] and
                           s.get("address") == expected_site
                           for r in refs for s in r["sites"]):
                    raise AssertionError(f"missing initialized reference site: {definition['symbol']} {expected_site}")
                initialized_sites.append({"definition": definition["symbol"],
                                          "address": expected_site, "target": patch["address"]})
    if not initialized_sites:
        raise AssertionError("no initialized menu reference sites were checked")
    cases = []
    guarded_paths = {Path(latest["reference"]), Path(latest["data_manifest"]),
                     link_plan.DEFAULT_LATEST, link_plan.DEFAULT_RUNTIME_DATA,
                     link_plan.DEFAULT_RUNTIME_LINKED, link_plan.DEFAULT_RUNTIME_SYMBOLS}
    for check in latest["checks"] + latest["data_checks"]:
        guarded_paths.update(Path(check[name]) for name in
                             ("source", "symbol_map", "object", "source_snapshot", "map_snapshot"))
    before = {str(p): link_plan.digest(p) for p in guarded_paths}
    mutations = [
        ("failed_check_flag", "latest", lambda r: r.__setitem__("all_selected_checks_pass", False)),
        ("bad_source_hash", "latest", lambda r: r["checks"][0].__setitem__("source_sha256", "0" * 64)),
        ("bad_whole_object_hash", "latest", lambda r: r["checks"][0].__setitem__("object_file_sha256", "0" * 64)),
        ("bad_data_only_source_hash", "latest", lambda r: next(c for c in r["data_checks"] if c["unit"] == "game_constants").__setitem__("source_sha256", "0" * 64)),
        ("bad_data_only_map_hash", "latest", lambda r: next(c for c in r["data_checks"] if c["unit"] == "game_constants").__setitem__("map_sha256", "0" * 64)),
        ("bad_data_only_object_hash", "latest", lambda r: next(c for c in r["data_checks"] if c["unit"] == "game_constants").__setitem__("object_file_sha256", "0" * 64)),
        ("bad_data_manifest_hash", "latest", lambda r: r.__setitem__("data_manifest_sha256", "0" * 64)),
        ("bad_runtime_data_count", "data", lambda r: r["counts"].__setitem__("certified_sections", 999)),
        ("bad_runtime_data_status", "data", lambda r: next(s for s in r["section_results"]
                                                          if s["status"] == "ambiguous_unanchored").__setitem__("status", "full_match")),
        ("bad_runtime_data_hash", "data", lambda r: r["inputs"].__setitem__("runtime_linked_report_sha256", "0" * 64)),
    ]
    with tempfile.TemporaryDirectory(prefix="alien-link-plan-validation-") as temp:
        for name, kind, mutate in mutations:
            changed = copy.deepcopy(latest if kind == "latest" else runtime_data)
            mutate(changed)
            path = Path(temp) / f"{name}.json"
            path.write_text(json.dumps(changed), encoding="utf-8")
            try:
                link_plan.build_plan(path if kind == "latest" else link_plan.DEFAULT_LATEST,
                                     link_plan.DEFAULT_RUNTIME_LINKED,
                                     link_plan.DEFAULT_RUNTIME_SYMBOLS, link_plan.DEFAULT_LOCK,
                                     runtime_data_path=path if kind == "data" else link_plan.DEFAULT_RUNTIME_DATA)
            except (RuntimeError, ValueError) as exc:
                cases.append({"case": name, "result": "rejected", "reason": str(exc)})
            else:
                raise AssertionError(f"edited evidence was accepted: {name}")
    after = {str(p): link_plan.digest(p) for p in guarded_paths}
    if before != after:
        raise AssertionError("validation changed original evidence")
    report = {"status": "pass", "latest_report_sha256": link_plan.digest(link_plan.DEFAULT_LATEST),
              "runtime_data_report_sha256": link_plan.digest(link_plan.DEFAULT_RUNTIME_DATA),
              "payload_partition_complete": True, "progress_and_link_plan_agree": True,
              "covered_union_bytes": payload["covered_union_bytes"], "cases": cases,
              "initialized_reference_sites_checked": initialized_sites,
              "all_cases_rejected": True, "guarded_evidence_file_count": len(guarded_paths),
              "evidence_unchanged_before_after": True}
    output = PROJECT / "reports/link_plan_validation.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"pass: payload partition, progress consistency, and {len(cases)} evidence rejection checks")


if __name__ == "__main__":
    main()
