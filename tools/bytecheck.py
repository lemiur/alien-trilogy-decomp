"""Compare one linked function's bytes with a PsyQ LNK object function.

Section-relative relocations
----------------------------
A relocation whose target is ``<data section>+<offset>`` (``.rdata+0x48``, a jump
table or string literal; ``.sdata+0x10``, a small literal read through $gp) is
resolved through the placement of the item that holds the offset, never through
a whole-section slice, so a unit's ``.rdata``/``.data``/``.sdata`` may have holes
where other functions' literals sit in retail. Items and their extents come from
the unit's assembly listing (``--asm``, by default ``INPUT.S`` beside the object)
through tools/data_extent.py, the sizer the gate and data_check.py share; an
offset inside an item (``$LC0+4``) is an interior reference into that item. An
item is placed, in this order:

1. by a ``config/data.json`` definition of the unit (``--unit``, ``--data-map``)
   that covers it: the definition is exactly one item and gives its address;
2. by evidence: every relocation into the item from the unit's mapped functions
   (HI16+LO16 halves, GP-relative and word relocations) and every word relocation
   of the unit's data definitions is an anchor. Its retail field gives a target,
   and the target minus the anchor's offset into the item is an implied address.
   A HI16 and a LO16 half combine only when, for that exact target expression, the
   unit holds one HI16 value and one LO16 value. The item is placed when every
   anchor implies the same address, the item is unnamed (an exported datum without
   a definition is not placed by evidence), the implied range lies in the retail
   section of the same name (``config/section_layout.json``), overlaps none of the
   unit's functions, definitions or other placed items, and the item's object bytes,
   with their own word relocations resolved, equal the reference bytes there.

A definition that the anchors contradict, anchors that imply several addresses or
an unpaired HI16/LO16 half ("ambiguous"), and an evidenced item that fails any of
the checks above ("misplaced") are errors: the referencing function does not
verify, because its referent is unproven. Only an offset that no item placement
covers (no definition and no anchor, or ``.align`` fill) falls back to the symbol
map's ``sections`` base for that section; a unit with holes omits that base, or
keeps it only for what it still correctly places. This is the gate's rule
(tools/gate.py, ``generated_placements`` and ``Linker.section_address``), so the
two agree on every placed byte, and the gate's cross-check against verify.py
enforces that they do.

Split HI16/LO16 pairs
---------------------
A ``lui``/``addiu`` pair that retail separates (asm/lib/hilo.h, ``HI16(E)``/``LO16(E)``) is
emitted by ASPSX as PsyQ patch types 26 and 30 with explicit shift expressions; ``split_half``
recognises exactly those shapes and checks each as the HI16 or LO16 half of E (target resolved
and anchored like a type-82/84 half). Every split half must have its other half, for the same
E, inside the same function.

Independence from the gate
--------------------------
verify.py is the gate's second witness, so this module does not import the gate.
It shares tools/lnk_symbols.py, the reader of the object's local-symbol records and of
the map's linkage flag, and tools/data_extent.py, the item sizer and the parser of data.json
keys, which data_check.py already shares and whose defects
tools/generated_witness.py (a sizer of its own over the object alone) would expose.
The anchor rule is implemented here a second time, from the object, the map, the
listing and the reference: it reads the retail operand fields itself, evaluates the
$gp base of a GP-relative anchor through this module's own expression evaluator
(``sectstart(.sdata)`` from the unit map) instead of the layout's gp, checks item
content against the reference (the gate leaves that to its final hash), and places
lazily, one referenced item at a time. A mistake in either implementation therefore
shows up as a disagreement between the gate's link and these per-function results
(the gate fails on it), not as a shared blind spot. data_check.py resolves its
definitions' section-relative words through the same ``ItemPlacements``, so
verify.py as a whole applies one rule.
"""
from __future__ import annotations
import argparse, hashlib, importlib.util, json, struct
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parent
spec = importlib.util.spec_from_file_location("psyqobj", ROOT / "shared-tools" / "psyqobj.py")
psyqobj = importlib.util.module_from_spec(spec)
spec.loader.exec_module(psyqobj)
spec = importlib.util.spec_from_file_location("data_extent", Path(__file__).resolve().parent / "data_extent.py")
data_extent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(data_extent)
spec = importlib.util.spec_from_file_location("lnk_symbols", Path(__file__).resolve().parent / "lnk_symbols.py")
lnk_symbols = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lnk_symbols)

