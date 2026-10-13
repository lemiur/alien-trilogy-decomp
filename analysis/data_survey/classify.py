"""Turn the raw survey into analysis/data_survey/gaps.json.

Run order (all read-only on the project; scratch output in builds/data_survey/):
    python analysis/data_survey/libscan.py     # whole-section library scan (exact + best partial)
    python analysis/data_survey/libfuncs.py    # function-level library identification
    python analysis/data_survey/libmodules.py  # module-level indel-tolerant alignment
    python analysis/data_survey/survey.py      # readers, neighbours, pieces
    python analysis/data_survey/classify.py    # this file: kinds, owners, status

Owner inference: the matched units occupy contiguous, ordered .text ranges, so
their .text order is the link order, and every section (.rdata/.data/.sdata)
follows the same object order.  A datum between two placed objects belongs to
a unit in that link-order window; a reader inside the window names the owner.
"""
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve()
PROJECT = HERE.parents[2]
SCR = PROJECT / 'builds/data_survey'

LIB_TEXT = (0x800588BC, 0x8006A578)
GAME_TEXT = (0x8001380C, 0x800588BC)
LEAD = (0x80010000, 0x800101E0)
SBSS_FILL = (0x8008D088, 0x8008D800)


def h(x):
    return int(x, 16)


def unit_order():
    fns = []
    for p in (PROJECT / 'config').glob('*.json'):
        d = json.loads(p.read_text())
        if isinstance(d, dict) and 'functions' in d:
            for n, x in d['functions'].items():
                fns.append((h(x['address']), x['size'], p.stem))
    fns.sort()
    runs = []
    for a, s, u in fns:
        if runs and runs[-1][2] == u:
            runs[-1][1] = a + s
        else:
            runs.append([a, a + s, u])
    return runs


SECTIONS = [('.rdata', 0x800101E0, 0x8001380C), ('.data', 0x8006A578, 0x8008CD74), ('.sdata', 0x8008CD74, 0x8008D088)]


