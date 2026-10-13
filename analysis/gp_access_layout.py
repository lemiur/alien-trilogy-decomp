"""Inventory minimum GP-relative access cells in certified recovered routines.

These are observed access intervals, not allocated globals or BSS sections.
They earn no payload coverage and do not prove array or enclosing object sizes.
"""
import copy
import hashlib
import importlib.util
import json
import struct
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('startup', PROJECT / 'analysis/startup_layout.py')
startup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(startup)

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def build_report(latest, raw, expected_hash):
    layout = startup.build_report(raw, expected_hash)
    if not latest['all_selected_checks_pass'] or latest['reference_sha256'] != expected_hash:
        raise ValueError('GP inventory requires passing certification for the pinned reference')
    gp = int(layout['startup_gp'], 0)
    clear = layout['zero_clear_interval']
    clear_start, clear_end = int(clear['start'],0), int(clear['end_exclusive'],0)
    load, payload_size = struct.unpack_from('<II',raw,0x18)
    # Only byte/halfword/word operations with an explicit GP base. Address
    # construction and all non-GP expressions remain outside this inventory.
    widths = {0x20:('lb',1,'read'),0x21:('lh',2,'read'),0x23:('lw',4,'read'),
              0x24:('lbu',1,'read'),0x25:('lhu',2,'read'),0x28:('sb',1,'write'),
              0x29:('sh',2,'write'),0x2b:('sw',4,'write')}
    cells = {}
    for check in latest['checks']:
        if check['status'] != 'match':
            raise ValueError('unmatched routine in certification report')
        for field, hash_field in (('source','source_sha256'),('symbol_map','map_sha256'),
                                  ('object','object_file_sha256')):
            if digest(check[field]) != check[hash_field]:
                raise ValueError(f'stale {field} evidence for {check["function"]}')
        address, size = int(check['address'],0),check['size']
        if address < load or size <= 0 or size % 4 or address+size > load+payload_size:
            raise ValueError('invalid certified function interval')
        code = raw[0x800+address-load:0x800+address-load+size]
        if hashlib.sha256(code).hexdigest() != check['reference_sha256']:
            raise ValueError('certified function reference extent or hash differs')
        mapping = json.loads(Path(check['symbol_map']).read_text())
        if mapping.get('gp') is not None and int(mapping['gp'],0) != gp:
            raise ValueError('unit GP differs from independently replayed startup GP')
        for offset in range(0,size,4):
            word = struct.unpack_from('<I',code,offset)[0]
            opcode, base = word>>26,(word>>21)&31
            if opcode not in widths or base != 28:
                continue
            instruction,width,operation = widths[opcode]
            displacement = word&0xffff
            if displacement&0x8000:
                displacement -= 0x10000
            target = gp+displacement
            if target % width:
                raise ValueError('unaligned observed GP-relative access')
            key = (target,width)
            cell = cells.setdefault(key,dict(address=hex(target),end_exclusive=hex(target+width),
                    minimum_access_width=width,
                    within_startup_clear=clear_start <= target and target+width <= clear_end,
                    mapped_names=[],sites=[]))
            names = [name for name,value in mapping.get('symbols',{}).items()
                     if int(value['address'] if isinstance(value,dict) else value,0) == target]
            cell['mapped_names'] = sorted(set(cell['mapped_names'])|set(names))
            cell['sites'].append(dict(unit=check['unit'],function=check['function'],
                         instruction_address=hex(address+offset),instruction=instruction,
                         operation=operation,displacement=displacement))
    intervals = sorted((a,a+w) for a,w in cells if clear_start <= a and a+w <= clear_end)
    union = []
    for a,b in intervals:
        if union and a <= union[-1][1]:
            union[-1][1] = max(union[-1][1],b)
        else:
            union.append([a,b])
    return dict(status='pass',reference_sha256=expected_hash,startup_gp=hex(gp),
                analyzer_sha256=digest(__file__),minimum_access_cells=list(cells.values()),
                cell_count=len(cells),access_site_count=sum(len(c['sites']) for c in cells.values()),
                clear_interval_minimum_access_union_bytes=sum(b-a for a,b in union),
                clear_interval_minimum_access_union=[dict(start=hex(a),end_exclusive=hex(b),size=b-a) for a,b in union],
                certified_payload_bytes_added=0,
                limitations=['Assumes the startup GP is preserved at these recovered function entries.',
                             'An access width is a lower bound, not a complete object extent.',
                             'No .sbss/.bss/COMMON/padding allocation or section ordering is established.',
                             'Non-GP accesses, address constructions and all unowned code are excluded.'])

def main():
    latest_path = PROJECT / 'reports/latest.json'
    latest = json.loads(latest_path.read_text())
    raw = Path(latest['reference']).read_bytes()
    lock = json.loads((PROJECT / 'config/toolchain.lock.json').read_text())
    result = build_report(latest,raw,lock['reference_sha256'])
    result['latest_report_sha256'] = digest(latest_path)
    rejections = []
    for case in ('stale_source','changed_extent','wrong_reference_hash'):
        changed = copy.deepcopy(latest)
        expected_hash = lock['reference_sha256']
        if case == 'stale_source':
            changed['checks'][0]['source_sha256'] = '0'*64
        elif case == 'changed_extent':
            changed['checks'][0]['size'] += 4
        else:
            expected_hash = '0'*64
        try:
            build_report(changed,raw,expected_hash)
        except ValueError as exc:
            rejections.append(dict(case=case,status='rejected',reason=str(exc)))
        else:
            raise AssertionError(f'changed evidence accepted: {case}')
    result['negative_checks'] = rejections
    (PROJECT / 'reports/gp_access_layout.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'pass: {result["cell_count"]} minimum GP access cells / {result["access_site_count"]} sites; zero coverage credit')

if __name__ == '__main__':
    main()
