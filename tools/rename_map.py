"""Regenerate config/style_renames.json, the Quake III style rename map (AGENTS.md, Project profile).

    python tools/rename_map.py            # rewrite the map for the current tree, keeping approved entries
    python tools/rename_map.py --check    # print the entries a regeneration would add or drop; write nothing

Run it before rename.py applies the map: once applied, the old names it records are gone from the tree.

The map is built mechanically from the tree: every function, global, typedef, tag, struct member,
enum value, constant and function-like macro declared in src/ and include/ or recorded in a symbol
map, with its owner unit. Names are chosen by the module-prefix table (Prefix_VerbNoun with the verb
moved first, g_ + lowerCamel, lowerCamel_t / lowerCamel_s, lowerCamel members), on top of the hand
decisions in config/style_overrides.json. SDK, library, lineage and placeholder names are kept with
the reason. Entries already in the map were approved by the user and are carried over verbatim; only
identifiers new to the tree get fresh entries, and entries whose name is gone from the tree are dropped.
tools/rename.py applies the map.
"""
import argparse
import json
from pathlib import Path
import re
import sys
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rules  # noqa: E402
import verify  # noqa: E402
import rename  # noqa: E402
from rules import lex, parse_top, match_close, split_declarators, KEYWORDS  # noqa: E402

MAP = ROOT / "config" / "style_renames.json"
OVERRIDES = ROOT / "config" / "style_overrides.json"