LAYOUT = PROJECT / "config" / "section_layout.json"
HEADER = 0x800

class CheckError(Exception): pass

def u32(x): return x & 0xffffffff

def num(v): return int(v,0) if isinstance(v,str) else int(v)

def text_symbols(o):
    """[(name, section id, offset, static)] of every .text definition: exported (XDEF) or static (local symbol)."""
    return [(nm,sid,off,static) for static,pool in ((False,o.xdefs),(True,getattr(o,'locals',()))) for nm,sid,off in pool
            if o.sections.get(sid)=='.text']

def text_functions(o,fs):
    """[(name, section id, offset, address, size)] of the mapped functions defined in the object's .text.

    A function is found only under the linkage its map entry declares: an exported function by
    its XDEF, a static one (``"static": true``) by its local symbol. Static names are scoped to
    this object, so a static of the same name in another unit never matches here."""
    out=[]
    for nm,sid,off,static in text_symbols(o):
        f=fs.get(nm)
        if not isinstance(f,dict): continue
        try: declared=lnk_symbols.is_static(f)
        except lnk_symbols.SymbolError as e: raise CheckError(f'{nm}: {e}')
        if declared==static: out.append((nm,sid,off,num(f['address']),int(f['size'])))
    return out

def sext16(v): return v-0x10000 if v&0x8000 else v

def section_target(expr):
    """(section id, constant) for a target expression ``[section]`` or ``[section]+c``, else None."""
    if expr[0]==4: return expr[1],0
    if expr[0]==44:
        a,b=expr[1],expr[2]
        if a[0]==4 and b[0]=='c': return a[1],b[1]
        if b[0]==4 and a[0]=='c': return b[1],a[1]
    return None

def linear(e):
    """(base, constant) for ``base + constant`` sums (base: one symbol or section, or None), else None.

    ASPSX folds the constants of such a sum (``.data+656+0x8000`` is ``[.data] + 33424``)."""
    if e[0]=='c': return None,e[1]
    if e[0] in (2,4): return e,0
    if e[0]==44:
        a,b=linear(e[1]),linear(e[2])
        if a is None or b is None or (a[0] is not None and b[0] is not None): return None
        return a[0] if a[0] is not None else b[0],u32(a[1]+b[1])
    return None

def split_half(typ,expr):
    """(82, E) or (84, E) for one half of a split HI16/LO16 pair, else None.

    ASPSX emits the HI16/LO16 patch types (82/84) only from its own back-to-back macro
    expansions (``la``, ``lw reg, sym``). Retail Sony code also separates the halves (the
    ``lui`` before a ``jal``, the ``addiu`` in its delay slot), which ASPSX writes as the
    explicit 16-bit field expressions of asm/lib/hilo.h:

    - ``lui r, HI16(E)``: type 26 (the ``lui`` immediate field) with ``(E + 0x8000) >> 16``;
    - ``addiu``/load/store ``LO16(E)``: type 30 (a signed 16-bit field) with
      ``E - (((E + 0x8000) >> 16) << 16)``.

    PSYLINK evaluates these expressions and range-checks the field, which yields exactly the
    bytes of an 82/84 pair for E; so each is checked as that half of E. Only these exact
    expression shapes are accepted: any other type-26 expression, and any type-30 expression
    that is neither this nor GP-relative, is refused by the caller. E is one symbol or section
    plus a constant (``linear``); the result's E is written ``base`` or ``(44, c, base)``."""
    def rounded(x):  # (E + 0x8000) >> 16, as (base, constant of E)
        if x[0]!=60 or x[1]!=('c',16): return None
        s=linear(x[2])
        return None if s is None or s[0] is None else (s[0],u32(s[1]-0x8000))
    def written(b,c): return b if c==0 else (44,('c',c),b)
    if typ==26:
        e=rounded(expr)
        if e: return 82,written(*e)
    if typ==30 and expr[0]==46 and expr[1][0]==58 and expr[1][1]==('c',16):
        e,low=rounded(expr[1][2]),linear(expr[2])
        if e and low==e: return 84,written(*e)
    return None