def resolve_owners(out, order, pos, text_slot):
    """Link-order owner assignment over placed game objects and gap pieces.

    Within each section the owners must be non-decreasing in link order.  A
    dynamic programme picks, for every gap piece, a link-order slot so that the
    sequence is monotone, fixed placed objects keep their unit, and as many
    pieces as possible land on one of their reader / pointer-source units
    (an in-window reader can still be an extern user; the DP lets the order
    decide).  Pieces without any reader take the slot only when both
    evidence-backed neighbours agree.
    """
    prov = json.loads((PROJECT.parent / 'alien_decomp/builds/gate/provenance.json').read_text())
    placed = []
    for r in prov['runs']:
        if r['kind'] in ('data', 'generated') and r['address'] and r['unit'] in pos:
            placed.append((h(r['address']), h(r['address']) + r['size'], r['unit']))
    pieces = [p for rec in out if 'pieces' in rec for p in rec['pieces'] if p['kind'] in ('game_data', 'unreferenced')]
    slots = {}
    for i, u in enumerate(order):
        slots[float(i)] = u

    def slot_pos(name):
        if name in pos:
            return float(pos[name])
        m = re.match(r'object between (\S+) and (\S+)', name or '')
        if m and m.group(1) in pos:
            slots[pos[m.group(1)] + 0.5] = name
            return pos[m.group(1)] + 0.5
        return None

    def owner_at(addr):
        for s, e, u in placed:
            if s <= addr < e:
                return u
        for p in pieces:
            if h(p['start']) <= addr < h(p['end']):
                return p.get('owner')
        return None

    img = (PROJECT.parent / 'games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07').read_bytes()[0x800:]

    def targets_of(p):
        a0, a1 = h(p['start']), h(p['end'])
        out_t = []
        for a in range((a0 + 3) & ~3, a1 - 3, 4):
            v = int.from_bytes(img[a - 0x80010000:a - 0x80010000 + 4], 'little')
            if 0x800101E0 <= v < 0x8001380C or 0x8006A578 <= v < 0x8008D088:
                out_t.append(v)
        return out_t

    for p in pieces:
        for u in p['reader_slots']:
            slot_pos(u)
    for _round in range(4):
        for sec, lo_s, hi_s in SECTIONS:
            items = [(s, 'placed', slot_pos(u)) for s, e, u in placed if lo_s <= s < hi_s]
            items += [(h(p['start']), 'piece', p) for p in pieces if lo_s <= h(p['start']) < hi_s]
            items.sort(key=lambda x: x[0])
            P = sorted(slots)
            n = len(P)
            INF = 10 ** 9
            cand_of = []
            for a, t, x in items:
                if t == 'placed':
                    cand_of.append(None)
                    continue
                c = {}
                for u in x['reader_slots']:
                    sp = slot_pos(u)
                    if sp is not None:
                        c[sp] = c.get(sp, 0) + 1
                # a table and the string literals it points at come from one unit
                for site in x['pointer_sites']:
                    o = owner_at(h(site))
                    if o and slot_pos(o) is not None:
                        c[slot_pos(o)] = c.get(slot_pos(o), 0) + 2
                for t in targets_of(x):
                    o = owner_at(t)
                    if o and slot_pos(o) is not None:
                        c[slot_pos(o)] = c.get(slot_pos(o), 0) + 2
                cand_of.append(c)
            # dp[i][k]: min cost for items[0..i] with item i at slot P[k]
            prev = [0] * n
            back = []
            for i, (a, t, x) in enumerate(items):
                best_prefix = []
                m = INF; arg = -1
                for k in range(n):
                    if prev[k] < m:
                        m, arg = prev[k], k
                    best_prefix.append((m, arg))
                cur = [INF] * n; bk = [-1] * n
                for k in range(n):
                    if t == 'placed':
                        if P[k] != x:
                            continue
                        cost = 0
                    else:
                        c = cand_of[i]
                        cost = (sum(c.values()) - c.get(P[k], 0)) if c else 0
                    pm, pa = best_prefix[k]
                    if pm >= INF:
                        continue
                    cur[k] = pm + cost; bk[k] = pa
                back.append(bk)
                prev = cur
            k = min(range(n), key=lambda j: prev[j])
            if prev[k] >= INF:
                continue
            assign = [None] * len(items)
            for i in range(len(items) - 1, -1, -1):
                assign[i] = k
                k = back[i][k]
            evid = [i for i in range(len(items)) if items[i][1] == 'placed' or (cand_of[i] and P[assign[i]] in cand_of[i])]
            for i, (a, t, x) in enumerate(items):
                if t != 'piece':
                    continue
                c = cand_of[i]
                here = P[assign[i]]
                for stale in ('owner_candidates', 'owner_bounds'):
                    x.pop(stale, None)
                if c and here in c:
                    x['owner'] = slots[here]
                    x['owner_confidence'] = 'high'
                    x['owner_evidence'] = 'reader/pointer-source unit consistent with link order (monotone assignment over placed objects and readers)'
                    if any(owner_at(h(site)) == x['owner'] for site in x['pointer_sites']):
                        x['pointer_owner_ok'] = True
                    continue
                before = [P[assign[j]] for j in evid if j < i]
                after = [P[assign[j]] for j in evid if j > i]
                lo = before[-1] if before else -1.0
                hi = after[0] if after else float(len(order))
                if lo == hi:
                    x['owner'] = slots[lo]
                    x['owner_confidence'] = 'medium' if c else 'high'
                    x['owner_evidence'] = ('its readers/pointer sources (%s) would be extern users; ' % ', '.join(sorted(slots.get(q, str(q)) for q in c)) if c else 'no reader; ') + 'bounded on both sides by %s' % slots[lo]
                else:
                    x['owner'] = None
                    x['owner_candidates'] = [slots[q] for q in P if lo <= q <= hi][:12]
                    x['owner_bounds'] = [slots.get(lo, 'start'), slots.get(hi, 'end')]