def inventory():
    """Identifiers declared in src/ and include/, by kind, and every code token with its files."""
    files = sorted((ROOT / "src").glob("*.c")) + sorted((ROOT / "include").glob("*.H"))
    inv = {"functions": {}, "globals": {}, "types": {}, "tags": {}, "members": defaultdict(dict),
           "enum_values": {}, "defines": {}, "fmacros": {}}
    alltok = defaultdict(set)

    def struct_members(toks, a, b, owner, file):
        # toks[a] == '{', toks[b] == '}'
        i = a + 1; start = i
        while i < b:
            t = toks[i]
            if t.text in ("struct", "union") and i + 1 < b and (toks[i+1].text == "{" or (toks[i+1].kind=="id" and i+2<b and toks[i+2].text=="{")):
                k = i + 1 if toks[i+1].text == "{" else i + 2
                j = match_close(toks, k)
                struct_members(toks, k, j, owner, file)  # nested members accessed via outer
                i = j + 1
                continue
            if t.text in "([{":
                i = match_close(toks, i) + 1; continue
            if t.text == ";":
                seq = toks[start:i]
                # drop nested bodies
                flat = []; q = 0
                while q < len(seq):
                    if seq[q].text == "{":
                        depth = 0
                        while q < len(seq):
                            if seq[q].text == "{": depth += 1
                            if seq[q].text == "}":
                                depth -= 1
                                if depth == 0: break
                            q += 1
                        q += 1; continue
                    flat.append(seq[q]); q += 1
                for n, f in split_declarators(flat):
                    inv["members"][owner].setdefault(n, file)
                start = i + 1
            i += 1

    for p in files:
        src = lexf(p)
        file = ("src/" if p.suffix == ".c" else "include/") + p.name
        unit = p.stem if p.suffix == ".c" else p.name
        toks = src.toks
        for t in toks:
            if t.kind == "id": alltok[t.text].add(file)
        for _, _, d in src.directives:
            m = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)(\()?", d)
            if m:
                (inv["fmacros"] if m.group(2) else inv["defines"]).setdefault(m.group(1), file)
            for x in re.findall(r"[A-Za-z_]\w*", d): alltok[x].add(file)
        # aggregates
        i = 0
        while i < len(toks):
            t = toks[i]
            if t.text in ("struct", "union", "enum"):
                tag = None; k = i + 1
                if k < len(toks) and toks[k].kind == "id" and toks[k].text not in KEYWORDS:
                    tag = toks[k].text; k += 1
                if tag:
                    inv["tags"].setdefault(tag, {"kind": t.text, "file": file})
                if k < len(toks) and toks[k].text == "{":
                    j = match_close(toks, k)
                    # typedef name after }
                    tdname = None
                    q = j + 1
                    if q < len(toks) and toks[q].kind == "id": tdname = toks[q].text
                    owner = tag or tdname or f"<anon@{file}:{t.line}>"
                    if t.text == "enum":
                        q = k + 1; expect = True
                        depth = 0
                        while q < j:
                            x = toks[q]
                            if x.text in "([": depth += 1
                            elif x.text in ")]": depth -= 1
                            if depth == 0 and expect and x.kind == "id":
                                inv["enum_values"].setdefault(x.text, {"file": file, "enum": owner}); expect = False
                            if depth == 0 and x.text == ",": expect = True
                            q += 1
                    else:
                        struct_members(toks, k, j, owner, file)
                    i = k + 1  # descend (nested)
                    continue
            i += 1
        funcs, decls = parse_top(src)
        for f in funcs:
            e = inv["functions"].setdefault(f.name, {"defined": None, "decl": []})
            e["defined"] = file; e["static"] = f.static
        for d in decls:
            if d.kind == "typedef":
                for n in d.names:
                    inv["types"].setdefault(n, file)
            elif d.kind in ("prototype", "static_prototype") or (d.kind == "extern" and any(f for _, f in split_declarators(d.toks))):
                for n, isf in split_declarators(d.toks):
                    if isf:
                        inv["functions"].setdefault(n, {"defined": None, "decl": []})["decl"].append(file)
                    else:
                        inv["globals"].setdefault(n, {"file": file, "static": d.static})
            elif d.kind in ("extern", "data"):
                for n in d.names:
                    g = inv["globals"].setdefault(n, {"file": file, "static": d.static})
                    if d.kind == "data": g["def"] = file; g["static"] = d.static


    inv["members"] = dict(inv["members"])
    return inv, {k: sorted(v) for k, v in alltok.items()}


# ---------------------------------------------------------------- keep sets
def lexf(p):
    return lex(p, p.name, p.read_text(encoding="utf-8", errors="replace"))

def sdk_names():
    """(declared names, every name) of the Runtime 3.3 headers; declared excludes prototype parameters."""
    sdk_decl = set()      # SDK declared names: macros, types, members, functions, globals (not prototype parameters)
    sdk_all = set()
    for p in rules.sdk_include_dir().rglob("*"):
        if not p.is_file():
            continue
        s = lexf(p)
        depth = 0
        for t in s.toks:
            if t.text == "(":
                depth += 1
            elif t.text == ")":
                depth -= 1
            if t.kind == "id":
                sdk_all.add(t.text)
                if depth == 0:
                    sdk_decl.add(t.text)
        for _, _, d in s.directives:
            m = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)", d)
            if m:
                sdk_decl.add(m.group(1))
            sdk_all |= set(re.findall(r"[A-Za-z_]\w*", d))
    return sdk_decl, sdk_all


sdk_decl, sdk_all = sdk_names()
lib_syms = set()
for p in (ROOT / "config").glob("lib*.json"):
    d = json.load(open(p))
    for k in ("functions", "symbols", "static_data"):
        lib_syms |= set(d.get(k, {}))
for p in (ROOT / "asm" / "lib").glob("*"):
    if p.is_file():
        txt = p.read_text(encoding="utf-8", errors="replace")
        lib_syms |= set(re.findall(r"^\s*\.(?:globl|global|extern|comm|lcomm)\s+([A-Za-z_]\w*)", txt, re.M))
        lib_syms |= set(re.findall(r"^([A-Za-z_]\w*):", txt, re.M))
rb = json.load(open(ROOT / "config/runtime_bindings.json"))
for b in rb.get("bindings", []):
    for n in b.get("name_or_section", []):
        if re.fullmatch(r"[A-Za-z_]\w*", n):
            lib_syms.add(n)
