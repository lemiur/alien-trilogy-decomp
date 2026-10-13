"""Scan every PsyQ library object in ../sdk against the uncovered library ranges.

For each section of each module (deduplicated by content), relocation-masked
bytes are anchored inside every uncovered gap and scored; exact masked matches
and best partial alignments are written to builds/data_survey/libscan.json
(git-ignored).  Evidence only: nothing here certifies bytes.
"""
import glob, hashlib, json, os, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve()
PROJECT = HERE.parents[2]
ROOT = PROJECT.parent
sys.path.insert(0, str(ROOT / 'shared-tools'))
import psyqobj  # noqa: E402

LOAD = 0x80010000

def payload():
    d = (ROOT / 'games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07').read_bytes()
    return d[0x800:]

def masked(m, sn):
    code = bytes(m.code[sn]); mask = bytearray(b'\x01' * len(code))
    for t, off, e in m.patches.get(sn, []):
        for k in range(off & ~3, min(len(code), (off & ~3) + 4)):
            mask[k] = 0
    return code, bytes(mask)

def score(code, mask, hay, st):
    n = len(code); good = tot = 0
    for k in range(0, n, 4):
        if st + k + 4 > len(hay): break
        if not mask[k]: continue
        tot += 1
        if hay[st + k:st + k + 4] == code[k:k + 4]: good += 1
    return good, tot

def main():
    hay = payload()
    plan = json.loads((PROJECT / 'reports/link_plan.json').read_text())
    gaps = [(int(g['start'], 16), int(g['end'], 16)) for g in plan['uncovered_payload_gaps']]
    big = [(s, e) for s, e in gaps if e - s >= 16]
    seen_lib = set(); seen_sec = {}
    out = []
    libs = sorted(glob.glob(str(ROOT / 'sdk' / '**' / '*.[LlOo][IiBb][BbJj]'), recursive=True))
    for lib in libs:
        raw = open(lib, 'rb').read()
        h = hashlib.sha256(raw).hexdigest()
        rel = os.path.relpath(lib, ROOT / 'sdk')
        if h in seen_lib:
            continue
        seen_lib.add(h)
        try:
            mods = psyqobj.load(lib)
        except Exception:
            continue
        for m in mods:
            if isinstance(m, Exception):
                continue
            for sn, nm in m.sections.items():
                if sn not in m.code or len(m.code[sn]) < 16 or nm not in ('.text', '.data', '.rdata', '.sdata'):
                    continue
                code, mask = masked(m, sn)
                key = hashlib.sha256(code + mask).hexdigest()
                if key in seen_sec:
                    seen_sec[key]['also'].append(rel + '::' + m.name)
                    continue
                best = None; exact = []
                n = len(code)
                anchors = []
                for k in range(0, n - 12, 4):
                    if all(mask[k:k + 12]) and code[k:k + 12] != b'\0' * 12:
                        anchors.append(k)
                if len(anchors) > 48:
                    step = len(anchors) / 48.0
                    anchors = [anchors[int(i * step)] for i in range(48)]
                cands = set()
                for s, e in big:
                    lo, hi = s - LOAD, e - LOAD
                    region = hay[max(0, lo - n):hi + n]
                    base = max(0, lo - n)
                    for k in anchors:
                        a = code[k:k + 12]
                        p = region.find(a)
                        while p != -1:
                            st = base + p - k
                            if st >= 0 and st % 4 == 0 and st < hi and st + n > lo:
                                cands.add(st)
                            p = region.find(a, p + 4)
                for st in cands:
                    g, t = score(code, mask, hay, st)
                    if not t: continue
                    r = g / t
                    if r == 1.0 and st + n <= len(hay):
                        exact.append(st + LOAD)
                    if best is None or r > best[0]:
                        best = (r, st + LOAD, g, t)
                rec = {'lib': rel, 'module': m.name, 'section': nm, 'size': n,
                       'masked_bytes': n - sum(mask), 'exact': [hex(x) for x in sorted(exact)],
                       'best': None if best is None else {'ratio': round(best[0], 4), 'address': hex(best[1]), 'words': best[2], 'of': best[3]},
                       'xdefs': [x[0] for x in m.xdefs][:12], 'also': []}
                seen_sec[key] = rec
                if best and best[0] >= 0.5:
                    out.append(rec)
    dst = PROJECT / 'builds/data_survey'; dst.mkdir(parents=True, exist_ok=True)
    (dst / 'libscan.json').write_text(json.dumps({'libraries_scanned': len(seen_lib), 'sections_scanned': len(seen_sec), 'hits': out}, indent=1))
    print(len(seen_lib), len(seen_sec), len(out))

if __name__ == '__main__':
    main()
