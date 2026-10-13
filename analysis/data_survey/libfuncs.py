"""Identify the uncovered library-range text function by function.

Every .text section of every PsyQ library object in ../sdk (all versions,
deduplicated) is split at its exported definitions.  Every uncovered gap in
the library text range (0x800588bc..0x8006a578) is split at inventory starts,
call targets and exported names from the unit symbol maps.  Each piece is
compared with corpus functions of similar length through relocation-blind
instruction tokens (immediates and jump targets dropped), then the best
candidates are scored word for word with the corpus relocations masked.

Writes builds/data_survey/libfuncs.json (git-ignored).  Evidence only.
"""
import bisect
import difflib
import glob
import hashlib
import json
import os
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve()
PROJECT = HERE.parents[2]
ROOT = PROJECT.parent
sys.path.insert(0, str(ROOT / 'shared-tools'))
import psyqobj  # noqa: E402

LOAD = 0x80010000
LIB_LO, LIB_HI = 0x800588BC, 0x8006A578


def token(w):
    op = w >> 26
    if op == 0:
        return w & 0xFFFFFFC0 | (w & 0x3F)  # R-type: keep all (shamt included)
    if op in (2, 3):
        return op << 26
    return w & 0xFFFF0000


def version_of(rel):
    r = rel.replace('\\', '/')
    m = re.search(r'Version (\d\.\d)', r) or re.search(r'Library (\d\.\d)', r) or re.search(r'Run-time Library (\d\.\d)', r)
    tag = m.group(1) if m else None
    for key, val in (('OLD_LIBS/LIB34', '3.4'), ('LIB35.ZIP', '3.5-old'), ('LIB37.ZIP', '3.7'), ('40X.ZIP', '4.0x'),
                     ('LIB42E', '4.2E'), ('Lib44.zip', '4.4z'), ('Lib45.zip', '4.5'), ('Floppies', 'floppy-isa'),
                     ('psyq/', 'psyq-install'), ('Yaroze', 'yaroze'), ('CodeWar', 'codewarrior'), ('4.7', '4.7'),
                     ('SAMPLE', 'sample')):
        if key.lower() in r.lower():
            tag = (tag + '/' if tag else '') + val
    return tag or r.split('/')[0]


def corpus():
    seen = set(); seen_fn = {}
    funcs = []
    for lib in sorted(glob.glob(str(ROOT / 'sdk' / '**' / '*.[LlOo][IiBb][BbJj]'), recursive=True)):
        raw = open(lib, 'rb').read()
        h = hashlib.sha256(raw).hexdigest()
        if h in seen:
            continue
        seen.add(h)
        rel = os.path.relpath(lib, ROOT / 'sdk')
        try:
            mods = psyqobj.load(lib)
        except Exception:
            continue
        for m in mods:
            if isinstance(m, Exception):
                continue
            for sn, nm in m.sections.items():
                if nm != '.text' or sn not in m.code or len(m.code[sn]) < 8:
                    continue
                code = bytes(m.code[sn])
                mask = bytearray(b'\x01' * len(code))
                for t, off, e in m.patches.get(sn, []):
                    for k in range(off & ~3, min(len(code), (off & ~3) + 4)):
                        mask[k] = 0
                defs = sorted({off for (n, s, off) in m.xdefs if s == sn})
                names = defaultdict(list)
                for (n, s, off) in m.xdefs:
                    if s == sn:
                        names[off].append(n)
                if not defs or defs[0] != 0:
                    defs = [0] + defs
                for i, off in enumerate(defs):
                    end = defs[i + 1] if i + 1 < len(defs) else len(code)
                    if end - off < 8:
                        continue
                    body = code[off:end]; bm = mask[off:end]
                    key = hashlib.sha256(body + bytes(bm)).hexdigest()
                    entry = {'lib': rel, 'version': version_of(rel), 'module': m.name,
                             'names': names.get(off, ['<module+0x%x>' % off]), 'size': end - off}
                    if key in seen_fn:
                        seen_fn[key]['also'].append(entry['version'] + ':' + os.path.basename(rel) + ':' + m.name)
                        continue
                    words = [struct.unpack_from('<I', body, k)[0] for k in range(0, len(body) - 3, 4)]
                    wm = [bm[k] for k in range(0, len(body) - 3, 4)]
                    entry.update({'words': words, 'wmask': wm, 'tokens': [token(w) for w in words], 'also': []}); entry['tset'] = set(entry['tokens'])
                    seen_fn[key] = entry
                    funcs.append(entry)
    return funcs