PLACEHOLDER = re.compile(r"^[fgs]_[0-9a-f]{5,8}$")
LINEAGE = {"TaskerInit", "TaskerAlloc", "TaskCreate", "TaskDie", "TaskDispatch", "AddTASKToFree", "GetTASKFromFree",
           "AddTASKToInUse", "RemoveTASKFromInUse", "TaskMEM", "TaskFree", "TaskUsed", "TASK", "MAXTASKS",
           "Priority", "Next", "Prev",
           "strKickCD", "strInit", "strCallback", "strNext", "strNextVlc", "strSync", "bound"}
INT_TYPEDEFS = {"s32", "s16", "u8", "u32", "u16"}
C_KEYWORDS = set(KEYWORDS) | {"auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else",
    "enum", "extern", "float", "for", "goto", "if", "int", "long", "register", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
    "inline", "asm", "typeof", "main"}

def keep_reason(name):
    if rename.is_placeholder(name) and not PLACEHOLDER.match(name):
        return "placeholder name (R7 open work); semantic name only from evidence"
    if name in LINEAGE:
        return "lineage spelling (Forsaken PC Tasker.c / Sony STR sample)"
    if name in lib_syms:
        return "library symbol (Runtime Library / asm/lib)"
    if name in sdk_decl:
        return "SDK spelling (Runtime Library 3.3 headers)"
    if PLACEHOLDER.match(name):
        return "address placeholder; semantic name only from evidence"
    return None

# ---------------------------------------------------------------- word helpers
ACRONYMS = {"cd", "spu", "vram", "ot", "xz", "xy", "rle", "lz", "mdec", "gte", "hud", "rgb", "rgb24", "clut", "sjis",
            "ascii", "ft4", "ft3", "f4", "abe", "uv", "be32", "dct", "vlc", "rcnt", "dma", "id", "bnd", "lzw",
            "cpu", "gpu", "tpage", "str", "io", "fx", "lod", "bss", "vsync", "rng"}
ACRONYM_FORM = {"id": "Id", "tpage": "TPage", "vsync": "VSync", "str": "Str", "fx": "Fx", "vblank": "VBlank",
                "nocull": "NoCull"}

def cap(w):
    if w in ACRONYM_FORM:
        return ACRONYM_FORM[w]
    if w in ACRONYMS:
        return w.upper()
    return w[:1].upper() + w[1:]

def lower_camel(name):
    """snake_case -> lowerCamel; names without inner underscores are kept."""
    lead = len(name) - len(name.lstrip("_"))
    trail = len(name) - len(name.rstrip("_"))
    core = name[lead:len(name) - trail] if trail else name[lead:]
    if "_" not in core:
        return name
    words = [w for w in core.split("_") if w != ""]
    out = words[0][:1].lower() + words[0][1:] if words[0].isupper() and len(words[0]) > 1 and False else words[0]
    for w in words[1:]:
        out += cap(w) if not w[0].isdigit() else w
    return "_" * lead + out + "_" * trail

def pascal(words):
    return "".join(cap(w) if not w[0].isdigit() else w for w in words)

VERBS = set("""init alloc free load unload draw update spawn link unlink add remove find upload start stop play set get clear
reset build prepare fixup create kill open close release run step tick poll enable disable mute unmute restore select launch
explode fire throw mark activate measure reserve apply store encode decode sync flush claim split raise lower wait pause
unpause resume render fill parse read write format delete scan advance show hide move turn face die hurt damage hit recoil
bleed eject collect unpack setup transform rotate check count print compute adjust accelerate decelerate recentre
increase decrease give discard bind project occupy walk swap install upload expire drop shatter knock attack trigger
fall climb""".split())
TAIL_MODIFIERS = {"all", "tall", "unscaled", "semi", "alternate", "slow", "large", "now", "up", "multiline",
                  "centered"}

