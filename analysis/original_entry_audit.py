"""Flag direct-call entries with straight-line prefixes before a certified entry."""
from pathlib import Path
import struct
import luna_until_stall as sessions
P=Path(__file__).resolve().parents[1]
latest,_=sessions.validate_report(P/'reports/latest.json')
raw=Path(latest['reference']).read_bytes();load=int(latest['load_address'],0)
words=struct.unpack_from('<'+'I'*(latest['payload_size']//4),raw,0x800)
calls={}
for i,w in enumerate(words):
    if w>>26==3:
        target=((load+i*4+4)&0xf0000000)|((w&0x3ffffff)<<2)
        calls.setdefault(target,[]).append(hex(load+i*4))
intervals=[(int(f['address'],0),int(f['address'],0)+f['size']) for f in latest['checks']]
def control(w):return w>>26 in (1,2,3,4,5,6,7) or (w>>26==0 and (w&63) in (8,9))
suspects=[]
for f in latest['checks']:
    a=int(f['address'],0)
    for t in calls:
        if not 0<a-t<=64 or any(x<=t<y for x,y in intervals):continue
        prefix=words[(t-load)//4:(a-load)//4]
        if not any(control(w) for w in prefix):suspects.append(dict(function=f['function'],owned_start=hex(a),preceding_target=hex(t),callers=calls[t],prefix_words=[hex(w) for w in prefix]))
sessions.write(P/'reports/original_entry_audit.json',dict(status='pass' if not suspects else 'review_required',
    latest_report_sha256=sessions.sha(P/'reports/latest.json'),analyzer_sha256=sessions.sha(Path(__file__)),
    certified_function_count=len(latest['checks']),direct_call_targets=len(calls),maximum_prefix_distance=64,remaining_suspects=suspects,
    limitation='This bounded direct-call audit cannot prove every original source boundary or identify unreferenced indirect entries.'))
assert not suspects,suspects
print('pass: no remaining direct-call straight-line prefix candidates within64 bytes of current certified entries')