def split_pairs(patches,start,end):
    """Errors for split HI16/LO16 halves in [start, end) whose other half is missing there."""
    halves={82:set(),84:set()}
    for typ,at,expr in patches:
        h=split_half(typ,expr) if start<=at<end else None
        if h: halves[h[0]].add(repr(h[1]))
    return [f'split {"LO16" if k==84 else "HI16"} of {e} has no {"HI16" if k==84 else "LO16"} half in the function'
            for k,o in ((82,84),(84,82)) for e in sorted(halves[k]-halves[o])]

def layout_section(layout, address):
    """Retail section holding ``address`` within the layout's certain bounds, or None."""
    for region in layout.get("regions",()):
        lo,hi=region["start"],region["end"]
        lo=num(lo["max"] if isinstance(lo,dict) else lo); hi=num(hi["min"] if isinstance(hi,dict) else hi)
        if lo<=address<hi: return region["section"]
    return None

class Reference:
    """The retail PS-X EXE."""
    def __init__(self,raw):
        if len(raw)<HEADER or raw[:8]!=b'PS-X EXE': raise CheckError('reference is not a PS-X EXE with a 0x800-byte header')
        self.raw=raw; self.load,self.size=struct.unpack_from('<II',raw,0x18)

    def read(self,address,size):
        if address<self.load or address+size>self.load+self.size: raise CheckError(f'0x{address:08x}+{size:#x} lies outside the executable image')
        return self.raw[HEADER+address-self.load:HEADER+address-self.load+size]

    def word(self,address): return struct.unpack('<I',self.read(address,4))[0]