UNIT_PREFIX = {
    "actor": "Actor", "player": "Player", "weapon_slots": "Weapon", "map_objects": "Object", "object_render": "Object",
    "doors": "Door", "lift": "Lift", "level_triggers": "Trigger", "command_sequencer": "Command",
    "particle": "Particle", "particle_hits": "Particle", "debris": "Debris", "effects": "Fx", "model_draw": "Model",
    "geometry": "Geo", "lights": "Light", "sprites": "Sprite", "texture_upload": "Tex", "text": "Text", "hud": "Hud",
    "countdown_timer": "Timer", "frontend": "Menu", "pause_menu": "Pause", "save_password": "Password",
    "memcard": "Card", "audio": "Snd", "audio_control": "Snd", "music_control": "Music", "movie": "Movie",
    "main": "Sys",
    # renderer assembly (user ruling: Model_; sprite_anims data are globals)
    "ot_depth": "Model", "poly_emit": "Model", "view_cells": "Model", "sprite_anims": "Sprite",
}
for u in ("subdiv_ft4", "subdiv_ft4_mirror", "subdiv_ft4_nocull", "subdiv_ft4_nocull_mirror", "subdiv_ft4_abe",
          "subdiv_ft4_abe_mirror", "subdiv_f4_abe"):
    UNIT_PREFIX[u] = "Model"
ASM_UNITS = {"ot_depth", "poly_emit", "view_cells", "sprite_anims"} | {u for u in UNIT_PREFIX if u.startswith("subdiv_")}
NO_PREFIX_UNITS = {"game_20000_2", "game_20000_3", "game_20000_8", "game_helpers7"}
STRIP = {
    "actor": [["actors"], ["actor"]], "player": [["player"]], "weapon_slots": [["weapon"]],
    "map_objects": [["map", "objects"], ["map", "object"], ["object"]], "object_render": [],
    "doors": [["doors"], ["door"]], "lift": [["lifts"], ["lift"]], "level_triggers": [],
    "command_sequencer": [["command"]], "particle": [["particles"], ["particle"]], "particle_hits": [["particle"]],
    "debris": [["debris"]], "effects": [["effects"], ["effect"]], "model_draw": [["model"]], "geometry": [],
    "lights": [["lights"], ["light"]], "sprites": [["sprites"], ["sprite"]], "texture_upload": [["texture"]],
    "text": [["text"]], "hud": [["hud"]], "countdown_timer": [["countdown"]], "frontend": [["menu"]],
    "pause_menu": [["pause", "menu"], ["pause"]], "save_password": [["password"]], "memcard": [["card"]],
    "audio": [["sound"], ["audio"]], "audio_control": [["sound"], ["audio"]], "music_control": [["music"]],
    "movie": [["movie"]], "main": [], "sprite_anims": [],
}

def main_prefix(name):
    if name.startswith("heap_") or name == "init_heap":
        return "Heap", [["heap"]]
    if "demo" in name.split("_"):
        return "Demo", [["demo"]]
    return "Sys", []

def func_new(name, unit):
    """returns (new, reworded_reason or None)"""
    words = name.split("_")
    if unit == "main":
        prefix, strip = main_prefix(name)
    else:
        prefix, strip = UNIT_PREFIX.get(unit), STRIP.get(unit, [])
    stripped = False
    for s in strip:
        if words[:len(s)] == s and len(words) > len(s):
            words = words[len(s):]
            stripped = True
            break
    if prefix == "Heap" and "heap" in words and len(words) > 1:
        words = [w for w in words if w != "heap"]
        stripped = True
    if prefix == "Demo" and "demo" in words and len(words) > 1:
        words = [w for w in words if w != "demo"]
        stripped = True
    reason = None
    is_task = "task" in words or "callback" in words or "handler" in words
    if not is_task and len(words) > 1:
        if words[-2:] == ["pool", "alloc"]:
            pass  # X_pool_alloc allocates from the pool, not the pool
        elif words[-1] in VERBS and words[0] not in VERBS:
            words = [words[-1]] + words[:-1]
            reason = "verb moved first"
        elif words[-2:] == ["pool", "alloc"]:
            pass  # X_pool_alloc allocates from the pool, not the pool
        else:
            v = len(words) - 1
            while v > 0 and words[v] in TAIL_MODIFIERS:
                v -= 1
            if 0 < v < len(words) - 1 and words[v] in VERBS and words[0] not in VERBS:
                words = [words[v]] + words[:v] + words[v + 1:]
                reason = "verb moved first"
    body = pascal(words)
    if unit in NO_PREFIX_UNITS or prefix is None:
        return body, reason
    return prefix + "_" + body, reason

