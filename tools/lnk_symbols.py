"""Symbols of a PsyQ LNK object that psyqobj does not keep, and the rule that locates a
unit's functions and static storage among them.

psyqobj (``../shared-tools/psyqobj.py``, pinned by hash in config/toolchain.lock.json) keeps
the exported definitions (XDEF, record 12) only. ASPSX also writes

- a local-symbol record (18: section, offset, name) for every ``static`` function in ``.text``
  and every uninitialised static variable (``.sbss``/``.bss``), file scope or function scope
  (GCC names it ``name.N``, e.g. ``pad.18``). A static with an initialiser (``.sdata``,
  ``.data``, ``.rdata``) gets none, like compiler labels ($LC0, $L5);
- an XBSS record (48: number, section, size, name) for every COMMON symbol it declares.

Unit maps (``config/<unit>.json``) declare linkage, and the object must agree:

- ``functions``: ``{"name": {"address", "size"}}`` for an exported function, and
  ``{"name": {"address", "size", "static": true}}`` for a static one;
- ``static_data``: ``{"name": {"address", "size", "section"}}``, one entry per static datum.
  In the uninitialised sections (``.sbss``, ``.bss``) each is a local symbol of the object,
  its address the unit's section base plus the symbol's offset and its size the extent to
  the next symbol. In the initialised sections (``.sdata``, ``.data``, ``.rdata``) ASPSX
  writes no local-symbol record: such a static is a label of the unit's assembly listing
  that no ``.globl`` exports (tools/data_extent.py, ``static_labels``), and it is placed like
  an exported datum, by the ``config/data.json`` definition of its name, whose address and
  size the entry repeats (``initialised_statics``). The definition is exactly one assembly
  item at the label, data_check.py compares its bytes with retail and the gate links it as
  source data.

Static names are scoped to their unit: two units may each define a static of the same name,
the link never binds one globally, and no other unit may bind a global to a static's
address (a static has no external name). Callers enforce that through
``verify.validate_symbol_bindings``; this module checks one object against its own map.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path
import struct

WORKSPACE = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location("psyqobj_symbols", WORKSPACE / "shared-tools/psyqobj.py")
psyqobj = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(psyqobj)

UNINITIALIZED = (".sbss", ".bss")
INITIALIZED = (".sdata", ".data", ".rdata")
_FIXED = {22: 2, 24: 2, 26: 2, 30: 6, 32: 4, 34: 0, 36: 1, 38: 2, 42: 2, 44: 3, 46: 1, 50: 2,
          52: 3, 54: 4, 56: 6, 58: 8, 60: 2, 76: 10, 78: 10, 80: 10}


class SymbolError(Exception):
    pass


def num(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def records(data):
    """XBSS (COMMON) and local-symbol records of one LNK object.

    Returns {"xbss": {number: (section id, size, name)}, "locals": [(section id, offset, name)]}.
    """
    if data[:4] != b"LNK\x02":
        raise SymbolError("not a PsyQ LNK object")
    i, xbss, local = 4, {}, []
    while i < len(data):
        op = data[i]
        i += 1
        if op == 0:
            break
        if op == 2:
            i += 2 + struct.unpack_from("<H", data, i)[0]
        elif op == 6:
            i += 2
        elif op == 8:
            i += 4
        elif op == 10:
            i += 3
            _, i = psyqobj.expr(data, i)
        elif op == 12:
            i += 8
            _, i = psyqobj.pstr(data, i)
        elif op in (14, 28):
            i += 2
            _, i = psyqobj.pstr(data, i)
        elif op == 16:
            i += 5
            _, i = psyqobj.pstr(data, i)
        elif op == 18:
            section, offset = struct.unpack_from("<HI", data, i)
            name, i = psyqobj.pstr(data, i + 6)
            local.append((section, offset, name))
        elif op == 20:
            i += 3
            _, i = psyqobj.pstr(data, i)
        elif op == 40:
            i += 6
            _, i = psyqobj.pstr(data, i)
        elif op == 48:
            number, section, size = struct.unpack_from("<HHI", data, i)
            name, i = psyqobj.pstr(data, i + 8)
            xbss[number] = (section, size, name)
        elif op == 74:
            i += 28
            _, i = psyqobj.pstr(data, i)
        elif op == 82:
            i += 14
            _, i = psyqobj.pstr(data, i)
        elif op == 84:
            i += 14
            i += 2 + 4 * struct.unpack_from("<H", data, i)[0]
            _, i = psyqobj.pstr(data, i)
            _, i = psyqobj.pstr(data, i)
        elif op in _FIXED:
            i += _FIXED[op]
        else:
            raise SymbolError(f"unsupported LNK record {op} at {i - 1:#x}")
    return {"xbss": xbss, "locals": local}


def load(path):
    """One LNK object parsed by psyqobj, with ``locals`` = [(name, section id, offset)] (xdefs' order)."""
    data = Path(path).read_bytes()
    if data[:4] != b"LNK\x02":
        raise SymbolError(f"{path} is not a single PsyQ LNK object")
    obj = psyqobj.parse_lnk(data, str(path))
    obj.locals = [(name, sid, offset) for sid, offset, name in records(data)["locals"]]
    return obj


def is_static(entry):
    """The map's linkage flag: absent or false is exported, true is static; anything else is refused."""
    value = entry.get("static", False) if isinstance(entry, dict) else False
    if type(value) is not bool:
        raise SymbolError(f"'static' must be true or false, not {value!r}")
    return value


def static_names(spec):
    """{name: (address, size)} of a map's static functions and static data."""
    out = {}
    for name, entry in spec.get("functions", {}).items():
        if is_static(entry):
            out[name] = (num(entry["address"]), int(entry["size"]))
    for name, entry in spec.get("static_data", {}).items():
        out[name] = (num(entry["address"]), int(entry["size"]))
    return out


def text_symbols(obj, functions):
    """{name: (section id, offset)} of every mapped function.

    An exported function is exactly one ``.text`` XDEF and no local symbol of that name; a
    static one is exactly one ``.text`` local symbol and no XDEF of that name. Every ``.text``
    symbol of the object must be a mapped function, so nothing in ``.text`` is unclaimed.
    """
    text = {sid for sid, name in obj.sections.items() if name == ".text"}
    exported = [(n, s, o) for n, s, o in obj.xdefs if s in text]
    local = [(n, s, o) for n, s, o in getattr(obj, "locals", ()) if s in text]
    problems, located = [], {}
    for name, entry in functions.items():
        static = is_static(entry)
        ex = [(s, o) for n, s, o in exported if n == name]
        lo = [(s, o) for n, s, o in local if n == name]
        own, other = (lo, ex) if static else (ex, lo)
        if other:
            problems.append(f"{name} is {'exported' if static else 'static'} in the object but the map "
                            f"declares it {'static' if static else 'exported'}")
        elif len(own) != 1:
            problems.append(f"{name} is defined {len(own)} times as a {'static' if static else 'exported'} "
                            "text symbol")
        else:
            located[name] = own[0]
    extra = sorted({n for n, _, _ in exported + local} - set(functions))
    if extra:
        problems.append(f"text symbols without a map entry: {extra}")
    if problems:
        raise SymbolError("; ".join(problems))
    return located


def text_partition(obj, functions, located):
    """Every ``.text`` byte belongs to exactly one mapped function, in object order."""
    for section, section_name in obj.sections.items():
        if section_name != ".text":
            continue
        cursor = 0
        for offset, name in sorted((o, n) for n, (s, o) in located.items() if s == section):
            if offset != cursor:
                raise SymbolError(f"compiled text contains an unowned prefix or inconsistent extent before {name}: "
                                  f"expected offset {cursor}, actual {offset}")
            size = int(functions[name]["size"])
            if size <= 0:
                raise SymbolError(f"invalid complete text extent for {name}")
            cursor += size
        actual = len(obj.code.get(section, b""))
        if cursor != actual:
            raise SymbolError(f"compiled text partition covers {cursor} bytes but complete section has {actual}")


def static_storage(obj, spec):
    """[(name, section name, offset, size, address)] of the unit's uninitialised static storage.

    Every local symbol of ``.sbss``/``.bss`` must be declared once in the map's ``static_data``
    with its section, its size equal to its extent in the object (to the next symbol of the
    section or the section end), and its address equal to the unit's section base
    (``sections``, through which the unit's relocations resolve it) plus its offset. Every
    byte of those sections must belong to a declared static: no exported or COMMON storage
    is modelled there, and nothing is unclaimed.
    """
    declared = spec.get("static_data", {})
    problems, out, seen = [], [], set()
    for name, entry in declared.items():
        if not isinstance(entry, dict) or entry.get("section") not in UNINITIALIZED + INITIALIZED:
            problems.append(f"static_data {name} needs a section among {'/'.join(UNINITIALIZED + INITIALIZED)}")
    for sid, section in obj.sections.items():
        if section in INITIALIZED and any(s == sid for _, s, _ in getattr(obj, "locals", ())):
            problems.append(f"{section} local symbols are not modelled: "
                            f"{sorted(n for n, s, _ in obj.locals if s == sid)}")
        if section not in UNINITIALIZED:
            continue
        size = len(obj.code.get(sid, b""))
        exported = sorted(n for n, s, _ in obj.xdefs if s == sid)
        if exported:
            problems.append(f"exported {section} definitions are not modelled: {exported}")
        local = sorted((o, n) for n, s, o in getattr(obj, "locals", ()) if s == sid)
        if size and (not local or local[0][0] != 0):
            problems.append(f"{section} bytes before the first static are unowned")
        base = spec.get("sections", {}).get(section)
        for index, (offset, name) in enumerate(local):
            end = local[index + 1][0] if index + 1 < len(local) else size
            entry = declared.get(name)
            if name in seen:
                problems.append(f"static {name} is defined twice")
                continue
            seen.add(name)
            if entry is None:
                problems.append(f"{section} static {name} (+{offset:#x}, {end - offset} bytes) has no static_data entry")
                continue
            if entry.get("section") != section:
                problems.append(f"static {name} is in {section}, the map says {entry.get('section')}")
            if int(entry["size"]) != end - offset:
                problems.append(f"static {name} occupies {end - offset} bytes, the map says {entry['size']}")
            if base is None:
                problems.append(f"static {name} needs the unit's {section} base in 'sections'")
            elif num(entry["address"]) != num(base) + offset:
                problems.append(f"static {name} resolves to {num(base) + offset:#x} ({section} base + {offset:#x}), "
                                f"the map says {num(entry['address']):#x}")
            out.append((name, section, offset, end - offset, num(entry["address"])))
    for name in sorted(set(declared) - seen):
        if isinstance(declared[name], dict) and declared[name].get("section") in INITIALIZED:
            continue  # located by the listing: initialised_statics
        problems.append(f"static_data {name} is not a local symbol of the object's {'/'.join(UNINITIALIZED)}")
    if problems:
        raise SymbolError("; ".join(problems))
    return out


def initialised_statics(spec, definitions, labels):
    """Problems with the unit's initialised statics (rule: module docstring); a list.

    ``labels`` is {name: (section, offset)} of the listing's unexported data labels
    (``data_extent.static_labels``). Each must be one ``static_data`` entry of that section,
    each initialised entry must be one of them, and each needs the ``config/data.json``
    definition of its name with the same address and size: a static without one would be an
    unverified claim on retail storage.
    """
    problems = []
    declared = {n: e for n, e in spec.get("static_data", {}).items()
                if isinstance(e, dict) and e.get("section") in INITIALIZED}
    for name, (section, offset) in sorted(labels.items()):
        entry = declared.get(name)
        if entry is None:
            problems.append(f"{section} static {name} (+{offset:#x}) has no static_data entry")
        elif entry["section"] != section:
            problems.append(f"static {name} is in {section}, the map says {entry['section']}")
    for name, entry in sorted(declared.items()):
        if name not in labels:
            problems.append(f"static_data {name} is not an unexported {entry['section']} label of the listing")
            continue
        definition = definitions.get(name)
        if definition is None:
            problems.append(f"initialised static {name} has no data definition of its unit")
        elif num(definition["address"]) != num(entry["address"]) or int(definition["size"]) != int(entry["size"]):
            problems.append(f"static {name} is {num(entry['address']):#x}+{int(entry['size'])} in the map but "
                            f"{num(definition['address']):#x}+{int(definition['size'])} in its data definition")
    return problems


def named_data(obj, spec, labels):
    """{name: (section name, offset)} of the data a ``config/data.json`` definition may name:
    the object's exported definitions, and the unit's declared initialised statics at their
    listing labels. A static's name is scoped to this unit."""
    out = {name: (obj.sections[sid], offset) for name, sid, offset in obj.xdefs}
    declared = spec.get("static_data", {})
    for name, (section, offset) in labels.items():
        entry = declared.get(name)
        if isinstance(entry, dict) and entry.get("section") == section and name not in out:
            out[name] = (section, offset)
    return out


def audit(obj, spec):
    """Text location, text partition and static storage of one unit object; returns the located text symbols."""
    functions = spec.get("functions", {})
    located = text_symbols(obj, functions)
    text_partition(obj, functions, located)
    static_storage(obj, spec)
    return located