def main():
    img = (ROOT / 'games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07').read_bytes()[0x800:]
    W = lambda a: struct.unpack_from('<I', img, a - LOAD)[0]
    plan = json.loads((PROJECT / 'reports/link_plan.json').read_text())
    gaps = [(int(g['start'], 16), int(g['end'], 16)) for g in plan['uncovered_payload_gaps']]
    lgaps = [(s, e) for s, e in gaps if LIB_LO <= s < LIB_HI and e - s >= 8]
    inv = json.loads((PROJECT / 'analysis/inventory.json').read_text())
    starts = set(int(c['address'], 16) for c in inv['candidate_starts'])
    for a in range(0x80010000, LIB_HI, 4):
        w = W(a)
        if w >> 26 == 3:
            starts.add(((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    named = defaultdict(set)
    for p in (PROJECT / 'config').glob('*.json'):
        d = json.loads(p.read_text())
        if isinstance(d, dict) and 'symbols' in d:
            for n, a in d['symbols'].items():
                named[int(a, 16)].add(n)
                starts.add(int(a, 16))
    funcs = corpus()
    by_len = defaultdict(list)
    for f in funcs:
        by_len[len(f['words'])].append(f)
    out = []
    for s, e in lgaps:
        cuts = sorted(x for x in starts if s <= x < e and x % 4 == 0)
        if not cuts or cuts[0] != s:
            cuts = [s] + cuts
        pieces = []
        for i, c in enumerate(cuts):
            pe = cuts[i + 1] if i + 1 < len(cuts) else e
            words = [W(a) for a in range(c, pe, 4)]
            toks = [token(w) for w in words]
            n = len(words)
            cands = []
            for L in range(max(2, int(n * 0.6)), int(n * 1.6) + 2):
                cands.extend(by_len.get(L, []))
            scored = []
            tset = set(toks)
            for f in cands:
                inter = len(tset & f['tset'])
                scored.append((inter / max(1, len(tset | f['tset'])), f))
            scored.sort(key=lambda x: -x[0])
            best = []
            for _, f in scored[:40]:
                sm = difflib.SequenceMatcher(None, toks, f['tokens'], autojunk=False)
                r = sm.ratio()
                exact = None
                if len(f['words']) == n:
                    good = sum(1 for k in range(n) if (not f['wmask'][k]) or f['words'][k] == words[k])
                    exact = good / n
                best.append((r, exact, f))
            best.sort(key=lambda x: (-(x[1] or 0) if x[1] == 1.0 else 0, -x[0]))
            top = [{'ratio': round(r, 4), 'word_exact': None if ex is None else round(ex, 4), 'version': f['version'],
                    'library': os.path.basename(f['lib'].replace('\\', '/')), 'module': f['module'], 'names': f['names'],
                    'size': f['size'], 'identical_in': f['also'][:8]} for r, ex, f in best[:5]]
            pieces.append({'start': hex(c), 'end': hex(pe), 'size': pe - c, 'names': sorted(named.get(c, [])), 'best': top})
        out.append({'start': hex(s), 'end': hex(e), 'size': e - s, 'pieces': pieces})
    dst = PROJECT / 'builds/data_survey'
    dst.mkdir(parents=True, exist_ok=True)
    (dst / 'libfuncs.json').write_text(json.dumps({'corpus_functions': len(funcs), 'gaps': out}, indent=1))
    print('corpus', len(funcs), 'gaps', len(out))


if __name__ == '__main__':
    main()