def global_new(name):
    for pre in ("g_", "s_"):
        if name.startswith(pre):
            rest = name[len(pre):]
            if "_" not in rest:
                return name
            return pre + lower_camel(rest)
    return lower_camel(name)

def type_new(name, suffix="_t"):
    base = name[:1].lower() + name[1:]
    # leading acronym runs: "GlyphUV" fine; "TASK"-like all caps handled by keep
    m = re.match(r"^([A-Z]+)([A-Z][a-z].*)$", name)
    if m and len(m.group(1)) > 1:
        base = m.group(1).lower() + m.group(2)
    return base + suffix


def dump_compact(obj):
    lines = ["{"]
    keys = ["_doc", "_rules", "functions", "globals", "types", "tags", "members", "enums_constants", "macros_functionlike"]
    for ki, k in enumerate(keys):
        v = obj[k]
        comma = "," if ki < len(keys) - 1 else ""
        if k in ("_doc",):
            lines.append(f'  "{k}": {json.dumps(v)}{comma}')
        elif k == "_rules":
            lines.append(f'  "{k}": ' + json.dumps(v, indent=2).replace(chr(10), chr(10) + "  ") + comma)
        elif k == "members":
            lines.append(f'  "{k}": {{')
            owners = list(v.items())
            for oi, (o, blk) in enumerate(owners):
                lines.append(f'    {json.dumps(o)}: {{"header": {json.dumps(blk["header"])}, "owner_type_new": {json.dumps(blk["owner_type_new"])}, "members": {{')
                mem = list(blk["members"].items())
                for mi, (mn, me) in enumerate(mem):
                    lines.append(f'      {json.dumps(mn)}: {json.dumps(me)}' + ("," if mi < len(mem) - 1 else ""))
                lines.append("    }}" + ("," if oi < len(owners) - 1 else ""))
            lines.append("  }" + comma)
        else:
            lines.append(f'  "{k}": {{')
            items = list(v.items())
            for ii, (n, e) in enumerate(items):
                lines.append(f'    {json.dumps(n)}: {json.dumps(e)}' + ("," if ii < len(items) - 1 else ""))
            lines.append("  }" + comma)
    lines.append("}")
    return chr(10).join(lines) + chr(10)

