"""Extents of initialized data items, sized from a unit's GCC/ASPSX assembly listing.

Shared by tools/gate.py and tools/data_check.py so that both tools give a datum the
same extent. The rule:

- An item runs from its label over the bytes its directives emit (``.word``,
  ``.half``, ``.ascii``, explicit ``.space`` ...). Padding that ``.align`` inserts
  before the next item belongs to no item; it is never placed from source and the
  fallback supplies it.
- A data definition in ``config/data.json`` places exactly one item: it starts at
  the item's offset and its size is the item's size.
- A definition is keyed by its exported name, by the name of a static the unit map
  declares in ``static_data`` (its local symbol), or, for a compiler-labelled item
  (a jump table, ``$L50``), by its position ``<section>+<hex offset>`` in the
  object. Names are never invented for unnamed items.
- Items begin only at labels; section ends close them. A relocation target
  (section+addend) is a referenced byte, not a boundary: it belongs to the item
  whose emitted bytes contain it, so ``$LC0+4`` (the second word an inline
  ``strcpy`` reads from an 8-byte literal) is an interior reference into $LC0.
  tools/generated_witness.py applies the same rule from the object alone.

The listing's section totals must equal the object's section sizes, otherwise no
extent is trusted.
"""
from __future__ import annotations

DATA_SECTIONS = (".rdata", ".data", ".sdata")
_ESCAPES = {"n": 10, "t": 9, "r": 13, "b": 8, "f": 12, "v": 11, "a": 7, "\\": 92, '"': 34, "'": 39}
_SIZES = {".byte": 1, ".half": 2, ".short": 2, ".word": 4, ".float": 4, ".double": 8}


class ExtentError(ValueError):
    pass


def ascii_length(literal):
    """Byte length of one quoted .ascii operand as GCC writes it (C escapes, 1-3 digit octal)."""
    if len(literal) < 2 or literal[0] != '"' or literal[-1] != '"':
        raise ExtentError(f"unsupported .ascii operand {literal[:40]!r}")
    body, i, n = literal[1:-1], 0, 0
    while i < len(body):
        if body[i] == "\\":
            i += 1
            if i < len(body) and body[i] in "01234567":
                j = i
                while j < len(body) and j - i < 3 and body[j] in "01234567":
                    j += 1
                i = j
            elif i < len(body) and body[i] in _ESCAPES:
                i += 1
            else:
                raise ExtentError(f"unsupported escape in .ascii operand {literal[:40]!r}")
        else:
            i += 1
        n += 1
    return n


def split_operands(text):
    """Comma-separated operands, keeping commas inside quotes."""
    out, current, quoted, i = [], "", False, 0
    while i < len(text):
        ch = text[i]
        if ch == "\\" and quoted:
            current += text[i:i + 2]
            i += 2
            continue
        if ch == '"':
            quoted = not quoted
        if ch == "," and not quoted:
            out.append(current.strip())
            current = ""
        else:
            current += ch
        i += 1
    if current.strip():
        out.append(current.strip())
    return out


def asm_data_items(asm_text):
    """Labelled items of the initialized data sections in a GCC/ASPSX assembly listing.

    Returns {section: {"size": total, "items": [{"label", "offset", "size"}]}}. An item runs
    from its label over the bytes emitted after it; padding inserted by ``.align`` before the
    next label is not part of any item, explicit ``.space`` is.
    """
    sections, section, current = {}, ".text", {}
    for line in asm_text.splitlines():
        text = line.split("#", 1)[0].strip() if not line.lstrip().startswith(".ascii") else line.strip()
        if not text:
            continue
        words = text.split(None, 1)
        directive, rest = words[0], (words[1] if len(words) > 1 else "")
        if directive in (".text", ".data", ".sdata", ".rdata", ".bss", ".sbss"):
            section = directive
            continue
        if directive == ".section":
            section = split_operands(rest)[0]
            continue
        if section not in DATA_SECTIONS:
            continue
        state = sections.setdefault(section, {"size": 0, "items": []})
        if text.endswith(":") and " " not in text and "\t" not in text:
            current[section] = {"label": text[:-1], "offset": state["size"], "size": 0}
            state["items"].append(current[section])
            continue
        if directive == ".align":
            alignment = 1 << int(rest, 0)
            state["size"] = (state["size"] + alignment - 1) & -alignment
            current[section] = None
            continue
        if directive in (".ascii", ".asciiz"):
            n = sum(ascii_length(op) + (directive == ".asciiz") for op in split_operands(rest))
        elif directive in _SIZES:
            n = _SIZES[directive] * len(split_operands(rest))
        elif directive == ".space":
            n = int(rest, 0)
        elif directive.startswith(".") and directive not in (".dword", ".quad", ".fill", ".org", ".skip"):
            continue  # .globl, .type, .size and other non-emitting directives
        else:
            raise ExtentError(f"unsupported data directive {text!r} in {section}")
        if current.get(section) is None:
            current[section] = {"label": None, "offset": state["size"], "size": 0}
            state["items"].append(current[section])
        current[section]["size"] += n
        state["size"] += n
    return sections


