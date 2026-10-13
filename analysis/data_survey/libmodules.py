"""Module-level, indel-tolerant comparison of SDK library objects with the
uncovered library text (0x800588bc..0x8006a578).

For every .text section of the runtime libraries the game links (all SDK
versions, deduplicated), relocation-blind instruction tokens are aligned with
each uncovered library gap (difflib).  Reported per module and gap: token
coverage (aligned tokens / module tokens), exact-word coverage (aligned words
equal to the retail word or covered by a relocation), the retail address the
alignment starts at and the number of differing instruction runs.

Writes builds/data_survey/libmodules.json (git-ignored).  Evidence only.
"""
import difflib
import glob
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
PROJECT = HERE.parents[2]
ROOT = PROJECT.parent
sys.path.insert(0, str(ROOT / 'shared-tools'))
sys.path.insert(0, str(HERE.parent))
import psyqobj  # noqa: E402
from libfuncs import token, version_of  # noqa: E402

LOAD = 0x80010000
LIB_LO, LIB_HI = 0x800588BC, 0x8006A578
LIBS = {'LIBSPU.LIB', 'LIBCD.LIB', 'LIBGPU.LIB', 'LIBETC.LIB', 'LIBGTE.LIB', 'LIBAPI.LIB', 'LIBC2.LIB',
        'LIBPRESS.LIB', 'LIBCARD.LIB', 'LIBSND.LIB', 'LIBSN.LIB', 'LIBCOMB.LIB', 'LIBMATH.LIB'}


def main():
    img = (ROOT / 'games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07').read_bytes()[0x800:]
    W = lambda a: struct.unpack_from('<I', img, a - LOAD)[0]
    plan = json.loads((PROJECT / 'reports/link_plan.json').read_text())
    gaps = [(int(g['start'], 16), int(g['end'], 16)) for g in plan['uncovered_payload_gaps']]
    lgaps = [(s, e) for s, e in gaps if LIB_LO <= s < LIB_HI and e - s >= 16]
    gtoks = {}
    for s, e in lgaps:
        words = [W(a) for a in range(s, e, 4)]
        gtoks[(s, e)] = (words, [token(w) for w in words])
    seen = set(); seen_sec = set(); out = []
    for lib in sorted(glob.glob(str(ROOT / 'sdk' / '**' / '*.[Ll][Ii][Bb]'), recursive=True)):
        if os.path.basename(lib).upper() not in LIBS:
            continue
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
                if nm != '.text' or sn not in m.code or len(m.code[sn]) < 16:
                    continue
                code = bytes(m.code[sn])
                key = hashlib.sha256(code).hexdigest()
                if key in seen_sec:
                    continue
                seen_sec.add(key)
                mask = bytearray(b'\x01' * len(code))
                for t, off, e in m.patches.get(sn, []):
                    for k in range(off & ~3, min(len(code), (off & ~3) + 4)):
                        mask[k] = 0
                words = [struct.unpack_from('<I', code, k)[0] for k in range(0, len(code) - 3, 4)]
                wm = [mask[k] for k in range(0, len(code) - 3, 4)]
                toks = [token(w) for w in words]
                tset = set(toks)
                best = None
                for (s, e), (gw, gt) in gtoks.items():
                    if len(tset & set(gt)) < 0.5 * len(tset):
                        continue
                    sm = difflib.SequenceMatcher(None, gt, toks, autojunk=False)
                    blocks = [b for b in sm.get_matching_blocks() if b.size >= 3]
                    if not blocks:
                        continue
                    matched = sum(b.size for b in blocks)
                    exact = 0
                    for b in blocks:
                        for k in range(b.size):
                            if not wm[b.b + k] or words[b.b + k] == gw[b.a + k]:
                                exact += 1
                    cov = matched / len(toks)
                    if best is None or cov > best['token_coverage']:
                        best = {'gap': hex(s), 'token_coverage': round(cov, 4), 'exact_word_coverage': round(exact / len(toks), 4),
                                'address': hex(s + 4 * (blocks[0].a - blocks[0].b)), 'runs': len(blocks),
                                'retail_span': [hex(s + 4 * blocks[0].a), hex(s + 4 * (blocks[-1].a + blocks[-1].size))]}
                if best and best['token_coverage'] >= 0.6:
                    out.append({'library': os.path.basename(lib), 'version': version_of(rel), 'path': rel, 'module': m.name,
                                'size': len(code), 'exports': [x[0] for x in m.xdefs][:10], **best})
    dst = PROJECT / 'builds/data_survey'
    dst.mkdir(parents=True, exist_ok=True)
    (dst / 'libmodules.json').write_text(json.dumps(out, indent=1))
    print(len(seen), len(seen_sec), len(out))


if __name__ == '__main__':
    main()