def build(inv, alltok):
    """The fresh map for the tree and the collision problems found."""
    # ---------------------------------------------------------------- owners
    fowner = {}
    for u, s, m in verify.UNITS:
        d = json.load(open(m))
        for f in d.get("functions", {}):
            fowner[f] = u
    asm_units = [a[0] if isinstance(a, (list, tuple)) else a for a in verify.ASM_UNITS]
    gowner = {}
    for u in asm_units:
        d = json.load(open(ROOT / "config" / f"{u}.json"))
        for f in d.get("functions", {}):
            if f.startswith("g_") or f.startswith("s_"):
                gowner.setdefault(f, u)
            else:
                fowner.setdefault(f, u)
    for u, s, m in verify.UNITS:
        d = json.load(open(m))
        for g in d.get("static_data", {}):
            gowner.setdefault(g, u)
    data = json.load(open(ROOT / "config/data.json"))
    for u, ents in data.items():
        if isinstance(ents, dict):
            for g in ents:
                if re.fullmatch(r"[A-Za-z_]\w*", g):
                    gowner.setdefault(g, u)
    for g, e in inv["globals"].items():
        if "def" in e:
            gowner.setdefault(g, Path(e["def"]).stem)

    # every referenced symbol in every symbol map
    all_symbols = set()
    for p in (ROOT / "config").glob("*.json"):
        if p.name.startswith("lib"):
            continue
        try:
            d = json.load(open(p))
        except Exception:
            continue
        if isinstance(d, dict) and "symbols" in d and isinstance(d["symbols"], dict):
            all_symbols |= set(d["symbols"])

    HEADER_UNIT = {"PARTICLE.H": "particle"}
    LIBAPI_KEEP = "Runtime Library LIBAPI export (include/LIBAPI.H collects the library prototypes)"

    def func_owner(name, e):
        if name in fowner:
            return fowner[name]
        if e and e.get("defined"):
            d = Path(e["defined"])
            return HEADER_UNIT.get(d.name, d.stem) if d.suffix == ".H" else d.stem
        decl = (e or {}).get("decl", [])
        for d in decl:
            if Path(d).name in HEADER_UNIT:
                return HEADER_UNIT[Path(d).name]
        srcs = sorted({Path(d).stem for d in decl if d.startswith("src/")})
        if len(srcs) == 1:
            return srcs[0]  # an unmatched function its module declares locally
        return None

    out = {"functions": {}, "globals": {}, "types": {}, "tags": {}, "members": {}, "enums_constants": {},
           "macros_functionlike": {}}
    functions = dict(inv["functions"])
    for f in fowner:
        functions.setdefault(f, None)
    for s in all_symbols:
        if not (s.startswith("g_") or s.startswith("s_")) and s not in functions and s not in inv["globals"]:
            functions.setdefault(s, None)

    for name, e in sorted(functions.items()):
        unit = func_owner(name, e)
        header = None
        if e and not e.get("defined"):
            header = ",".join(sorted(set(e.get("decl", []))))
        ent = {"unit": unit} if unit else {"header": header}
        kr = keep_reason(name)
        if name == "main":
            kr = "C entry point"
        if name == "__main":
            kr = "GCC/startup symbol"
        if e and any(d.endswith("LIBAPI.H") for d in e.get("decl", [])):
            kr = LIBAPI_KEEP
        if name == "NormalClipS":
            kr = kr or "library symbol spelling (GTE)"
        if name in ("SetBlockFill", "DecDCToutCallback"):
            kr = kr or "SDK/library spelling"
        if kr is None and unit is None:
            kr = "no owner unit found (declared only); left for manual review"
        if kr:
            ent.update(new=name, keep=kr)
        else:
            new, reason = func_new(name, unit)
            if name in OV["functions"]:
                new, reason = OV["functions"][name]
            ent["new"] = new
            if reason:
                ent["reworded"] = True
                ent["reason"] = reason
            if unit in NO_PREFIX_UNITS:
                ent["note"] = "unit identity open: no prefix yet"
        out["functions"][name] = ent

    globs = set(inv["globals"]) | set(gowner) | {s for s in all_symbols if s.startswith("g_") or s.startswith("s_")}
    for name in sorted(globs):
        if name in out["functions"]:
            continue
        e = inv["globals"].get(name, {})
        unit = gowner.get(name)
        ent = {"unit": unit} if unit else {"header": e.get("file")}
        if e.get("static"):
            ent["static"] = True
        kr = keep_reason(name)
        if name == "_stacksize":
            kr = "linker/startup symbol (2MBYTE.OBJ)"
        if kr:
            ent.update(new=name, keep=kr)
        else:
            new = tuple(OV["globals"].get(name, (global_new(name), None)))
            ent["new"] = new[0]
            if new[1]:
                ent["reworded"] = True
                ent["reason"] = new[1]
            if not (name.startswith("g_") or name.startswith("s_")):
                ent["note"] = "no g_/s_ prefix in the current name"
        out["globals"][name] = ent

    for name, f in sorted(inv["types"].items()):
        kr = keep_reason(name)
        if name in INT_TYPEDEFS:
            kr = "conventional fixed-width integer typedef"
        ent = {"header": f}
        if kr:
            ent.update(new=name, keep=kr)
        else:
            n = OV["types"].get(name, type_new(name))
            ent["new"] = n
        out["types"][name] = ent
    for name, e in sorted(inv["tags"].items()):
        kr = keep_reason(name)
        ent = {"header": e["file"], "keyword": e["kind"]}
        if kr:
            ent.update(new=name, keep=kr)
        else:
            ent["new"] = type_new(name, "_t" if e["kind"] == "enum" else "_s")
        out["tags"][name] = ent

    # members
    type_rename = {k: v["new"] for k, v in out["types"].items()}
    explicit_old = set(out["functions"]) | set(out["globals"]) | set(out["types"]) | set(out["tags"])
    for owner, members in sorted(inv["members"].items()):
        o = {}
        for m, f in sorted(members.items()):
            kr = keep_reason(m) if m in LINEAGE else None
            if rename.is_placeholder(m):
                kr = "placeholder member name (R7 open work); semantic name only from evidence"
            new = lower_camel(m)
            ent = {"new": new}
            if kr:
                ent = {"new": m, "keep": kr}
            elif new == m:
                ent["keep"] = "already lowerCamel"
            elif m in sdk_decl:
                ent["ambiguous"] = "also an SDK struct member name; rename only accesses whose base is " + owner
            elif m in explicit_old and out.get("functions", {}).get(m, {}).get("new", new) != new:
                ent["ambiguous"] = "same spelling as a function/global with a different new name"
            o[m] = ent
        out["members"][owner] = {"header": next(iter(members.values())), "owner_type_new": type_rename.get(owner, owner),
                                 "members": o}

    for k in ("enum_values", "defines"):
        for name, v in sorted(inv[k].items()):
            f = v if isinstance(v, str) else v["file"]
            ent = {"owner": f, "kind": "enum value" if k == "enum_values" else "constant", "new": name,
                   "keep": "already UPPER_SNAKE with its domain prefix"}
            if k == "enum_values":
                ent["enum"] = v["enum"]
            if name in LINEAGE:
                ent["keep"] = keep_reason(name)
            out["enums_constants"][name] = ent
    for name, f in sorted(inv["fmacros"].items()):
        ent = {"owner": f, "new": name}
        kr = keep_reason(name)
        ent["keep"] = kr or "upper-case statement/expression macro (does not stand in for one function)"
        out["macros_functionlike"][name] = ent

    out["_rules"] = {
        "locals_parameters": "not mapped individually: rename.py applies snake_case -> lowerCamel (words after the first capitalised, acronyms upper-case as in the map) to every remaining identifier with an inner underscore that is not an explicit map entry, an SDK/library name, a lineage name or an address placeholder; names without an inner underscore (i, ent, dir, self, x) are kept",
        "acronyms": sorted(ACRONYMS),
        "acronym_forms": ACRONYM_FORM,
        "members": "renamed per owning struct; a snake_case member whose new name is the plain lowerCamel conversion is renamed by the same rule as locals (one spelling everywhere); entries marked ambiguous are left for manual handling",
        "tags": "struct/union tags become lowerCamel_s, enum tags lowerCamel_t; the tool renames a tag only where it follows struct/union/enum",
    }
    # ---------------------------------------------------------------- collisions
    problems = []
    newmap = {}
    for kind in ("functions", "globals", "types"):
        for o, e in out[kind].items():
            n = e["new"]
            if n in newmap and newmap[n] != o:
                problems.append(("two olds -> one new", n, newmap[n], o))
            newmap[n] = o
    for o, e in out["tags"].items():
        pass
    for n, o in newmap.items():
        if n == o:
            continue
        if n in sdk_all:
            problems.append(("new == SDK name", n, o))
        if n in lib_syms:
            problems.append(("new == library symbol", n, o))
        if n in C_KEYWORDS:
            problems.append(("keyword", n, o))
        if n in alltok and n not in out["functions"] and n not in out["globals"] and n not in out["types"]:
            problems.append(("new == existing identifier", n, o, alltok[n][:3]))
        if n in explicit_old and out.get("functions", {}).get(n, out["globals"].get(n, out["types"].get(n, {}))).get("new") != n:
            problems.append(("new == another old name", n, o))
    return out, problems