def static_labels(asm_text):
    """{name: (section, offset)} of the unexported named items of the initialized data sections.

    A label of ``.sdata``/``.data``/``.rdata`` that is not a compiler label (``$LC0``, ``$L5``)
    and that no ``.globl`` exports is a ``static`` datum (file scope, or function scope as
    ``name.N``). ASPSX writes no local-symbol record for it, so the listing is where it is found;
    tools/lnk_symbols.py (``initialised_statics``) holds the map to these labels.
    """
    exported = set()
    for line in asm_text.splitlines():
        words = line.split("#", 1)[0].split(None, 1)
        if len(words) == 2 and words[0] == ".globl":
            exported.add(split_operands(words[1])[0])
    out = {}
    for section, state in asm_data_items(asm_text).items():
        for item in state["items"]:
            label = item["label"]
            if not label or label.startswith("$") or label in exported:
                continue
            if label in out:
                raise ExtentError(f"static label {label} is defined twice")
            out[label] = (section, item["offset"])
    return out


def check_sizes(items, present):
    """Raise unless the listing's section totals equal the object's {section: size}."""
    sized = {name: state["size"] for name, state in items.items() if state["size"]}
    present = {name: size for name, size in present.items() if size}
    if sized != present:
        raise ExtentError(f"assembly sizes {sized} differ from object sections {present}")


def position_key(key):
    """(section, offset) for a positional key such as ``.rdata+0x38``, else None."""
    section, plus, offset = key.partition("+")
    if not plus or section not in DATA_SECTIONS:
        return None
    try:
        return section, int(offset, 16)
    except ValueError:
        return None


def definition_position(key, definition, exported):
    """(section name, object offset) a data definition names.

    ``exported`` maps the names a definition may use to (section name, offset): exported
    names, and the unit's declared initialised statics (tools/lnk_symbols.py,
    ``named_data``). A definition keyed by
    position may repeat its section and offset as fields; they must agree. Explicit
    section and offset fields under a name are refused: that name is invented.
    """
    position = position_key(key)
    if position is not None:
        given = definition.get("section", position[0]), int(definition.get("offset", position[1]))
        if given != position:
            raise ExtentError(f"{key}: fields name {given[0]}+{given[1]:#x}")
        return position
    if "section" in definition or "offset" in definition:
        raise ExtentError(f"{key}: an unnamed item is keyed by its position "
                          f"(<section>+<hex offset>), not by an invented name")
    if key not in exported:
        raise ExtentError(f"missing object definition: {key}")
    return exported[key]


def item_at(items, section, offset):
    """The item of ``section`` that starts at ``offset``, or None."""
    return next((i for i in items.get(section, {}).get("items", ())
                 if i["offset"] == offset and i["size"]), None)


def definition_extent(items, key, section, offset, size):
    """Raise unless the definition at section+offset with ``size`` is exactly one item."""
    item = item_at(items, section, offset)
    if item is None:
        raise ExtentError(f"{key}: no assembly item starts at {section}+{offset:#x}")
    if item["size"] != size:
        raise ExtentError(f"{key}: item extent {item['size']} differs from expected {size}")
    return item


def item_containing(items, section, offset):
    """The item of ``section`` whose emitted bytes contain ``offset``, or None (padding)."""
    return next((i for i in items.get(section, {}).get("items", ())
                 if i["offset"] <= offset < i["offset"] + i["size"]), None)
