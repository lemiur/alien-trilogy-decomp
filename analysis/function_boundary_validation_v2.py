"""Replay all complete corrected entries and validate zero-credit accounting."""
from pathlib import Path
import struct
import luna_until_stall as sessions
P=Path(__file__).resolve().parents[1]
latest,current=sessions.validate_report(P/'reports/latest.json')
baseline=sessions.read(P/'analysis/luna_until_stall/baseline_latest.json')
old={r['function']:r for r in baseline['checks']}
new={r['function']:r for r in latest['checks']}
additions,baseline_corrections=sessions.compare_functions(old,new)
rows=sessions.read(P/'reports/function_boundary_corrections.json')['corrections']
raw=Path(latest['reference']).read_bytes()
load=int(latest['load_address'],0)
targets={int(r[k]['address'],0):[] for r in rows for k in ('previous','current')}
for index in range(0,latest['payload_size'],4):
    word=struct.unpack_from('<I',raw,0x800+index)[0]
    if word>>26 in (2,3):
        pc=load+index
        target=((pc+4)&0xf0000000)|((word&0x3ffffff)<<2)
        if target in targets:targets[target].append(hex(pc))
proofs=[]
for row in rows:
    previous,now=row['previous'],row['current']
    assert row['new_function_credit']==0
    report_path=P/f'reports/session{row["session"]-1}_full_verify.json'
    history=sessions.read(report_path)
    former=next(r for r in history['checks'] if r['function']==previous['function'])
    assert all(former[k]==previous[k] for k in ('unit','address','size'))
    check=new[now['function']]
    assert all(check[k]==now[k] for k in ('unit','address','size'))
    assert check['object_sha256']==check['reference_sha256']
    assert previous['function'] not in new
    a,b=int(now['address'],0),int(previous['address'],0)
    assert a<b and a+now['size']==b+previous['size']
    assert targets[a]==[row['direct_caller']] and targets[b]==[]
    prefix=raw[0x800+a-load:0x800+b-load]
    assert len(prefix)==row['extent_delta_bytes']
    proofs.append(dict(previous=previous,current=now,entry_prefix_words=[hex(w) for w in struct.unpack('<'+'I'*(len(prefix)//4),prefix)],
        previous_evidence_report=str(report_path.relative_to(P)),previous_evidence_sha256=sessions.sha(report_path),
        previous_bytes_retained=previous['size'],extent_delta=len(prefix),new_function_credit=0))
negative=[]
for label,mutate in [('unexplained_deleted_identity',lambda d:d.pop('f_13a44')),
                     ('wrong_corrected_extent',lambda d:d['f_23800'].update(size=52)),
                     ('wrong_corrected_start',lambda d:d['f_2576c'].update(address='0x80025770'))]:
    candidate={name:dict(d) for name,d in new.items()};mutate(candidate)
    try:sessions.compare_functions(old,candidate)
    except AssertionError:negative.append(dict(case=label,rejected=True))
    else:raise AssertionError(label)
sessions.write(P/'reports/function_boundary_validation.json',dict(status='pass',
    latest_report_sha256=sessions.sha(P/'reports/latest.json'),correction_report_sha256=sessions.sha(P/'reports/function_boundary_corrections.json'),
    analyzer_sha256=sessions.sha(Path(__file__)),corrected_entries=proofs,target_references={hex(k):v for k,v in targets.items()},
    corrected_extent_delta=sum(r['extent_delta_bytes'] for r in rows),corrected_function_new_credit=0,
    baseline_definition_corrections=len(baseline_corrections),new_functions=len(additions),negative_checks=negative))
print(f'pass: {len(rows)} corrected complete entries, retained suffix bytes, zero new-function credit; three rejection cases')