DOC = ("Quake III style rename map (AGENTS.md, Project profile), regenerated by tools/rename_map.py and applied by "
       "tools/rename.py. Each entry: old name -> {new, unit/header owner}; 'keep' gives the reason a name stays; "
       "'reworded' marks a change beyond case and prefix, with its reason; 'ambiguous' members are left for manual "
       "handling. Locals and parameters follow _rules.locals_parameters.")
KINDS = ("functions", "globals", "types", "tags", "enums_constants", "macros_functionlike")


def merge(fresh, approved):
    """Carry approved entries over verbatim; return (map, added, dropped) with added/dropped as (kind, name)."""
    added, dropped = [], []
    out = {"_doc": DOC, "_rules": fresh["_rules"]}
    for kind in KINDS:
        old = approved.get(kind, {})
        out[kind] = {}
        for name, e in fresh[kind].items():
            if name in old:
                ent = dict(old[name])
                ent.pop("asm_prefix_proposed", None)
                if "enum" in e:
                    ent["enum"] = e["enum"]  # anonymous-enum labels carry a line number
                out[kind][name] = ent
            else:
                out[kind][name] = e
                added.append((kind, name, e.get("new")))
        dropped += [(kind, name, None) for name in old if name not in fresh[kind]]
    out["members"] = {}
    old_members = approved.get("members", {})
    for owner, blk in fresh["members"].items():
        prev = old_members.get(owner, {}).get("members", {})
        members = {}
        for m, e in blk["members"].items():
            if m in prev:
                members[m] = prev[m]
            else:
                members[m] = e
                added.append(("members", f"{owner}.{m}", e.get("new")))
        dropped += [("members", f"{owner}.{m}", None) for m in prev if m not in blk["members"]]
        out["members"][owner] = dict(blk, members=members)
    dropped += [("members", owner, None) for owner in old_members if owner not in fresh["members"]]
    return out, added, dropped