class ItemPlacements:
    """Retail addresses of a unit object's initialized data items (rule: module docstring)."""
    def __init__(self,o,m,reference,definitions=None,listing=None,layout=None):
        self.o,self.m,self.ref=o,m,reference
        self.definitions=definitions or {}; self.listing=listing; self.layout=layout
        self.resolver=None; self.unpaired=[]; self._items=None; self._defs=None; self._placed={}; self._pending=set()

    def items(self):
        if self._items is None:
            if self.listing is None: raise CheckError('a section-relative data relocation needs the assembly listing (--asm)')
            try:
                items=data_extent.asm_data_items(self.listing)
                data_extent.check_sizes(items,{nm:len(self.o.code.get(sid,b'')) for sid,nm in self.o.sections.items() if nm in data_extent.DATA_SECTIONS})
            except data_extent.ExtentError as e: raise CheckError(f'cannot size data items: {e}')
            self._items=items
        return self._items

    def static_labels(self):
        """{name: (section, offset)} of the listing's unexported data labels (none without a listing)."""
        if self.listing is None: return {}
        try: return data_extent.static_labels(self.listing)
        except data_extent.ExtentError as e: raise CheckError(f'cannot read static labels: {e}')

    def defs(self):
        """[(key, section id, object offset, address, size)] of the unit's data definitions."""
        if self._defs is None:
            # Exported data by XDEF; declared initialised statics by their listing label.
            exported=lnk_symbols.named_data(self.o,self.m,self.static_labels())
            out=[]
            for key,d in self.definitions.items():
                try:
                    secname,start=data_extent.definition_position(key,d,exported)
                    if secname not in data_extent.DATA_SECTIONS: raise CheckError(f'{key} is not initialized data')
                    data_extent.definition_extent(self.items(),key,secname,start,int(d['size']))
                except data_extent.ExtentError as e: raise CheckError(str(e))
                sid=next((s for s,nm in self.o.sections.items() if nm==secname),None)
                if sid is None: raise CheckError(f'{key} has no section in the object')
                out.append((key,sid,start,num(d['address']),int(d['size'])))
            self._defs=out
        return self._defs

    def anchors(self,sid,lo,hi):
        """Item addresses implied by the unit's relocations into [lo, hi) of section sid (None: an unpaired half)."""
        o,implied,halves=self.o,set(),{}
        def owners():
            for _,sec,start,address,size in text_functions(o,self.m.get('functions',{})): yield sec,start,address,size,True
            for _,sec,start,address,size in self.defs(): yield sec,start,address,size,False
        for sec,start,address,size,code in owners():
            for typ,at,expr in o.patches.get(sec,[]):
                if not start<=at<start+size: continue
                if code and split_half(typ,expr): typ,expr=split_half(typ,expr)
                gp_base=None
                if typ==30 and code and expr[0]==46 and expr[1][0]==12:
                    target=section_target(expr[2]); gp_base=expr[1]
                elif typ==16 or (code and typ in (82,84)):
                    target=section_target(expr)
                else: continue
                if target is None or target[0]!=sid or not lo<=target[1]<hi: continue
                field=self.ref.word(address+at-start)
                if typ==16: implied.add(u32(field-(target[1]-lo)))
                elif typ==30: implied.add(u32(self.resolver.resolve(gp_base)+sext16(field&0xffff)-(target[1]-lo)))
                else: halves.setdefault(target,{}).setdefault(typ,set()).add(field&0xffff)
        self.unpaired=[]
        for target,parts in halves.items():
            his,los=parts.get(82,set()),parts.get(84,set())
            if len(his)==1 and len(los)==1:
                implied.add(u32((next(iter(his))<<16)+sext16(next(iter(los)))-(target[1]-lo)))
            else:
                implied.add(None)
                self.unpaired.append(f'{self.o.sections[sid]}+{target[1]:#x}: hi {sorted(map(hex,his))} lo {sorted(map(hex,los))}')
        return implied

    def where(self,sid,item): return f"{self.o.sections[sid]}+{item['offset']:#x}"

    def place(self,sid,item):
        """Retail address of one item, or None when nothing places it."""
        key=(sid,item['offset'])
        if key in self._placed: return self._placed[key]
        if key in self._pending: raise CheckError(f'{self.where(sid,item)}: its placement depends on itself')
        self._pending.add(key)
        try: address=self._place(sid,item)
        finally: self._pending.discard(key)
        self._placed[key]=address
        return address

    def _place(self,sid,item):
        lo,size=item['offset'],item['size']; hi=lo+size; where=self.where(sid,item)
        implied=self.anchors(sid,lo,hi)
        unpaired=f" and HI16/LO16 halves of {'; '.join(self.unpaired)} that do not pair one to one" if None in implied else ''
        defined=[d for d in self.defs() if d[1]==sid and d[2]<hi and lo<d[2]+d[4]]
        if defined:
            (name,_,_,address,_),=defined  # definition_extent: a definition is exactly one item
            if implied and implied!={address}:
                raise CheckError(f'misplaced {where}: data definition {name} is at {address:#x} but relocations imply '
                                 f'{sorted(hex(v) for v in implied if v is not None)}{unpaired}')
            return address
        if not implied: return None
        if any(s==sid and off==lo for _,s,off in list(self.o.xdefs)+list(getattr(self.o,'locals',()))): return None  # named datum (exported or static) without a definition
        if None in implied or len(implied)!=1:
            raise CheckError(f'ambiguous {where}: relocations imply several addresses '
                             f'{sorted(hex(v) for v in implied if v is not None)}{unpaired}')
        address=next(iter(implied)); secname=self.o.sections[sid]
        if address<self.ref.load or address+size>self.ref.load+self.ref.size:
            raise CheckError(f'misplaced {where}: implied address {address:#x} is outside the image')
        if self.layout is not None and {layout_section(self.layout,a) for a in (address,address+size-1)}!={secname}:
            raise CheckError(f'misplaced {where}: implied address {address:#x} is not inside the retail {secname}')
        taken=[(num(f['address']),int(f['size']),n) for n,f in self.m.get('functions',{}).items()]
        taken+=[(a,s,n) for n,_,_,a,s in self.defs()]
        taken+=[(a,i['size'],self.where(s,i)) for (s,_),(a,i) in self._located().items()]
        for a,s,n in taken:
            if a<address+size and address<a+s: raise CheckError(f'misplaced {where}: implied range {address:#x}+{size:#x} overlaps {n}')
        content=bytearray(self.o.code.get(sid,b'')[lo:hi])
        for typ,at,expr in self.o.patches.get(sid,[]):
            if not lo<=at<hi: continue
            if typ!=16 or at+4>hi: raise CheckError(f'{where}: unsupported data patch type {typ} at {at:#x}')
            struct.pack_into('<I',content,at-lo,self.resolver.resolve(expr))
        if bytes(content)!=self.ref.read(address,size):
            raise CheckError(f'misplaced {where}: object bytes differ from the reference at the implied address {address:#x}')
        return address

    def _located(self):
        out={}
        for (sid,off),address in self._placed.items():
            if address is None: continue
            item=data_extent.item_at(self.items(),self.o.sections[sid],off)
            if item is not None: out[(sid,off)]=(address,item)
        return out

    def address(self,sid,offset):
        """Retail address of section sid + offset through the item that holds it, or None."""
        item=data_extent.item_containing(self.items(),self.o.sections[sid],offset)
        if item is None: return None
        base=self.place(sid,item)
        return None if base is None else base+offset-item['offset']

