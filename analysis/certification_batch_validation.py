"""Independently audit supplemental SDK matches and verifier ownership checks."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import copy

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parent


def module_at(path, name):
    return next(obj for obj in runtime.psyqobj.load(str(path))
                if not isinstance(obj, Exception) and obj.name == name)


def verify_small_placement(record, obj, payload, load):
    section = next(sid for sid, name in obj.sections.items() if name == '.text')
    code = bytes(obj.code[section])
    if record['size'] != len(code) or not 0 < len(code) < 48:
        raise ValueError('small module extent differs from complete archive text')
    mask = bytearray(b'\x01' * len(code))
    for _, offset, _ in obj.patches.get(section, []):
        if offset % 4 or offset < 0 or offset + 4 > len(code):
            raise ValueError('small module relocation is unaligned or outside text')
        mask[offset:offset + 4] = bytes(4)
    a, n = record['anchor_offset'], record['anchor_size']
    if a < 0 or n < 8 or a + n > len(code) or not all(mask[a:a+n]):
        raise ValueError('small module anchor includes relocation bytes or invalid extent')
    hits = [offset for offset in range(0, len(payload) - len(code) + 1, 4)
            if all(not mask[i] or payload[offset+i] == code[i] for i in range(len(code)))]
    if hits != [int(record['address'], 0) - load]:
        raise ValueError('small module placement is not the unique aligned masked hit')


spec = importlib.util.spec_from_file_location('runtime', PROJECT / 'analysis/runtime_relocations.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
spec = importlib.util.spec_from_file_location('verify', PROJECT / 'tools/verify.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)
verify.validate_function_ownership(verify.UNITS)
with tempfile.TemporaryDirectory(prefix='alien-ownership-') as temp:
    directory = Path(temp)
    source = directory / 'unit.c'
    source.write_text('void duplicate(void) {}\n')
    mapping = directory / 'unit.json'
    mapping.write_text(json.dumps({'functions': {'duplicate': {'address': '0x80010000', 'size': 8}}}))
    try:
        verify.validate_function_ownership([('first', source, mapping), ('second', source, mapping)])
    except RuntimeError as exc:
        assert 'registered in both first and second' in str(exc)
    else:
        raise AssertionError('duplicate function ownership was accepted')

symbols = json.loads((PROJECT / 'analysis/runtime_symbols.json').read_text())
linked = json.loads((PROJECT / 'analysis/runtime_linked.json').read_text())
raw = (ROOT / 'games/Alien Trilogy (PS1 USA)/extracted/SLUS_000.07').read_bytes()
load, size = struct.unpack_from('<II', raw, 0x18)
payload = raw[0x800:0x800 + size]
assert hashlib.sha256(raw).hexdigest() == linked['executable_sha256']
supplemental = [x for x in symbols['modules'] if x.get('discovery')]
audited = []
small_relocated_audited = []
small_rejections = []
for record in supplemental:
    library = ROOT / record['library']
    obj = module_at(library, record['module'])
    section = next(sid for sid, name in obj.sections.items() if name == '.text')
    code = bytes(obj.code[section])
    start = int(record['address'], 0) - load
    expected = payload[start:start + len(code)]
    result = next(x for x in linked['module_results']
                  if x['module'] == record['module'] and x['library'] == record['library']
                  and x['address'] == record['address'])
    assert result['status'] == 'full_match'
    assert result['mismatch_count'] == 0 and not result['unresolved_relocations']
    lnk = next(data for name, data in runtime.library_modules(library) if name == obj.name)
    _, imports = runtime.symbol_ids(lnk)
    assert result['imported_symbols'] == sorted(set(imports.values()))
    if record.get('discovery') == 'supplemental_unique_small_relocation_masked_text_scan':
        verify_small_placement(record, obj, payload, load)
        small_relocated_audited.append(obj.name)
        for label, mutate in (
            ('wrong_address', lambda r: r.__setitem__('address', hex(int(r['address'], 0) + 4))),
            ('truncated_extent', lambda r: r.__setitem__('size', r['size'] - 4)),
            ('relocated_anchor', lambda r: r.__setitem__('anchor_offset', obj.patches[section][0][1])),
        ):
            changed = copy.deepcopy(record)
            mutate(changed)
            try:
                verify_small_placement(changed, obj, payload, load)
            except ValueError as exc:
                small_rejections.append({'module': obj.name, 'case': label, 'status': 'rejected', 'reason': str(exc)})
            else:
                raise AssertionError(f'invalid small module evidence accepted: {obj.name}:{label}')
    if record['byte_exact_object_text']:
        assert not obj.patches.get(section, [])
        assert code == expected
        hits = [off for off in range(0, len(payload) - len(code) + 1, 4)
                if payload[off:off + len(code)] == code]
        assert hits == [start]
    # Replay every recorded patch independently, without masking any bytes.
    actual = bytearray(code)
    assert len(result['applied_relocations']) == len(obj.patches.get(section, []))
    for patch in result['applied_relocations']:
        offset, typ, value = patch['offset'], patch['type'], patch['value']
        word = struct.unpack_from('<I', actual, offset)[0]
        if typ in (82, 84, 30):
            word = (word & 0xffff0000) | (value & 0xffff)
        elif typ == 74:
            word = (word & 0xfc000000) | ((value >> 2) & 0x03ffffff)
        elif typ == 16:
            word = value
        else:
            raise AssertionError(f'unsupported replay patch {typ}')
        struct.pack_into('<I', actual, offset, word)
    assert actual == expected
    audited.append({'module': obj.name, 'address': record['address'], 'size': len(code),
                    'relocation_count': len(obj.patches.get(section, [])),
                    'reconstructed_sha256': hashlib.sha256(actual).hexdigest()})

ranges = sorted({(int(x['address'], 0), int(x['address'], 0) + x['text_size'])
                 for x in linked['module_results'] if x['status'] == 'full_match'})
assert all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:]))
report = {'status': 'pass', 'reference_sha256': hashlib.sha256(raw).hexdigest(),
          'duplicate_function_ownership_rejected': True,
          'registered_function_ownership_unique': True,
          'supplemental_sdk_modules_independently_replayed': len(audited),
          'supplemental_unrelocated_matches_unique_and_aligned': True,
          'small_relocated_modules_unique_placements_replayed': small_relocated_audited,
          'small_relocated_negative_checks': small_rejections,
          'sdk_certified_ranges_disjoint': True, 'checks': audited}
output = PROJECT / 'reports/certification_batch_validation.json'
output.write_text(json.dumps(report, indent=2) + '\n')
print(f"pass: {len(audited)} supplemental SDK modules replayed; duplicate ownership rejected")