def main(argv=None):
    global OV
    ap = argparse.ArgumentParser(description=__doc__.split(chr(10))[0])
    ap.add_argument("--check", action="store_true", help="print what a regeneration would add or drop; write nothing")
    ap.add_argument("--fresh", action="store_true", help="ignore the approved map (rebuild every entry)")
    args = ap.parse_args(argv)
    OV = json.loads(OVERRIDES.read_text(encoding="utf-8"))
    inv, alltok = inventory()
    fresh, problems = build(inv, alltok)
    approved = {} if args.fresh or not MAP.exists() else json.loads(MAP.read_text(encoding="utf-8"))
    out, added, dropped = merge(fresh, approved)
    for kind, name, new in added:
        print(f"added   {kind:20s} {name} -> {new}")
    for kind, name, _ in dropped:
        print(f"dropped {kind:20s} {name}")
    for p in problems:
        print("problem", p)
    print(f"added {len(added)}, dropped {len(dropped)}, problems {len(problems)}")
    if problems:
        return 1
    if not args.check:
        text = dump_compact(out)
        json.loads(text)
        MAP.write_text(text, encoding="utf-8", newline=chr(10))
    return 0


OV = {"functions": {}, "globals": {}, "types": {}}

if __name__ == "__main__":
    sys.exit(main())