class Resolver:
    """Evaluates one object's PsyQ relocation expressions against a unit symbol map."""
    def __init__(self,o,m,placements=None):
        self.o=o; self.m=m; self.fs=m.get('functions',{}); self.symbols=m.get('symbols',{})
        self.placements=placements
        if placements is not None: placements.resolver=self

    def text_address(self,sid,offset):
        # Function maps give final addresses for discontiguous .text
        # placements: resolve an object-relative .text offset through the
        # anchor of the mapped function that contains it, never through a
        # section base.
        for _,sn,off,address,size in text_functions(self.o,self.fs):
            if sn==sid and off <= offset < off+size: return address+(offset-off)
        # An end label (one past the last byte of .text, as PATCHGTE's copy loop bound) is the
        # end of the mapped function that ends the section; no other offset outside a function.
        if offset==len(getattr(self.o,'code',{}).get(sid,b'')):
            for _,sn,off,address,size in text_functions(self.o,self.fs):
                if sn==sid and off+size==offset: return address+size
        raise CheckError(f'.text+0x{offset:x} is not inside a mapped function')

    def section_address(self,sid,offset):
        secname=self.o.sections[sid]
        if secname=='.text': return self.text_address(sid,offset)
        if self.placements is not None and secname in data_extent.DATA_SECTIONS:
            placed=self.placements.address(sid,offset)
            if placed is not None: return placed
        base=self.m.get('sections',{}).get(secname)
        if base is None: raise CheckError(f'no item placement and no section base for {secname}+{offset:#x}')
        return num(base)+offset

    def symbol(self,n):
        o=self.o
        if n in self.symbols: return num(self.symbols[n])
        # An external reference never names a static: its name is local to this object.
        if n in self.fs and not lnk_symbols.is_static(self.fs[n]): return num(self.fs[n]['address'])
        for nm,sn,off in o.xdefs:
            if nm==n and sn in o.sections: return self.section_address(sn,off)
        raise CheckError('unmapped relocation symbol '+n)

    def ev(self,e):
        """Return an int, or (section id, offset) for a section-relative value."""
        o,m=self.o,self.m
        if e[0]=='c': return e[1]
        if isinstance(e[0],int) and e[0] in (2,4,12):
            idx=e[1]
            if e[0]==2 and idx in o.xrefs: return self.symbol(o.xrefs[idx])
            if e[0]==4 and idx in o.sections: return (idx,0)
            if e[0]==12 and idx in o.sections:
                secname=o.sections[idx]
                start_value=m.get('section_starts',{}).get(secname)
                if start_value is None and secname=='.sdata':
                    start_value=m.get('gp')
                    section_base=m.get('sections',{}).get(secname)
                    if start_value is not None and section_base is not None and num(start_value)!=num(section_base):
                        raise CheckError('.sdata GP/start differs from allocation base; add section_starts to symbol map')
                if start_value is None: raise CheckError('unmapped section start '+secname)
                return num(start_value)
            raise CheckError(f'unsupported relocation expression {e!r}')
        op,x,y=e
        # PsyQ expressions are serialized right operand first. See
        # Expression::parse in pcsx-redux's PsyQ object parser:
        # https://github.com/grumpycoders/pcsx-redux/blob/main/tools/psyq-obj-parser/psyq-obj-parser.cc
        right,left=self.ev(x),self.ev(y)
        if op==44:
            if isinstance(left,tuple) and isinstance(right,tuple): raise CheckError('sum of two section-relative values')
            if isinstance(left,tuple): return (left[0],left[1]+right)
            if isinstance(right,tuple): return (right[0],right[1]+left)
            return u32(left+right)
        if op==46:
            if isinstance(left,tuple) and isinstance(right,tuple):
                if left[0]!=right[0]: raise CheckError('difference of values in different sections')
                return left[1]-right[1]
            # A section-relative value minus an absolute one (GP-relative: target - sectstart(.sdata))
            # is the target's own address minus it: resolve through its item first.
            if isinstance(right,tuple): right=self.section_address(*right)
            if isinstance(left,tuple): left=self.section_address(*left)
            return u32(left-right)
        raise CheckError(f'unsupported relocation expression operator {op:#x}')

    def resolve(self,e):
        """Final 32-bit value of a relocation expression."""
        v=self.ev(e)
        if isinstance(v,tuple): v=self.section_address(*v)
        return u32(v)

