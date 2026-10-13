"""Classify every uncovered payload byte (reports/link_plan.json gaps).

Inputs (read-only): the retail SLUS_000.07, reports/link_plan.json, the gate's
provenance record (builds/gate/provenance.json of the checkout named by
--provenance), every unit symbol map in config/, analysis/runtime_linked.json,
analysis/inventory.json, config/section_layout.json and the library scan
builds/data_survey/libscan.json written by libscan.py.

Method: every instruction of the retail .text (and of the leading module) is
decoded; lui/addiu|load|store pairs and $gp-relative accesses give the data
address each instruction names, jal gives call edges, and every aligned word of
.rdata/.data/.sdata that holds a payload address gives a data-to-data or
data-to-code pointer.  Each gap is split at the addresses named inside it; each
piece gets its readers, the owner of each reader (matched C unit, asm unit,
certified/uncertified library module, unmatched game function), its neighbours
in the linked image, a content kind and a status.

Writes analysis/data_survey/gaps.json.  Evidence only: no byte is certified.
"""
import argparse
import bisect
import json
import struct
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve()
PROJECT = HERE.parents[2]
ROOT = PROJECT.parent
LOAD = 0x80010000
GP = 0x8008CD74
TEXT = (0x8001380C, 0x8006A578)
LEAD = (0x80010000, 0x800101E0)
GAME_TEXT_END = 0x800588BC          # 2MBYTE.OBJ: first library text
ASM_REGION = (0x80048F54, 0x800588BC)
DATA_LO, DATA_HI = 0x800101E0, 0x8008D088
BSS_END = 0x800A1958
SECTIONS = [('leading module', 0x80010000, 0x800101E0), ('.rdata', 0x800101E0, 0x8001380C),
            ('.text', 0x8001380C, 0x8006A578), ('.data', 0x8006A578, 0x8008CD74),
            ('.sdata', 0x8008CD74, 0x8008D088), ('.sbss (payload zero fill)', 0x8008D088, 0x8008D800)]


def section_of(a):
    for n, s, e in SECTIONS:
        if s <= a < e:
            return n
    return 'outside payload'


def load_json(p):
    return json.loads(Path(p).read_text(encoding='utf-8'))


class Image:
    def __init__(self):
        raw = (ROOT / 'games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07').read_bytes()
        self.b = raw[0x800:]

    def word(self, a):
        return struct.unpack_from('<I', self.b, a - LOAD)[0]

    def bytes(self, s, e):
        return self.b[s - LOAD:e - LOAD]


def build_code_owners(prov, maps, runtime, inventory, libscan, gaps):
    """Sorted (start, end, label, status, unit) for every code range we can name."""
    owners = []
    fn_unit = {}
    for unit, m in maps.items():
        for name, f in m.get('functions', {}).items():
            a = int(f['address'], 16)
            fn_unit[a] = (unit, name, f['size'])
    placed = {}
    for r in prov['runs']:
        if r['kind'] in ('function', 'asm') and r['address']:
            placed[int(r['address'], 16)] = r
    for a, (unit, name, size) in fn_unit.items():
        r = placed.get(a)
        if r is None:
            status = 'mapped_not_placed'
        else:
            status = 'matched_c' if r['kind'] == 'function' else 'asm'
        owners.append((a, a + size, name, status, unit))
    for m in runtime['module_results']:
        a = int(m['address'], 16)
        size = m.get('text_size') or 0
        lib = Path(m['library'].replace('\\', '/')).name
        st = 'library_certified' if m['status'] == 'full_match' else 'library_uncertified'
        owners.append((a, a + size, '%s:%s' % (lib, m['module']), st, None))
    # library objects that match uncovered text exactly (libscan) or partially
    for h in libscan:
        if h['section'] != '.text':
            continue
        for x in h['exact']:
            a = int(x, 16)
            owners.append((a, a + h['size'], '%s:%s' % (Path(h['lib']).name, h['module']), 'library_variant_exact', None))
    owners.sort()
    covered = []
    for s, e, *_ in owners:
        covered.append((s, e))
    # unmatched game functions: inventory starts inside text gaps not already owned
    gap_text = [(s, e) for s, e in gaps if TEXT[0] <= s < TEXT[1] or LEAD[0] <= s < LEAD[1]]
    starts = sorted(int(c['address'], 16) for c in inventory['candidate_starts'])
    extra = []
    for s, e in gap_text:
        inside = [x for x in starts if s <= x < e]
        if not inside or inside[0] != s:
            inside = [s] + inside
        for i, x in enumerate(inside):
            end = inside[i + 1] if i + 1 < len(inside) else e
            if any(os_ <= x < oe for os_, oe in covered):
                continue
            region = 'game' if (x < GAME_TEXT_END) else 'library'
            if LEAD[0] <= x < LEAD[1]:
                region = 'leading'
            extra.append((x, end, 'f_%05x' % (x & 0xFFFFF), 'unmatched_%s_text' % region, None))
    owners.extend(extra)
    owners.sort()
    return owners