def main():
    raw = json.loads((SCR / 'raw_gaps.json').read_text())
    libmods = json.loads((SCR / 'libmodules.json').read_text())
    libfuncs = json.loads((SCR / 'libfuncs.json').read_text())
    libscan = json.loads((SCR / 'libscan.json').read_text())['hits']
    runs = unit_order()
    order = [r[2] for r in runs]
    pos = {u: i for i, u in enumerate(order)}

    def text_slot(addr):
        """Link-order slot of a game text address: unit name or 'between A and B'."""
        prev = None
        for s, e, u in runs:
            if s <= addr < e:
                return u, pos[u]
            if e <= addr:
                prev = u
        nxt = next((u for s, e, u in runs if s > addr), None)
        return 'object between %s and %s' % (prev, nxt), (pos[prev] + 0.5 if prev else -0.5)

    # library module identification per text gap (best non-overlapping modules)
    lib_by_gap = defaultdict(list)
    best_mod = {}
    for x in libmods:
        key = (x['library'], x['module'])
        cur = best_mod.get(key)
        if cur is None or (x['exact_word_coverage'], x['token_coverage']) > (cur['exact_word_coverage'], cur['token_coverage']):
            best_mod[key] = x
    for key, x in best_mod.items():
        if x['size'] >= 64 or x['exact_word_coverage'] == 1.0:
            v33 = [y for y in libmods if (y['library'], y['module']) == key and (y['version'] or '').startswith('3.3')]
            lib_by_gap[x['gap']].append({
                'module': '%s:%s' % key, 'module_size': x['size'], 'aligned_at': x['address'],
                'closest_version': x['version'], 'token_coverage': x['token_coverage'],
                'exact_word_coverage': x['exact_word_coverage'],
                'runtime_3_3': None if not v33 else {'token_coverage': v33[0]['token_coverage'], 'exact_word_coverage': v33[0]['exact_word_coverage']},
                'exports': x['exports'][:6]})
    funcs_by_gap = {g['start']: g['pieces'] for g in libfuncs['gaps']}
    exact_data = defaultdict(list)
    for x in libscan:
        if x['section'] != '.text':
            for a in x['exact']:
                exact_data[a].append('%s %s:%s %s (%d bytes)' % (x['lib'].replace('\\', '/').split('/LIB/')[0][-60:], Path(x['lib'].replace('\\', '/')).name, x['module'], x['section'], x['size']))

    out = []
    for g in raw:
        s, e = h(g['start']), h(g['end'])
        rec = {'start': g['start'], 'end': g['end'], 'size': g['size'], 'section': g['section'],
               'content': g['content'], 'prev_placed': g['prev_placed'], 'next_placed': g['next_placed']}
        if LEAD[0] <= s < LEAD[1]:
            rec.update(kind='leading_asm_module', owner='hand-written assembly module linked first (SJIS<->ASCII converters f_10000/f_10170 and their 96+96-byte tables)',
                       status='not_reconstructible_as_c',
                       evidence=['no stack frame, only $t registers, j to the next instruction: not GCC 2.5.7 output',
                                 'called by memcard card_read_header (matched) and f_4879c (unmatched)',
                                 'no SDK object matches it (libscan/libfuncs); outside the AGENTS.md asm region 0x80048f54..0x800588bb',
                                 'its section name is unknown (config/section_layout.json: leading module)'],
                       readers=[r for p in g['pieces'] for r in p['readers']][:8])
            out.append(rec); continue
        if GAME_TEXT[0] <= s < GAME_TEXT[1]:
            slot = text_slot(s)[0]
            rec.update(kind='game_text_unmatched', owner=slot, status='unmatched_game_function',
                       functions=[p['start'] for p in g['pieces']] if False else sorted({o[0] for o in g['code_owners_inside']}),
                       evidence=['game .text between matched units; known walls, outside this survey'])
            out.append(rec); continue
        if LIB_TEXT[0] <= s < LIB_TEXT[1]:
            mods = sorted(lib_by_gap.get(g['start'], []), key=lambda m: (h(m['aligned_at']), -m['exact_word_coverage']))
            reasons = []
            for m in mods:
                if m['exact_word_coverage'] == 1.0:
                    reasons.append('%s identical (relocation-masked) to %s' % (m['module'], m['closest_version']))
            rec.update(kind='library_text', owner='Sony Runtime Library (variant)', status='library_variant',
                       modules=mods, evidence=reasons,
                       functions=[{'start': f['start'], 'size': f['size'], 'names': f['names'],
                                   'best': [{k: b[k] for k in ('version', 'library', 'module', 'names', 'size', 'ratio', 'word_exact')} for b in f['best'][:2]]}
                                  for f in funcs_by_gap.get(g['start'], [])])
            out.append(rec); continue
        if SBSS_FILL[0] <= s < SBSS_FILL[1] or (s == 0x8008D087):
            rec.update(kind='sbss_payload_zero_fill', owner='linker (.sdata alignment byte + first 1912 bytes of .sbss carried as zeros up to the 0x800 file rounding)',
                       status='layout_tooling', evidence=['config/section_layout.json .sbss: the payload file carries the first 1912 bytes of .sbss as zeros',
                                                          'sectstart(.sbss)=0x8008d088; .sdata ends at 0x8008d087'])
            out.append(rec); continue
        # data sections
        pieces = []
        last = None
        for p in g['pieces']:
            readers = p['readers']
            code = [r for r in readers if r['via'] != 'data_ptr']
            ptrs = [r for r in readers if r['via'] == 'data_ptr']
            # 2MBYTE.OBJ imports _stacksize from the program: a game datum
            lib_readers = [r for r in code if (r['status'] or '').startswith(('library', 'unmatched_library'))
                           and not str(r['owner']).startswith('2MBYTE')]
            game_readers = [r for r in code if r not in lib_readers]
            lib_zone = all(n is None or str(n.get('kind', '')).startswith('library_') for n in (g['prev_placed'], g['next_placed']))
            if p['content'] == 'zero_fill' and p['size'] < 8 and not readers and len(g['pieces']) == 1:
                kind = 'alignment_padding'
            elif lib_readers and not game_readers:
                kind = 'library_data'
            elif game_readers:
                kind = 'game_data'
            elif ptrs:
                kind = 'game_data' if any((r['unit'] or r['status'] == 'uncovered_data') and not str(r.get('owner') or '').startswith('LIB') for r in ptrs) else 'library_data'
                if ptrs and all(r['status'] == 'uncovered_data' for r in ptrs) and last:
                    kind = last['kind'] if last['kind'] != 'alignment_padding' else 'game_data'
            else:
                kind = last['kind'] if last else 'unreferenced'
            if lib_zone and not game_readers and kind not in ('library_data', 'alignment_padding'):
                kind = 'library_data'
            units = set()
            unmatched = set()
            for r in game_readers:
                if r['status'] == 'matched_c' or r['status'] == 'asm':
                    units.add(r['unit'])
                elif r['status'] == 'unmatched_game_text':
                    unmatched.add(r['owner'])
                    units.add(text_slot(h(r['site']))[0])
            for r in ptrs:
                if r['unit']:
                    units.add(r['unit'])
            piece = {'start': p['start'], 'end': p['end'], 'size': p['size'], 'content': p['content'], 'names': p['names'],
                     'kind': kind,
                     'readers': sorted({'%s (%s, %s)' % (r['owner'], r['unit'] or '-', r['status']) for r in code})[:12],
                     'blocked_by': sorted(unmatched),
                     'matched_readers': sorted({r['owner'] for r in game_readers if r['status'] in ('matched_c', 'asm', 'library_certified')}),
                     'reader_slots': sorted({text_slot(h(r['site']))[0] for r in game_readers if r['status'] in ('matched_c', 'asm', 'unmatched_game_text')}),
                     'pointer_sites': sorted({r['site'] for r in ptrs})}
            if kind == 'library_data':
                lib = sorted({r['owner'] for r in lib_readers})
                piece['library_readers'] = lib[:10]
                ex = exact_data.get(p['start'])
                if ex:
                    piece['exact_in_archive'] = ex
            if not readers and last is not None:
                piece['note'] = 'no direct reference: interior or tail of the preceding object'
                piece['inherits'] = last['start']
            pieces.append(piece)
            last = piece
        rec.update(pieces=pieces)
        out.append(rec)

    resolve_owners(out, order, pos, text_slot)
    for rec in out:
        if 'pieces' not in rec:
            continue
        pieces = rec['pieces']
        pu = (rec['prev_placed'] or {}).get('unit'); nu = (rec['next_placed'] or {}).get('unit')
        for piece in pieces:
            if piece['kind'] == 'alignment_padding':
                piece['status'] = 'tooling_padding'
                piece['owner'] = pu if (pu and pu == nu) else None
            elif piece['kind'] == 'library_data':
                piece['status'] = 'blocked_by_library_text'
            elif piece['kind'] in ('game_data', 'unreferenced'):
                own = piece.get('owner')
                if own is None:
                    piece['status'] = 'owner_unresolved'
                elif own.startswith('object between'):
                    piece['status'] = 'blocked_by_unmatched'
                elif piece['matched_readers'] or piece.get('pointer_owner_ok'):
                    piece['status'] = 'definable_now'
                elif piece['blocked_by']:
                    piece['status'] = 'blocked_by_unmatched'
                else:
                    piece['status'] = 'definable_now'
                    piece.setdefault('note', 'no direct reader; owner bounded by link order (interior/tail of a neighbouring object or an unreferenced initialised static)')
        kinds = Counter(); stats = Counter()
        for piece in pieces:
            kinds[piece['kind']] += piece['size']; stats[piece['status']] += piece['size']
        gap_kind = kinds.most_common(1)[0][0] if kinds else 'unknown'
        owners = sorted({p.get('owner') for p in pieces if p.get('owner')})
        if gap_kind == 'alignment_padding':
            gap_kind = 'alignment_padding_intra_unit' if (pu and pu == nu) else 'alignment_padding_between_objects'
        rec.update(kind=gap_kind, kind_bytes=dict(kinds), status_bytes=dict(stats),
                   status=stats.most_common(1)[0][0] if stats else 'unknown',
                   owner=owners[0] if len(owners) == 1 else (owners or None))

    # totals
    by_kind = Counter(); by_status = Counter()
    for r in out:
        if 'kind_bytes' in r:
            for k, v in r['kind_bytes'].items():
                by_kind[k if k != 'alignment_padding' else r['kind']] += v
            for k, v in r['status_bytes'].items():
                by_status[k] += v
        else:
            by_kind[r['kind']] += r['size']; by_status[r['status']] += r['size']
    plan = defaultdict(lambda: {'bytes': 0, 'sections': Counter(), 'objects': []})
    for r in out:
        for pc in r.get('pieces', []):
            if pc['status'] in ('definable_now', 'owner_unresolved', 'blocked_by_unmatched'):
                key = (pc['status'], pc.get('owner') or ' or '.join(map(str, pc.get('owner_bounds') or [])))
                item = plan[key]
                item['bytes'] += pc['size']; item['sections'][r['section']] += pc['size']
                if pc['names'] or pc['size'] >= 64:
                    item['objects'].append('%s %s (%d)' % (pc['start'], ','.join(pc['names']) or pc['content'], pc['size']))
    plan_list = [{'status': k[0], 'owner': k[1], 'bytes': v['bytes'], 'sections': dict(v['sections']), 'objects': v['objects'][:40]}
                 for k, v in sorted(plan.items(), key=lambda kv: (['definable_now', 'owner_unresolved', 'blocked_by_unmatched'].index(kv[0][0]), -kv[1]['bytes']))]
    doc = {
        'schema': 1,
        'plan': plan_list,
        'generated_by': 'analysis/data_survey/{libscan,libfuncs,libmodules,survey,classify}.py',
        'inputs': {'link_plan': 'reports/link_plan.json', 'provenance': 'alien_decomp/builds/gate/provenance.json (gate run at 9f5fa031)',
                   'reference_sha256': 'f424660920764ac70f697b5b909a42fe259a76e60acbf5b01c6ca3ee8c8f8e98'},
        'totals': {'gaps': len(out), 'bytes': sum(r['size'] for r in out), 'by_kind': dict(by_kind.most_common()),
                   'by_status': dict(by_status.most_common())},
        'link_order': order,
        'gaps': out,
    }
    (HERE.parent / 'gaps.json').write_text(json.dumps(doc, indent=1))
    print(json.dumps(doc['totals'], indent=1))


if __name__ == '__main__':
    main()