def load_object(path):
    """One PsyQ LNK object with its local symbols (tools/lnk_symbols.py)."""
    try: return lnk_symbols.load(path)
    except (lnk_symbols.SymbolError, AssertionError, ValueError, IndexError, struct.error) as e:
        raise CheckError(f'object is not a single valid PsyQ LNK object: {e}')

def check(o,m,reference,function,placements=None):
    """Report fields for one function of object ``o`` (raises CheckError)."""
    fs=m.get('functions',{}); fn=fs.get(function)
    if not isinstance(fn,dict) or 'address' not in fn or 'size' not in fn: raise CheckError('function missing address/size in symbol map')
    addr=num(fn['address']); size=int(fn['size'])
    if size<=0 or size%4: raise CheckError('function size must be a positive multiple of 4')
    try: static=lnk_symbols.is_static(fn)
    except lnk_symbols.SymbolError as e: raise CheckError(str(e))
    defs=[(sn,off,st) for nm,sn,off,st in text_symbols(o) if nm==function]
    if not defs: raise CheckError('function is not defined by object')
    if any(st!=static for _,_,st in defs):
        raise CheckError(f"function is {'exported' if static else 'static'} in the object but the map declares it {'static' if static else 'exported'}")
    if len(defs)!=1: raise CheckError(f'function is defined {len(defs)} times by object')
    sec,start,_=defs[0]
    code=bytes(o.code.get(sec,b'')); end=start+size
    if end>len(code): raise CheckError(f'object function range {start:#x}..{end:#x} exceeds section size {len(code):#x}')
    next_xdef=min((off for _,sn,off,_ in text_symbols(o) if sn==sec and off>start),default=len(code))
    if next_xdef!=end: raise CheckError(f'expected function size {size} does not match object function extent {next_xdef-start:#x}')
    actual=bytearray(code[start:end]); patches=[]
    resolve=Resolver(o,m,placements).resolve
    gp=m.get('gp')
    if isinstance(gp,str): gp=int(gp,0)
    unpaired=split_pairs(o.patches.get(sec,[]),start,end)
    if unpaired: raise CheckError('; '.join(unpaired))
    for typ,off,expr in o.patches.get(sec,[]):
        if not start<=off<end: continue
        rel=off-start
        emitted=typ
        if split_half(typ,expr): typ,expr=split_half(typ,expr)
        if rel+4>size: raise CheckError(f'relocation at {rel:#x} crosses function boundary')
        word=struct.unpack_from('<I',actual,rel)[0]
        applied_value=None
        if typ==82: # HI16
            target=resolve(expr); val=((target+0x8000)>>16)&0xffff; word=(word&0xffff0000)|val; applied_value=val
        elif typ==84: # LO16; extra records contain paired HI references
            target=resolve(expr); word=(word&0xffff0000)|(target&0xffff); applied_value=target&0xffff
        elif typ==74: # MIPS26
            target=resolve(expr)
            if target&3: raise CheckError(f'unaligned MIPS26 target 0x{target:08x}')
            place=addr+rel
            if ((target ^ (place+4)) & 0xf0000000): raise CheckError(f'MIPS26 target 0x{target:08x} is outside jump region for 0x{place+4:08x}')
            word=(word&0xfc000000)|((target>>2)&0x03ffffff); applied_value=target
        elif typ==30: # GP-relative 16-bit signed displacement
            if gp is None: raise CheckError('symbol map has no gp value')
            # The PsyQ type-30 expression itself carries the GP subtraction.
            # Applying GP a second time would turn the seed's zero offset
            # into an out-of-range negative address.
            delta=resolve(expr)
            if delta&0x80000000: delta-=0x100000000
            if not -32768<=delta<=32767: raise CheckError(f'GP relocation out of range: {delta}')
            word=(word&0xffff0000)|(delta&0xffff); applied_value=delta
        else: raise CheckError(f'unsupported PsyQ patch type {typ} at {rel:#x}')
        struct.pack_into('<I',actual,rel,word); patches.append({"type":emitted,"offset":rel,"applied_value":applied_value,**({"split":"HI16" if typ==82 else "LO16"} if emitted!=typ else {})})
    load_addr,text_size=reference.load,reference.size
    file_off=HEADER+(addr-load_addr)
    if addr<load_addr or addr+size>load_addr+text_size or file_off+size>len(reference.raw):
        raise CheckError(f'function VA range 0x{addr:08x}..0x{addr+size:08x} lies outside executable text at 0x{load_addr:08x} size 0x{text_size:x}')
    expected=reference.raw[file_off:file_off+size]
    diffs=[i for i,(x,y) in enumerate(zip(actual,expected)) if x!=y]
    return {"address":f'0x{addr:08x}',"reference_file_offset":f'0x{file_off:x}',"size":size,"patches":patches,"object_sha256":hashlib.sha256(actual).hexdigest(),"reference_sha256":hashlib.sha256(expected).hexdigest(),"mismatch_count":len(diffs),"mismatch_offsets":diffs[:128],"first_mismatch":None if not diffs else {"offset":diffs[0],"object":actual[diffs[0]],"reference":expected[diffs[0]]},"status":"match" if not diffs else "mismatch"}

