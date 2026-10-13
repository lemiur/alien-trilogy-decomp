"""Build an evidence-backed backlog for reconstructing the linked executable.

This inventories exact recovered ranges and relocation targets. It never writes
or copies executable bytes and does not claim that a full executable rebuild
exists.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from collections import defaultdict
from pathlib import Path
from typing import Any

PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parent
DEFAULT_LATEST = PROJECT / "reports" / "latest.json"
DEFAULT_RUNTIME_LINKED = PROJECT / "analysis" / "runtime_linked.json"
DEFAULT_RUNTIME_SYMBOLS = PROJECT / "analysis" / "runtime_symbols.json"
DEFAULT_RUNTIME_DATA = PROJECT / "reports" / "runtime_data.json"
DEFAULT_LOCK = PROJECT / "config" / "toolchain.lock.json"
DEFAULT_OUTPUT = PROJECT / "reports" / "link_plan.json"
DEFAULT_PROVENANCE = PROJECT / "builds" / "gate" / "provenance.json"
PSYQOBJ = WORKSPACE / "shared-tools" / "psyqobj.py"


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def as_int(value: Any) -> int:
    return int(value, 0) if isinstance(value, str) else int(value)


def load_lnk_symbols():
    """tools/lnk_symbols.py: the object's local-symbol records (static functions), which psyqobj does not keep."""
    spec = importlib.util.spec_from_file_location("link_plan_lnk_symbols", PROJECT / "tools" / "lnk_symbols.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_parser():
    if not PSYQOBJ.is_file():
        raise FileNotFoundError(f"pinned PsyQ parser is missing: {PSYQOBJ}")
    spec = importlib.util.spec_from_file_location("pinned_psyqobj", PSYQOBJ)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load PsyQ parser: {PSYQOBJ}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def project_path(value: str | Path) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    # Reports may have been generated with either project-relative or workspace-relative paths.
    for base in (PROJECT, WORKSPACE):
        candidate = (base / path).resolve()
        if candidate.exists():
            return candidate
    return (PROJECT / path).resolve()


def collect_ranges(latest: dict[str, Any], runtime: dict[str, Any], runtime_data=None, generated=None):
    ranges: list[dict[str, Any]] = []
    # Assembly (verify.py kinds "asm", hand-written, and "library_asm", Sony Runtime Library
    # variant modules) keeps its own kind: never recovered C, and library_asm never certified library.
    asm_kinds = ("asm", "library_asm")
    for check in latest.get("checks", []):
        if check.get("status") != "match":
            continue
        start, size = as_int(check["address"]), int(check["size"])
        ranges.append({"start": start, "end": start + size, "size": size,
                       "kind": check["kind"] if check.get("kind") in asm_kinds else "recovered_c_function",
                       "name": check["function"], "unit": check["unit"]})
    for data_check in latest.get("data_checks", []):
        if data_check.get("status") != "match":
            continue
        for definition in data_check.get("definitions", []):
            if definition.get("status") != "match":
                continue
            start, size = as_int(definition["address"]), int(definition["size"])
            ranges.append({"start": start, "end": start + size, "size": size,
                           "kind": (data_check["kind"] if data_check.get("kind") in asm_kinds
                                    else "recovered_c_initialized_data"),
                           "name": definition["symbol"], "unit": data_check["unit"]})
    for claim in generated or []:
        start, size = as_int(claim["address"]), int(claim["size"])
        ranges.append({"start": start, "end": start + size, "size": size,
                       "kind": "compiler_generated_data", "name": claim["symbol"], "unit": claim["unit"]})
    for module in runtime.get("module_results", []):
        if module.get("status") != "full_match":
            continue
        start, size = as_int(module["address"]), int(module["text_size"])
        ranges.append({"start": start, "end": start + size, "size": size,
                       "kind": "runtime_object_text", "name": module["module"],
                       "library": module["library"]})
    for section in ((runtime_data or {}).get("section_results", []) +
                    (runtime_data or {}).get("supplemental_data_only_section_results", [])):
        if section.get("status") != "full_match":
            continue
        start, size = as_int(section["address"]), int(section["size"])
        ranges.append({"start": start, "end": start + size, "size": size,
                       "kind": "runtime_initialized_section",
                       "name": f"{section['module']}:{section['section']}",
                       "library": section["library"]})
    return sorted(ranges, key=lambda r: (r["start"], r["end"], r["kind"], r["name"]))


def range_union(ranges: list[dict[str, Any]], start: int, end: int):
    clipped = []
    for r in ranges:
        a, b = max(start, r["start"]), min(end, r["end"])
        if a < b:
            clipped.append((a, b, r))
    clipped.sort(key=lambda x: (x[0], x[1]))
    union: list[dict[str, Any]] = []
    overlaps: list[dict[str, Any]] = []
    for a, b, r in clipped:
        if union and a < union[-1]["end"]:
            overlaps.append({"start": hex(a), "end": hex(min(b, union[-1]["end"])),
                             "existing": union[-1]["members"], "incoming": r})
        if not union or a > union[-1]["end"]:
            union.append({"start": a, "end": b, "members": [r]})
        else:
            union[-1]["end"] = max(union[-1]["end"], b)
            if r not in union[-1]["members"]:
                union[-1]["members"].append(r)
    gaps = []
    cursor = start
    for r in union:
        if cursor < r["start"]:
            gaps.append({"start": hex(cursor), "end": hex(r["start"]), "size": r["start"] - cursor})
        cursor = max(cursor, r["end"])
    if cursor < end:
        gaps.append({"start": hex(cursor), "end": hex(end), "size": end - cursor})
    serialized_union = [{"start": hex(r["start"]), "end": hex(r["end"]),
                         "size": r["end"] - r["start"], "members": r["members"]} for r in union]
    return serialized_union, gaps, overlaps


def walk_xrefs(expr: Any):
    if isinstance(expr, tuple):
        if len(expr) == 2 and expr[0] == 2 and isinstance(expr[1], int):
            yield expr[1]
        else:
            for item in expr[1:]:
                yield from walk_xrefs(item)
    elif isinstance(expr, list):
        for item in expr:
            yield from walk_xrefs(item)


def symbol_address(name: str, symbol_map: dict[str, Any]):
    if name in symbol_map.get("symbols", {}):
        return as_int(symbol_map["symbols"][name])
    fn = symbol_map.get("functions", {}).get(name)
    if isinstance(fn, dict) and "address" in fn:
        return as_int(fn["address"])
    return None


def aliases_by_address(map_files: list[Path], runtime_symbols: dict[str, Any]):
    aliases: dict[int, set[str]] = defaultdict(set)
    for path in map_files:
        try:
            mapping = read_json(path)
        except (OSError, ValueError):
            continue
        for name, value in mapping.get("symbols", {}).items():
            try:
                aliases[as_int(value)].add(name)
            except (TypeError, ValueError):
                continue
        for name, value in mapping.get("functions", {}).items():
            try:
                aliases[as_int(value["address"])].add(name)
            except (TypeError, ValueError, KeyError):
                continue
    for rec in runtime_symbols.get("exports", []):
        try:
            aliases[as_int(rec["address"])].add(rec["name"])
        except (TypeError, ValueError, KeyError):
            continue
    groups = {addr: sorted(names) for addr, names in aliases.items() if len(names) > 1}
    return groups


def range_coverage(address: int | None, ranges: list[dict[str, Any]]):
    if address is None:
        return []
    found = []
    for r in ranges:
        if r["start"] <= address < r["end"]:
            found.append({"kind": r["kind"], "name": r["name"],
                          **({"unit": r["unit"]} if "unit" in r else {}),
                          **({"library": r["library"]} if "library" in r else {})})
    return found


def relocation_target_kind(types: set[int]):
    if types and types <= {74}:
        return "code"
    if 30 in types:
        return "data"
    if types and types <= {82, 84}:
        return "code_or_data_address"
    if types:
        return "mixed_or_unknown"
    return "unknown"


def collect_references(latest, ranges, runtime_symbols):
    parser = load_parser()
    map_paths = []
    objects = {}
    for check in latest.get("checks", []) + latest.get("data_checks", []):
        if check.get("status") != "match":
            continue
        obj_path = project_path(check["object"])
        map_path = project_path(check.get("map_snapshot") or check["symbol_map"])
        map_paths.append(map_path)
        key = str(obj_path)
        objects.setdefault(key, {"path": obj_path, "map_path": map_path, "unit": check["unit"],
                                 "source": check["source"], "source_sha256": check["source_sha256"],
                                 "source_snapshot": check.get("source_snapshot"),
                                 "map_sha256": check["map_sha256"], "functions": set(),
                                 "definitions": {}})
        if "function" in check:
            objects[key]["functions"].add(check["function"])
        for definition in check.get("definitions", []):
            if definition.get("status") == "match":
                objects[key]["definitions"][definition["symbol"]] = definition
    alias_groups = aliases_by_address(sorted(set(map_paths)), runtime_symbols)
    refs: dict[tuple[str, str, int | None], dict[str, Any]] = {}
    provenance = []
    for obj_record in objects.values():
        obj_path: Path = obj_record["path"]
        map_path: Path = obj_record["map_path"]
        mapping = read_json(map_path)
        loaded = parser.load(str(obj_path))
        if len(loaded) != 1 or isinstance(loaded[0], Exception):
            raise RuntimeError(f"latest report object is not one valid LNK object: {obj_path}")
        obj = loaded[0]
        lnk_symbols = load_lnk_symbols()
        local_text = [(n, s, o) for s, o, n in lnk_symbols.records(obj_path.read_bytes())["locals"]]
        provenance.append({"unit": obj_record["unit"], "source": obj_record["source"],
                          "source_sha256": obj_record["source_sha256"],
                          "source_snapshot": obj_record["source_snapshot"],
                          "source_snapshot_sha256": (digest(project_path(obj_record["source_snapshot"]))
                                                     if obj_record["source_snapshot"] else None),
                          "symbol_map": str(map_path), "symbol_map_sha256": obj_record["map_sha256"],
                          "symbol_map_snapshot_sha256": digest(map_path),
                          "object": str(obj_path), "object_sha256": digest(obj_path),
                          "functions": sorted(obj_record["functions"])})
        refs_for_idx: dict[int, dict[str, Any]] = {}
        for idx, name in obj.xrefs.items():
            addr = symbol_address(name, mapping)
            refs_for_idx[idx] = {"name": name, "address": addr, "relocation_types": set(),
                                 "sites": []}
        fn_records = []
        data_records = []
        for static, symbols in ((False, obj.xdefs), (True, local_text)):
            for name, sec, offset in symbols:
                f = mapping.get("functions", {}).get(name)
                if (isinstance(f, dict) and "size" in f and "address" in f and bool(f.get("static")) == static
                        and obj.sections.get(sec) == ".text"):
                    fn_records.append((sec, offset, offset + int(f["size"]), name, as_int(f["address"])))
        for name, sec, offset in obj.xdefs:
            definition = obj_record["definitions"].get(name)
            if definition:
                data_records.append((sec, offset, offset + int(definition["size"]),
                                     name, as_int(definition["address"])))
        for sec, patches in obj.patches.items():
            for typ, off, expr in patches:
                for idx in set(walk_xrefs(expr)):
                    ref = refs_for_idx.get(idx)
                    if ref is None:
                        continue
                    ref["relocation_types"].add(typ)
                    owner = next((f for f in fn_records if f[0] == sec and f[1] <= off < f[2]), None)
                    site = {"object_section": obj.sections.get(sec, str(sec)), "object_offset": hex(off)}
                    if owner:
                        site.update({"function": owner[3], "address": hex(owner[4] + off - owner[1])})
                    else:
                        data_owner = next((d for d in data_records
                                           if d[0] == sec and d[1] <= off < d[2]), None)
                        if data_owner:
                            site.update({"definition": data_owner[3],
                                         "address": hex(data_owner[4] + off - data_owner[1])})
                    ref["sites"].append(site)
        for ref in refs_for_idx.values():
            addr = ref["address"]
            coverage = range_coverage(addr, ranges)
            alias_names = alias_groups.get(addr, []) if addr is not None else []
            if addr is None:
                status = "unresolved_symbol_address"
            elif coverage:
                status = "covered"
            elif relocation_target_kind(ref["relocation_types"]) == "code":
                status = "mapped_uncovered_code_target"
            elif relocation_target_kind(ref["relocation_types"]) == "data":
                status = "mapped_uncovered_data_target"
            else:
                status = "mapped_uncovered_code_or_data_target"
            key = (ref["name"], obj_record["unit"], addr)
            record = refs.setdefault(key, {"symbol": ref["name"],
                                           "address": None if addr is None else hex(addr),
                                           "status": status,
                                           "inferred_target_kind": relocation_target_kind(ref["relocation_types"]),
                                           "covered_by": coverage,
                                           "alias_names_at_address": alias_names,
                                           "alias_reconciliation_required": len(alias_names) > 1,
                                           "units": set(), "relocation_types": set(), "sites": []})
            record["units"].add(obj_record["unit"])
            record["relocation_types"].update(ref["relocation_types"])
            record["sites"].extend(ref["sites"])
    serialized = []
    for r in refs.values():
        serialized.append({**r, "units": sorted(r["units"]),
                           "relocation_types": sorted(r["relocation_types"]),
                           "sites": sorted(r["sites"], key=lambda x: (x.get("address", ""), x["object_offset"]))})
    serialized.sort(key=lambda r: (r["address"] or "", r["symbol"], r["units"]))
    return serialized, provenance, alias_groups


def collect_runtime_references(runtime, ranges, alias_groups):
    """Preserve imported-symbol obligations from every certified SDK text."""
    spec = importlib.util.spec_from_file_location("runtime_refs", PROJECT / "analysis/runtime_relocations.py")
    rr = importlib.util.module_from_spec(spec); spec.loader.exec_module(rr)
    bindings = {tuple([row["kind"], *row["name_or_section"]]): as_int(row["address"])
                for row in runtime.get("symbol_bindings", [])
                if row.get("status") == "resolved" and row.get("address") is not None}
    records = []
    for module in runtime.get("module_results", []):
        if module.get("status") != "full_match": continue
        path = WORKSPACE / module["library"]
        raw = next((lnk for name,lnk in rr.library_modules(path) if name == module["module"]), None)
        if raw is None: raise RuntimeError("SDK raw object missing: " + module["module"])
        obj = rr.psyqobj.parse_lnk(raw, module["module"])
        _, imports = rr.symbol_ids(raw)
        obj.xrefs.update(imports)
        text = next(s for s,n in obj.sections.items() if n == ".text")
        unit = "runtime:" + module["library"] + "::" + module["module"] + "@" + module["address"]
        for index,name in imports.items():
            sites = []; types = set()
            for typ,off,expr in obj.patches.get(text, []):
                if index not in set(walk_xrefs(expr)): continue
                types.add(typ)
                sites.append({"object_section":".text","object_offset":hex(off),
                              "module":module["module"],"address":hex(as_int(module["address"])+off)})
            if not sites: continue
            addr = bindings.get(("symbol",name))
            coverage = range_coverage(addr,ranges)
            names = alias_groups.get(addr,[]) if addr is not None else []
            kind = relocation_target_kind(types)
            status = ("unresolved_symbol_address" if addr is None else "covered" if coverage else
                      "mapped_uncovered_code_target" if kind == "code" else
                      "mapped_uncovered_data_target" if kind == "data" else "mapped_uncovered_code_or_data_target")
            records.append({"symbol":name,"address":hex(addr) if addr is not None else None,
                            "status":status,"inferred_target_kind":kind,"covered_by":coverage,
                            "alias_names_at_address":names,"alias_reconciliation_required":len(names)>1,
                            "units":[unit],"relocation_types":sorted(types),"sites":sites,
                            "reference_scope":"certified_runtime_text"})
    return records


def runtime_provenance(runtime: dict[str, Any], runtime_symbols: dict[str, Any]):
    symbol_records = {(x.get("library"), x.get("module"), x.get("address")): x
                      for x in runtime_symbols.get("modules", [])}
    hashes = runtime.get("runtime_library_sha256", {})
    result = []
    for module in runtime.get("module_results", []):
        if module.get("status") != "full_match":
            continue
        key = (module.get("library"), module.get("module"), module.get("address"))
        sym = symbol_records.get(key, {})
        result.append({"module": module["module"], "library": module["library"],
                       "library_sha256": hashes.get(module["library"]),
                       "address": module["address"], "text_size": module["text_size"],
                       "object_text_sha256": sym.get("object_text_sha256"),
                       "reconstructed_text_sha256": module.get("reconstructed_sha256"),
                       "reference_text_sha256": module.get("reference_sha256"),
                       "relocation_count": module.get("relocation_count")})
    return result


def manifest_digest(value: Any) -> tuple[str | None, str]:
    """Hash a manifest represented by a file path or embedded JSON value."""
    if isinstance(value, str):
        path = project_path(value)
        return str(path), digest(path)
    if isinstance(value, dict) and isinstance(value.get("path") or value.get("file"), str):
        path = project_path(value.get("path") or value.get("file"))
        return str(path), digest(path)
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return None, hashlib.sha256(payload).hexdigest()


def validate_provenance(latest: dict[str, Any], runtime: dict[str, Any],
                        runtime_symbols: dict[str, Any], lock_path: Path):
    lock = read_json(lock_path)
    if latest.get("reference_sha256") != lock.get("reference_sha256"):
        raise RuntimeError("latest report reference hash differs from config/toolchain.lock.json")
    reference = project_path(latest["reference"])
    reference_sha = digest(reference)
    if reference_sha != latest["reference_sha256"]:
        raise RuntimeError(f"current reference hash differs from latest report: {reference}")
    if latest.get("all_selected_checks_pass") is not True:
        raise RuntimeError("latest report all_selected_checks_pass is not true")
    if latest.get("runtime") != lock.get("runtime"):
        raise RuntimeError("latest report Runtime Library version differs from toolchain lock")
    selected = lock.get("selected_assembler")
    if selected and latest.get("assemblers") != [selected]:
        raise RuntimeError("latest report assembler set differs from toolchain lock")
    if lock.get("default_options") and latest.get("options") != lock["default_options"]:
        raise RuntimeError("latest report compiler options differ from toolchain lock")
    if not str(latest.get("compiler", "")).startswith("GCC 2.5.7"):
        raise RuntimeError("latest report compiler is not the pinned GCC 2.5.7 build")

    parser_rel = "shared-tools/psyqobj.py"
    parser_expected = lock.get("files", {}).get(parser_rel)
    parser_actual = digest(PSYQOBJ)
    if not parser_expected or parser_actual != parser_expected:
        raise RuntimeError("pinned PsyQ parser hash differs from config/toolchain.lock.json")

    for name, report in (("runtime linked", runtime), ("runtime symbols", runtime_symbols)):
        if report.get("executable_sha256") != latest.get("reference_sha256"):
            raise RuntimeError(f"{name} report executable hash differs from latest report")
    runtime_hashes = runtime.get("runtime_library_sha256", {})
    symbols_hashes = runtime_symbols.get("runtime_library_sha256", runtime_hashes)
    if runtime_hashes != symbols_hashes:
        raise RuntimeError("runtime linked and runtime symbol reports have different library hash manifests")
    for rel, expected in runtime_hashes.items():
        archive = project_path(rel)
        if digest(archive) != expected:
            raise RuntimeError(f"Runtime Library archive hash differs from report: {rel}")

    checked_objects = set()
    for check in latest.get("checks", []) + latest.get("data_checks", []):
        if check.get("status") != "match":
            continue
        label = check.get("function", check["unit"])
        source = project_path(check["source"])
        symbol_map = project_path(check["symbol_map"])
        if digest(source) != check.get("source_sha256"):
            raise RuntimeError(f"current source hash differs from latest check for {label}: {source}")
        if digest(symbol_map) != check.get("map_sha256"):
            raise RuntimeError(f"current symbol-map hash differs from latest check for {label}: {symbol_map}")
        source_snapshot = check.get("source_snapshot")
        if source_snapshot and digest(project_path(source_snapshot)) != check.get("source_sha256"):
            raise RuntimeError(f"source snapshot hash differs from latest check for {label}")
        map_snapshot = check.get("map_snapshot")
        if map_snapshot and digest(project_path(map_snapshot)) != check.get("map_sha256"):
            raise RuntimeError(f"symbol-map snapshot hash differs from latest check for {label}")
        obj = project_path(check["object"])
        if check.get("object_file_sha256") and digest(obj) != check["object_file_sha256"]:
            raise RuntimeError(f"object file hash differs from latest check for {label}")
        checked_objects.add(str(obj))

    manifest_info = None
    if "data_manifest" in latest:
        path, actual = manifest_digest(latest["data_manifest"])
        declared = latest.get("data_manifest_sha256")
        if declared and declared != actual:
            raise RuntimeError("data manifest hash differs from latest report")
        manifest_info = {"path": path, "sha256": actual, "declared_sha256": declared}
    elif latest.get("data_manifest_sha256"):
        raise RuntimeError("latest report declares data_manifest_sha256 but has no data_manifest")
    return {"reference_path": str(reference), "reference_sha256": reference_sha,
            "parser_path": str(PSYQOBJ), "parser_sha256": parser_actual,
            "runtime_library_count": len(runtime_hashes), "checked_source_objects": len(checked_objects),
            "data_manifest": manifest_info}


def witnessed_generated(provenance_path: Path, latest: dict[str, Any]):
    """The gate's compiler-generated data claims, accepted only through tools/generated_witness.py
    against verify.py's own objects (the gate's sizing and anchoring are not trusted here)."""
    if not provenance_path.is_file():
        raise RuntimeError(f"gate provenance record is missing: {provenance_path}; run tools/gate.py")
    record = read_json(provenance_path)
    contract = record["contract"]
    if contract["sha256"] != latest.get("reference_sha256"):
        raise RuntimeError("gate provenance record belongs to a different executable")
    claims = [{**o, "label": o["symbol"]} for o in record["owners"] if o["kind"] == "generated"]
    if not claims:
        return []
    spec = importlib.util.spec_from_file_location("generated_witness_plan", PROJECT / "tools/generated_witness.py")
    witness = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(witness)
    raw = (WORKSPACE / contract["reference"]).read_bytes()
    witnessed, issues = witness.witness(claims, latest, raw, as_int(contract["load_address"]),
                                        contract["header_size"])
    if issues or witnessed != sum(int(c["size"]) for c in claims):
        raise RuntimeError("generated-data claims fail the witness: " + "; ".join(issues[:5]))
    return claims


def build_plan(latest_path: Path, runtime_path: Path, runtime_symbols_path: Path, lock_path: Path,
               runtime_data_path: Path = DEFAULT_RUNTIME_DATA, provenance_path: Path = DEFAULT_PROVENANCE):
    latest, runtime, runtime_symbols = map(read_json, (latest_path, runtime_path, runtime_symbols_path))
    validation = validate_provenance(latest, runtime, runtime_symbols, lock_path)
    runtime_data = read_json(runtime_data_path)
    spec = importlib.util.spec_from_file_location("runtime_data", PROJECT / "analysis/runtime_data.py")
    data_analyzer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(data_analyzer)
    validation["runtime_initialized_data"] = data_analyzer.validate_report(runtime_data)
    if runtime_data["inputs"]["runtime_linked_report_sha256"] != digest(runtime_path):
        raise RuntimeError("Runtime initialized data belongs to different linked-text evidence")
    if runtime_data["inputs"]["runtime_symbol_report_sha256"] != digest(runtime_symbols_path):
        raise RuntimeError("Runtime initialized data belongs to different symbol evidence")
    if latest.get("reference_sha256") != runtime.get("executable_sha256"):
        raise RuntimeError("latest C report and runtime report refer to different executable hashes")
    if runtime.get("executable_sha256") != runtime_symbols.get("executable_sha256"):
        raise RuntimeError("runtime linked and symbol reports refer to different executable hashes")
    load = as_int(latest.get("load_address", "0x80010000"))
    payload_size = int(latest["payload_size"])
    payload_end = load + payload_size
    generated = witnessed_generated(Path(provenance_path), latest)
    ranges = collect_ranges(latest, runtime, runtime_data, generated)
    union, gaps, overlaps = range_union(ranges, load, payload_end)
    category_bytes = {kind: sum(x["size"] for x in range_union([r for r in ranges if r["kind"] == kind],
                                                                 load, payload_end)[0])
                      for kind in sorted({r["kind"] for r in ranges})}
    data_exports = [definition for section in runtime_data.get("supplemental_data_only_section_results", [])
                    if section.get("status") == "full_match"
                    for definition in section.get("exported_definitions", [])
                    if definition.get("address") is not None]
    symbols_with_data = {**runtime_symbols,
                         "exports": runtime_symbols.get("exports", []) + data_exports}
    ref_list, object_provenance, alias_groups = collect_references(latest, ranges, symbols_with_data)
    ref_list.extend(collect_runtime_references(runtime, ranges, alias_groups))
    ref_list.sort(key=lambda r: (r["address"] or "", r["symbol"], r["units"]))
    unresolved = [r for r in ref_list if r["status"] != "covered"]
    alias_refs = [r for r in ref_list if r["alias_reconciliation_required"]]
    counts = defaultdict(int)
    for r in ref_list:
        counts[r["status"]] += 1
    covered_bytes = sum(x["size"] for x in union)
    return {
        "generated_from": {"latest_report": str(latest_path), "latest_report_sha256": digest(latest_path),
                           "runtime_linked_report": str(runtime_path), "runtime_linked_report_sha256": digest(runtime_path),
                           "runtime_symbol_report": str(runtime_symbols_path), "runtime_symbol_report_sha256": digest(runtime_symbols_path),
                           "runtime_data_report": str(runtime_data_path), "runtime_data_report_sha256": digest(runtime_data_path),
                           "toolchain_lock": str(lock_path), "toolchain_lock_sha256": digest(lock_path),
                           "gate_provenance": str(provenance_path),
                           "gate_provenance_sha256": digest(Path(provenance_path)),
                           "reference_sha256": latest["reference_sha256"],
                           "compiler": latest.get("compiler"), "assemblers": latest.get("assemblers"),
                           "runtime": latest.get("runtime")},
        "scope": "Evidence-backed remaining work for full executable reconstruction. This report does not rebuild the executable or reconstruct uncovered original bytes.",
        "validated_provenance": validation,
        "payload": {"load_address": hex(load), "end_address_exclusive": hex(payload_end),
                    "size_bytes": payload_size, "covered_union_bytes": covered_bytes,
                    "uncovered_gap_bytes": sum(g["size"] for g in gaps),
                    "coverage_percent": round(100.0 * covered_bytes / payload_size, 6)},
        "category_bytes": category_bytes,
        "certified_range_union": union,
        "uncovered_payload_gaps": gaps,
        "overlapping_certified_ranges": overlaps,
        "range_counts": {"recovered_c_functions": sum(1 for r in ranges if r["kind"] == "recovered_c_function"),
                         "recovered_c_initialized_data": sum(1 for r in ranges if r["kind"] == "recovered_c_initialized_data"),
                         "compiler_generated_data": sum(1 for r in ranges if r["kind"] == "compiler_generated_data"),
                         "asm": sum(1 for r in ranges if r["kind"] == "asm"),
                         "library_asm": sum(1 for r in ranges if r["kind"] == "library_asm"),
                         "runtime_object_modules": sum(1 for r in ranges if r["kind"] == "runtime_object_text"),
                         "runtime_initialized_sections": sum(1 for r in ranges if r["kind"] == "runtime_initialized_section")},
        "external_references": {"unique_symbol_unit_address_records": len(ref_list),
                                "status_counts": dict(sorted(counts.items())),
                                "uncovered_or_unresolved": unresolved,
                                "all_references": ref_list},
        "linkage_alias_reconciliation": [{"address": hex(addr), "names": names,
                                          "reason": "distinct mapped names share a final VA; reconcile export/alias identity and type before linking"}
                                         for addr, names in sorted(alias_groups.items())],
        "references_with_aliases": alias_refs,
        "provenance": {"source_objects": object_provenance,
                       "runtime_objects": runtime_provenance(runtime, runtime_symbols),
                       "runtime_initialized_sections": [s for s in runtime_data["section_results"] + runtime_data.get("supplemental_data_only_section_results", []) if s["status"] == "full_match"]},
        "reconstruction_backlog": [
            {"item": "Recover or account for every uncovered payload gap", "gap_count": len(gaps),
             "bytes": sum(g["size"] for g in gaps), "evidence": "exact certified range union and gap boundaries above"},
            {"item": "Resolve external symbols that lack a final VA", "count": counts.get("unresolved_symbol_address", 0),
             "evidence": "PsyQ object xrefs compared with each unit's symbol map"},
            {"item": "Recover mapped code/data targets outside certified ranges", "count": counts.get("mapped_uncovered_code_target", 0) + counts.get("mapped_uncovered_data_target", 0) + counts.get("mapped_uncovered_code_or_data_target", 0),
             "evidence": "relocation target VA is mapped but uncovered by C, initialized or generated data, asm, or full-match runtime text/data"},
            {"item": "Reconcile mapped aliases at shared VAs", "count": len(alias_refs),
             "evidence": "all distinct names at each address are listed above; type compatibility is intentionally unasserted"}
        ],
        "limitations": ["Coverage union includes only full-match C functions, matched initialized C data, compiler-generated data that the gate places and tools/generated_witness.py accepts, hand-written asm units (kind asm, never recovered C), library asm units (kind library_asm: Sony Runtime Library variant modules, never recovered C and never certified library), and full-match Runtime Library 3.3 module text and initialized sections. Uncovered gaps exclude all of them.",
                        "BSS, uninitialized globals, linker placement, entrypoint/startup composition, and uncovered original bytes are not reconstructed by this inventory.",
                        "External target type compatibility is not inferred from matching names or addresses."]
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--latest", type=Path, default=DEFAULT_LATEST)
    ap.add_argument("--runtime-linked", type=Path, default=DEFAULT_RUNTIME_LINKED)
    ap.add_argument("--runtime-symbols", type=Path, default=DEFAULT_RUNTIME_SYMBOLS)
    ap.add_argument("--runtime-data", type=Path, default=DEFAULT_RUNTIME_DATA)
    ap.add_argument("--toolchain-lock", type=Path, default=DEFAULT_LOCK)
    ap.add_argument("--provenance", type=Path, default=DEFAULT_PROVENANCE,
                    help="tools/gate.py's provenance record (source of the generated-data claims)")
    ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--dry-run", action="store_true", help="compute and summarize without writing the report")
    args = ap.parse_args()
    paths = [p.resolve() for p in (args.latest, args.runtime_linked, args.runtime_symbols, args.toolchain_lock)]
    plan = build_plan(*paths, runtime_data_path=args.runtime_data.resolve(),
                      provenance_path=args.provenance.resolve())
    if args.dry_run:
        print(json.dumps({"payload": plan["payload"], "category_bytes": plan["category_bytes"],
                          "range_counts": plan["range_counts"],
                          "reference_counts": plan["external_references"]["status_counts"],
                          "gap_count": len(plan["uncovered_payload_gaps"]),
                          "alias_count": len(plan["linkage_alias_reconciliation"])}, indent=2))
        return 0
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
