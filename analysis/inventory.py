"""Reproducible Alien Trilogy SLUS_000.07 executable and PsyQ library inventory.

Run from the workspace root with: python alien_decomp/analysis/inventory.py
Candidate boundaries are heuristic unless supported by a directly referenced
start or the executable entry point; library candidates mask relocations.
"""
from __future__ import annotations
import hashlib, importlib.util, json, os, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "games" / "Alien Trilogy (PS1 USA)" / "extracted" / "SLUS_000.07"
RUNTIME = ROOT / "sdk" / "extracted" / "Programmer Tool - Runtime Library Version 3.3 (Japan)_DTL-S2190_redump"
EXPECTED_USA_BASE = 0x80010000
OUT = Path(__file__).resolve().parent

spec = importlib.util.spec_from_file_location("psyqobj", ROOT / "shared-tools" / "psyqobj.py")
psyqobj = importlib.util.module_from_spec(spec); spec.loader.exec_module(psyqobj)

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def u32s(data): return list(struct.unpack("<%dI" % (len(data)//4), data[:len(data)//4*4]))

raw = EXE.read_bytes()
if raw[:8] != b"PS-X EXE": raise SystemExit("input is not a PS-X EXE")
entry, header_gp, text_addr, text_size = struct.unpack_from("<IIII", raw, 0x10)
BASE = text_addr
if BASE != EXPECTED_USA_BASE: raise SystemExit(f"unexpected USA text address: 0x{BASE:08X}")
text = raw[0x800:0x800+text_size]
if len(text) != text_size: raise SystemExit("truncated PS-X EXE text payload")
words = u32s(text)

# Collect possible direct JAL destinations and common compiler prologue
# fingerprints. JAL words can occur in embedded data, so these remain heuristic.
jal_refs = {}
for i, w in enumerate(words):
    if w >> 26 == 3:
        dest = ((BASE + i*4 + 4) & 0xF0000000) | ((w & 0x03ffffff) << 2)
        if BASE <= dest < BASE + len(text): jal_refs.setdefault(dest, []).append(BASE+i*4)
starts = {BASE}
sources = {BASE: {"image_start_sentinel"}}
if BASE <= entry < BASE+len(text): starts.add(entry); sources.setdefault(entry,set()).add("exe_entry")
for target, callers in jal_refs.items():
    starts.add(target); sources.setdefault(target,set()).add("direct_jal_target")
    # call references retained separately in report
# MIPS prologue signature: addiu sp,sp,-frame followed by a saved register.
for i in range(len(words)-1):
    w = words[i]
    if (w & 0xffff0000) == 0x27bd0000 and (w & 0x8000):
        nxt = words[i+1]
        if nxt >> 26 == 0x2b and ((nxt >> 21) & 31) == 29:
            a=BASE+i*4; starts.add(a); sources.setdefault(a,set()).add("prologue_pattern")
# The existing AT.C carries eight functions already checked against the game
# by the supplied compiler experiment, so their symbols are trusted boundaries.
AT_C=ROOT / "compiler-identification" / "alien-trilogy" / "AT.C"
import re
known={0x80000000+int(x,16):"f_"+x for x in re.findall(r"\bf_([0-9a-fA-F]{5,8})\s*\([^;{}]*\)\s*\{",AT_C.read_text(encoding="latin1"))}
for a in known:
    if BASE <= a < BASE+len(text):
        starts.add(a); sources.setdefault(a,set()).add("compiler_verified_AT_C_symbol")
# Entries that load a register before their prologue. The prologue scan finds the
# interior prologue 8 bytes in and the JAL/reference scan may miss the entry, so the
# entry is added and the interior prologue dropped. Each is evidenced outside this scan.
PRE_PROLOGUE_ENTRIES = [
    (0x8002AB48, 0x8002AB50, "task callback: src/main.c creates TaskCreate(60, f_2ab48); it loads "
     "g_visible_actor_count (lui+lh) before its prologue; DuckStation write watches hit its store at "
     "0x8002adcc (analysis/runtime/globals.json)"),
    (0x8004070C, 0x80040714, "DuckStation execute breakpoint at 0x8004070c hit from pause_menu_init+0xd8 "
     "(analysis/runtime/globals.json); it loads g_unpack_buffer into v1 (lui+lw) before its prologue"),
]
entry_corrections=[]
for entry_address, interior, why in PRE_PROLOGUE_ENTRIES:
    if words[(entry_address-BASE)//4] >> 16 & 0xffe0 != 0x3c00 or (words[(interior-BASE)//4] & 0xffff8000) != 0x27bd8000:
        raise SystemExit(f"entry correction 0x{entry_address:08X} no longer fits the image")
    starts.add(entry_address); sources.setdefault(entry_address,set()).add("pre_prologue_entry")
    starts.discard(interior); sources.pop(interior, None)
    entry_corrections.append({"entry":f"0x{entry_address:08X}","interior_prologue_dropped":f"0x{interior:08X}","evidence":why})
sorted_starts=sorted(starts)
inventory=[]
for i,a in enumerate(sorted_starts):
    b=sorted_starts[i+1] if i+1<len(sorted_starts) else BASE+len(text)
    inventory.append({"address":f"0x{a:08X}","file_offset":f"0x{0x800+a-BASE:06X}","candidate_span_to_next_start":b-a,
      "end_address_exclusive_if_next_start_is_real":f"0x{b:08X}",
      "verified_size_bytes":72 if known.get(a)=="f_15564" else None,
      "verified_end_address_exclusive":"0x800155AC" if known.get(a)=="f_15564" else None,
      "symbol":known.get(a),"boundary_confidence":"compiler_verified_function_symbol" if a in known else ("verified_entry" if a==entry or "pre_prologue_entry" in sources[a] else ("heuristic_candidate" if a==BASE else ("strong_candidate" if "direct_jal_target" in sources[a] and "prologue_pattern" in sources[a] else "heuristic_candidate"))),
      "start_evidence":sorted(sources[a]),"direct_callers":[f"0x{x:08X}" for x in jal_refs.get(a,[])],"ret_in_slice":any(w==0x03e00008 for w in words[(a-BASE)//4:(b-BASE)//4])})

# Match Runtime Library 3.3 .text modules against executable. Full masked match
# means all non-relocation bytes agree; it is deliberately not called byte exact.
def masked_text(m):
    for sec,name in m.sections.items():
        if name==".text" and sec in m.code:
            code=bytes(m.code[sec]); mask=bytearray(b"\1"*len(code))
            for _,off,_ in m.patches.get(sec,[]):
                for k in range(off & ~3,min(len(code),(off&~3)+4)): mask[k]=0
            return code,bytes(mask)
    return None
def find_matches(code,mask,hay):
    runs=[]; i=0
    while i<len(mask):
        if mask[i]:
            j=i
            while j<len(mask) and mask[j]: j+=1
            if j-i>=8:runs.append((i,j))
            i=j
        else:i+=1
    if not runs:return []
    a,b=max(runs,key=lambda ab:ab[1]-ab[0]); hits=[]; p=hay.find(code[a:b])
    while p>=0:
        st=p-a
        if st>=0 and st%4==0 and st+len(code)<=len(hay) and all(not mask[k] or code[k]==hay[st+k] for k in range(len(code))): hits.append(st)
        p=hay.find(code[a:b],p+1)
    return hits
modules=[]; seen_libs={}
for lib in sorted(RUNTIME.rglob("*.LIB")):
    digest=sha(lib)
    if digest in seen_libs: continue
    seen_libs[digest]=str(lib.relative_to(ROOT))
    try: mods=psyqobj.load(str(lib))
    except Exception as exc: modules.append({"library":str(lib.relative_to(ROOT)),"error":str(exc)}); continue
    for m in mods:
        if isinstance(m,Exception): continue
        tp=masked_text(m)
        if not tp:continue
        code,mask=tp
        # Tiny stubs and relocation-heavy templates match too many unrelated
        # places. Keep only useful-sized code with substantial literal evidence.
        if len(code) < 48 or sum(mask) < 32 or sum(mask) / max(len(mask),1) < 0.50:
            continue
        for off in find_matches(code,mask,text):
            modules.append({"library":str(lib.relative_to(ROOT)),"library_sha256":digest,"module":m.name,"text_size":len(code),
              "masked_relocation_bytes":len(mask)-sum(mask),"exe_address":f"0x{BASE+off:08X}",
              "match_type":"relocation_masked_full_text_match","byte_exact":False,
              "note":"All unmasked .text bytes match; relocation words were ignored. Verify final relocation values before claiming byte identity."})

targets=[0x13a1c,0x143f8,0x15324,0x15564,0x157d0,0x15814,0x15cc8,0x15d28]
focus=[]
for off in targets:
    # These recovered labels are virtual PS-X addresses (f_13a1c = 0x80013a1c).
    a=0x80000000+off
    nearest=min(inventory,key=lambda x:abs(int(x["address"],16)-a))
    focus.append({"suggested_function":"f_%05x"%off,"address":f"0x{a:08X}","in_executable_range":off<len(text),
      "is_inventory_start":a in starts,"boundary_confidence":"compiler_verified_function_symbol" if a in known else next(x["boundary_confidence"] for x in inventory if int(x["address"],16)==a) if a in starts else None,"nearby_inventory_start":nearest["address"],"distance_from_nearest":abs(int(nearest["address"],16)-a),
      "direct_callers":[f"0x{x:08X}" for x in jal_refs.get(a,[])],
      "candidate_span_to_next_start":next((x["candidate_span_to_next_start"] for x in inventory if int(x["address"],16)==a),None),
      "verified_size_bytes":72 if known.get(a)=="f_15564" else None})

result={"inputs":{"executable":str(EXE.relative_to(ROOT)),"executable_sha256":sha(EXE),"file_size":len(raw),"header":"PS-X EXE","entry_address":f"0x{entry:08X}","header_gp":f"0x{header_gp:08X}","startup_gp_observed":"0x8008CD74","text_address":f"0x{BASE:08X}","text_size":text_size,"runtime":"Programmer Tool Runtime Library 3.3 DTL-S2190","runtime_root":str(RUNTIME.relative_to(ROOT))},
 "method":{"address_mapping":"file offset = 0x800 + (address - text_address)","starts":"Executable entry, direct JAL destinations, prologue fingerprints, and eight AT.C labels. Direct JAL-looking words can occur in data and remain heuristic. Entries that load a register before their prologue are corrected from outside evidence (entry_corrections).","size":"Candidate span ends at the next candidate start and is not asserted as a function size. The supplied verification confirms f_15564 size 72 bytes (exclusive end 0x800155AC).","library_match":"PsyQ .text matching masks every relocation word; a full masked hit is not byte exact."},
 "candidate_start_count":len(inventory),"entry_corrections":entry_corrections,"candidate_starts":inventory,"runtime33_matches":modules,"first_eight_AT_C_targets":focus}
(OUT/"inventory.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
lines=["# Alien Trilogy SLUS_000.07 inventory","",f"Executable SHA-256: `{result['inputs']['executable_sha256']}`; {len(text):,} bytes at `0x{BASE:08X}`; entry `0x{entry:08X}`; header GP `0x{header_gp:08X}`. Startup code initializes GP to `0x8008CD74`.","",f"Candidate starts: {len(inventory)}; these include heuristic JAL/prologue discoveries and verified `AT.C` symbols. Candidate spans are not certified function sizes. `f_15564` is verified as 72 bytes through exclusive end `0x800155AC`. Runtime Library 3.3 full relocation-masked text matches: {len(modules)}.","","Direct JAL-looking words may occur in data; their targets remain heuristic unless independently supported. Header GP is zero; the nonzero GP value comes from startup code.","","## First eight recovered source targets","","| Function | Address | Start evidence | Candidate span | Verified size |","|---|---:|---|---:|---:|"]
for x in focus:
    span=x['candidate_span_to_next_start']
    lines.append(f"| `{x['suggested_function']}` | `{x['address']}` | {x['boundary_confidence'] or 'not in candidate scan'} | {span if span is not None else '—'} | {x['verified_size_bytes'] if x['verified_size_bytes'] is not None else '—'} |")
lines += ["","## Runtime Library 3.3 matches","", "All entries below require relocation-value confirmation; these are not labeled byte exact.",""]
if modules:
 for x in modules: lines.append(f"- `{x.get('module','?')}` from `{Path(x['library']).name}` at `{x.get('exe_address','?')}` ({x.get('text_size','?')} text bytes; {x.get('masked_relocation_bytes','?')} masked bytes).")
else: lines.append("No full relocation-masked module matches found in the extracted DTL-S2190 libraries.")
lines += ["","## Reproduction","","From the workspace root, run `python alien_decomp/analysis/inventory.py`. The script reads the existing `shared-tools/psyqobj.py` parser but writes only this analysis directory.",""]
(OUT/"inventory.md").write_text("\n".join(lines),encoding="utf-8")
print(f"wrote {OUT/'inventory.json'} and inventory.md; starts={len(inventory)}, runtime matches={len(modules)}")