def main():
    ap=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--object',required=True,type=Path)
    ap.add_argument('--symbol-map',required=True,type=Path)
    ap.add_argument('--reference',required=True,type=Path)
    ap.add_argument('--function',required=True)
    ap.add_argument('--json-report',required=True,type=Path)
    ap.add_argument('--unit',help='unit name in the data map, whose definitions place items')
    ap.add_argument('--data-map',type=Path,help='data definitions (config/data.json or its snapshot)')
    ap.add_argument('--asm',type=Path,help='assembly listing of the object (default: INPUT.S beside it)')
    ap.add_argument('--layout',type=Path,default=LAYOUT,help='retail section layout (config/section_layout.json)')
    a=ap.parse_args()
    report={"function":a.function,"status":"error"}
    try:
        m=json.loads(a.symbol_map.read_text(encoding='utf-8'))
        o=load_object(a.object)
        reference=Reference(a.reference.read_bytes())
        definitions={}
        if a.data_map is not None:
            if not a.unit: raise CheckError('--data-map needs --unit')
            definitions=json.loads(a.data_map.read_text(encoding='utf-8')).get(a.unit,{})
        asm=a.asm or a.object.with_name('INPUT.S')
        listing=asm.read_text(encoding='latin1') if asm.is_file() else None
        layout=json.loads(a.layout.read_text(encoding='utf-8'))
        placements=ItemPlacements(o,m,reference,definitions,listing,layout)
        report.update(check(o,m,reference,a.function,placements))
    except (CheckError, lnk_symbols.SymbolError, OSError, ValueError, KeyError, AssertionError, IndexError, struct.error) as e:
        report['error']=str(e)
    a.json_report.parent.mkdir(parents=True,exist_ok=True); a.json_report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2)); return 0 if report['status']=='match' else 1
if __name__=='__main__': raise SystemExit(main())