class Owners:
    def __init__(self, owners):
        self.o = owners
        self.starts = [x[0] for x in owners]

    def at(self, a):
        i = bisect.bisect_right(self.starts, a) - 1
        while i >= 0:
            s, e, *_ = self.o[i]
            if s <= a < e:
                return self.o[i]
            if s < a - 0x10000:
                break
            i -= 1
        return None

    def fn_starts(self):
        return sorted(set(self.starts))


def scan_code(img, owners):
    """Return list of refs: (target, site, kind, op)."""
    refs = []
    fstarts = set(owners.fn_starts())
    ranges = [LEAD, TEXT]
    for lo, hi in ranges:
        hi_reg = {}
        for a in range(lo, hi, 4):
            if a in fstarts:
                hi_reg = {}
            w = img.word(a)
            op = w >> 26
            rs = (w >> 21) & 31
            rt = (w >> 16) & 31
            rd = (w >> 11) & 31
            imm = w & 0xFFFF
            simm = imm - 0x10000 if imm & 0x8000 else imm
            if op == 0x0F:
                hi_reg[rt] = (imm << 16, a)
                continue
            if op == 3:
                refs.append((((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2), a, 'call', 'jal'))
                hi_reg.pop(31, None)
                continue
            if op == 2:
                refs.append((((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2), a, 'jump', 'j'))
                continue
            mem_load = {0x20: 'lb', 0x21: 'lh', 0x22: 'lwl', 0x23: 'lw', 0x24: 'lbu', 0x25: 'lhu', 0x26: 'lwr', 0x32: 'lwc2'}
            mem_store = {0x28: 'sb', 0x29: 'sh', 0x2A: 'swl', 0x2B: 'sw', 0x2E: 'swr', 0x3A: 'swc2'}
            if op in mem_load or op in mem_store:
                name = mem_load.get(op) or mem_store.get(op)
                if rs == 28:
                    refs.append(((GP + simm) & 0xFFFFFFFF, a, 'gp', name))
                elif rs in hi_reg:
                    refs.append(((hi_reg[rs][0] + simm) & 0xFFFFFFFF, a, 'hilo', name))
                if op in mem_load and op != 0x32:
                    hi_reg.pop(rt, None)
                continue
            if op == 9:  # addiu
                if rs == 28:
                    refs.append(((GP + simm) & 0xFFFFFFFF, a, 'gp', 'addiu'))
                elif rs in hi_reg:
                    refs.append(((hi_reg[rs][0] + simm) & 0xFFFFFFFF, a, 'hilo', 'addiu'))
                hi_reg.pop(rt, None)
                continue
            if op == 0x0D:  # ori
                if rs in hi_reg and rs != 0:
                    refs.append(((hi_reg[rs][0] | imm) & 0xFFFFFFFF, a, 'hilo_ori', 'ori'))
                hi_reg.pop(rt, None)
                continue
            if 8 <= op <= 0x0E:
                hi_reg.pop(rt, None)
                continue
            if op == 0:
                funct = w & 0x3F
                if funct in (0x08, 0x18, 0x19, 0x1A, 0x1B, 0x11, 0x13, 0x0C, 0x0D):
                    continue
                # GCC switch tables: lui $at,%hi(L); addu $at,$at,$v0; lw $v0,%lo(L)($at)
                if funct == 0x21 and rd in (rs, rt) and rd in hi_reg:
                    continue
                hi_reg.pop(rd, None)
                continue
            if op == 0x12 and rs in (0, 2):
                hi_reg.pop(rt, None)
    return refs


def scan_data_pointers(img):
    refs = []
    for a in range(DATA_LO, DATA_HI, 4):
        v = img.word(a)
        if LOAD <= v < BSS_END:
            refs.append((v, a, 'data_ptr', '.word'))
    return refs


def classify_bytes(img, s, e, ptr_words):
    data = img.bytes(s, e)
    n = len(data)
    if n == 0:
        return 'empty'
    if not any(data):
        return 'zero_fill'
    printable = sum(1 for c in data if 32 <= c < 127 or c in (9, 10, 13))
    zeros = data.count(0)
    if printable + zeros == n and printable >= n * 0.55 and data.rstrip(b'\0').find(b'\0\0\0\0') == -1:
        return 'strings'
    if n >= 8 and s % 4 == 0:
        words = [struct.unpack_from('<I', data, k)[0] for k in range(0, n - n % 4, 4)]
        inptr = sum(1 for w in words if LOAD <= w < BSS_END)
        intext = sum(1 for w in words if TEXT[0] <= w < TEXT[1])
        if words and intext == len(words):
            return 'code_pointer_table'
        if words and inptr >= 0.75 * len(words):
            return 'pointer_table'
        if words and inptr >= 0.25 * len(words):
            return 'mixed_struct_with_pointers'
    return 'binary_table'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--provenance', default=str(ROOT / 'alien_decomp/builds/gate/provenance.json'))
    args = ap.parse_args()
    img = Image()
    plan = load_json(PROJECT / 'reports/link_plan.json')
    prov = load_json(args.provenance)
    runtime = load_json(PROJECT / 'analysis/runtime_linked.json')
    inventory = load_json(PROJECT / 'analysis/inventory.json')
    libscan = load_json(PROJECT / 'builds/data_survey/libscan.json')['hits']
    maps = {}
    for p in sorted((PROJECT / 'config').glob('*.json')):
        d = load_json(p)
        if isinstance(d, dict) and 'functions' in d:
            maps[p.stem] = d
    gaps = [(int(g['start'], 16), int(g['end'], 16)) for g in plan['uncovered_payload_gaps']]

    owners = Owners(build_code_owners(prov, maps, runtime, inventory, libscan, gaps))
    code_refs = scan_code(img, owners)
    data_refs = scan_data_pointers(img)

    # placed objects (non-fallback) in address order, for neighbour/owner evidence
    placed = []
    for r in prov['runs']:
        if r['kind'] != 'fallback' and r['address']:
            a = int(r['address'], 16)
            placed.append((a, a + r['size'], r['kind'], r['unit'], r['symbol']))
    # runtime certified sections (library data/text) as neighbours
    rdata = load_json(PROJECT / 'reports/runtime_data.json')
    for sec in rdata['section_results'] + rdata.get('supplemental_data_only_section_results', []):
        if sec.get('status') == 'full_match' and sec.get('address') and sec.get('size'):
            a = int(sec['address'], 16)
            placed.append((a, a + sec['size'], 'library_' + sec['section'], Path(sec['library'].replace('\\', '/')).name + ':' + sec['module'], sec['section']))
    for m in runtime['module_results']:
        if m['status'] == 'full_match':
            a = int(m['address'], 16)
            placed.append((a, a + m['text_size'], 'library_.text', Path(m['library'].replace('\\', '/')).name + ':' + m['module'], '.text'))
    placed.sort()
    pstarts = [p[0] for p in placed]

    # symbol names every unit map gives to addresses (names + which units reference them)
    named = defaultdict(set)
    sym_users = defaultdict(set)
    for unit, m in maps.items():
        for nm, a in m.get('symbols', {}).items():
            a = int(a, 16)
            named[a].add(nm)
            sym_users[a].add(unit)
    section_starts = defaultdict(list)   # section -> [(addr, unit)]
    for unit, m in maps.items():
        for sec, a in m.get('sections', {}).items():
            section_starts[sec].append((int(a, 16), unit))
    data_defs = {}
    for unit, d in load_json(PROJECT / 'config/data.json').items():
        for nm, x in d.items():
            data_defs[int(x['address'], 16)] = (unit, nm, x['size'])

    by_target = defaultdict(list)
    for t, site, kind, op in code_refs + data_refs:
        by_target[t].append((site, kind, op))
    # also find refs whose target lies anywhere inside each gap
    all_targets = sorted(by_target)

    def reader_info(site, kind):
        if kind == 'data_ptr':
            i = bisect.bisect_right(pstarts, site) - 1
            if i >= 0 and placed[i][0] <= site < placed[i][1]:
                p = placed[i]
                return {'site': hex(site), 'via': kind, 'owner': p[4], 'unit': p[3], 'status': 'placed_' + p[2]}
            return {'site': hex(site), 'via': kind, 'owner': None, 'unit': None, 'status': 'uncovered_data'}
        o = owners.at(site)
        if o is None:
            return {'site': hex(site), 'via': kind, 'owner': None, 'unit': None, 'status': 'unknown_code'}
        return {'site': hex(site), 'via': kind, 'owner': o[2], 'unit': o[4], 'status': o[3]}

    def neighbour(a, before):
        if before:
            i = bisect.bisect_right(pstarts, a - 1) - 1
            while i >= 0 and placed[i][1] > a:
                i -= 1
            return placed[i] if i >= 0 else None
        i = bisect.bisect_left(pstarts, a)
        return placed[i] if i < len(placed) else None

    def unit_section_owner(a, sec):
        """Unit whose recorded section start is the greatest one <= a."""
        lst = sorted(section_starts.get(sec, []))
        best = None
        for s, u in lst:
            if s <= a:
                best = (s, u)
        nxt = next(((s, u) for s, u in lst if s > a), None)
        return best, nxt

    libtext_hits = defaultdict(list)
    for h in libscan:
        b = h['best']
        if not b:
            continue
        libtext_hits[h['section']].append(h)

    out = []
    for gs, ge in gaps:
        sec = section_of(gs)
        lo = bisect.bisect_left(all_targets, gs)
        hi = bisect.bisect_left(all_targets, ge)
        inside = all_targets[lo:hi]
        cuts = sorted(set([gs] + [t for t in inside if t > gs]))
        pieces = []
        for i, c in enumerate(cuts):
            pe = cuts[i + 1] if i + 1 < len(cuts) else ge
            rs = [reader_info(site, kind) | {'op': op} for site, kind, op in by_target.get(c, [])]
            pieces.append({'start': hex(c), 'end': hex(pe), 'size': pe - c,
                           'names': sorted(named.get(c, [])),
                           'named_by_units': sorted(sym_users.get(c, [])),
                           'content': classify_bytes(img, c, pe, None),
                           'readers': rs})
        prev = neighbour(gs, True)
        nxt = neighbour(ge, False)
        sec_key = {'.rdata': '.rdata', '.data': '.data', '.sdata': '.sdata'}.get(sec)
        sec_owner = unit_section_owner(gs, sec_key) if sec_key else (None, None)
        out.append({'start': hex(gs), 'end': hex(ge), 'size': ge - gs, 'section': sec,
                    'content': classify_bytes(img, gs, ge, None),
                    'prev_placed': None if not prev else {'end': hex(prev[1]), 'kind': prev[2], 'unit': prev[3], 'symbol': prev[4]},
                    'next_placed': None if not nxt else {'start': hex(nxt[0]), 'kind': nxt[2], 'unit': nxt[3], 'symbol': nxt[4]},
                    'unit_section_start_at_or_before': None if not sec_owner[0] else {'address': hex(sec_owner[0][0]), 'unit': sec_owner[0][1]},
                    'unit_section_start_after': None if not sec_owner[1] else {'address': hex(sec_owner[1][0]), 'unit': sec_owner[1][1]},
                    'code_owners_inside': sorted({(o[2], o[3]) for o in owners.o if gs <= o[0] < ge}),
                    'pieces': pieces})
    dst = PROJECT / 'builds/data_survey'
    dst.mkdir(parents=True, exist_ok=True)
    (dst / 'raw_gaps.json').write_text(json.dumps(out, indent=1, default=list))
    print('gaps', len(out), 'code refs', len(code_refs), 'data ptrs', len(data_refs))


if __name__ == '__main__':
    main()
