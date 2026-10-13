"""Reproduce rejection of the actual formerly hidden duplicate definition."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import luna_until_stall as sessions
P = Path(__file__).resolve().parents[1]
def main():
    spec = importlib.util.spec_from_file_location('exports_verify', P / 'tools/verify.py')
    verify = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verify)
    archived = sessions.read(P / 'reports/session10_full_verify.json')
    bad = next(r for r in archived['data_checks'] if r['unit'] == 'text_helpers5')
    clean_build = sessions.read(P / 'analysis/recovery12_03/session11/build_text_helpers5_owner_review.json')
    mapping = sessions.read(P / 'config/text_helpers5.json')['functions']
    clean_object = Path(clean_build['object'])
    verify.validate_object_text_exports(clean_object, mapping)
    cases = []
    for label, object_path, expected in (
        ('actual_archived_unmapped_duplicate', Path(bad['object']), mapping),
        ('missing_expected_definition', clean_object, dict(mapping, absent_function=dict(address='0x80010000', size=8))),
        ('unregistered_real_definition', clean_object, {n:d for n,d in mapping.items() if n != 'f_1fc0c'}),
    ):
        try:
            verify.validate_object_text_exports(object_path, expected)
        except RuntimeError as error:
            cases.append(dict(case=label, rejected=True, reason=str(error)))
        else:
            raise AssertionError(f'Unsafe export set accepted: {label}')
    private_prefix = SimpleNamespace(sections={1: '.text'}, xdefs=[('function', 1, 4)], code={1: bytes(12)})
    try:
        verify.validate_object_text_partition(private_prefix, {'function': {'size': 8}})
    except RuntimeError as error:
        cases.append(dict(case='unexported_text_prefix', rejected=True, reason=str(error)))
    else:
        raise AssertionError('Unowned private helper prefix accepted')
    sessions.write(P / 'reports/object_export_validation.json', dict(status='pass', clean_object=str(clean_object),
        clean_object_sha256=sessions.sha(clean_object), negative_checks=cases, archived_object_sha256=sessions.sha(bad['object'])))
    print('pass: complete compiled text and four rejection cases, including archived duplicate and unexported prefix')
if __name__ == '__main__':
    main()
