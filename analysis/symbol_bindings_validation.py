"""Exercise conflicting global names without modifying registered evidence."""
import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('verify', PROJECT / 'tools/verify.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)

def main():
    verify.validate_symbol_bindings(verify.UNITS)
    cases = []
    with tempfile.TemporaryDirectory(prefix='alien-symbol-bindings-') as temp:
        root = Path(temp)
        source = root / 'source.c'
        source.write_text('extern int shared;\n')
        first, second = root / 'first.json', root / 'second.json'
        for kind in ('import_import', 'definition_import', 'definition_definition', 'same_map'):
            a = {'functions': {}, 'symbols': {}}
            b = {'functions': {}, 'symbols': {}}
            target_a = 'functions' if kind in ('definition_import', 'definition_definition', 'same_map') else 'symbols'
            target_b = 'functions' if kind == 'definition_definition' else 'symbols'
            a[target_a]['shared'] = {'address': '0x80010000', 'size': 8} if target_a == 'functions' else '0x80010000'
            (a if kind == 'same_map' else b)[target_b]['shared'] = {'address': '0x80010008', 'size': 8} if target_b == 'functions' else '0x80010008'
            first.write_text(json.dumps(a))
            second.write_text(json.dumps(b))
            try:
                verify.validate_symbol_bindings([('first', source, first), ('second', source, second)])
            except RuntimeError as exc:
                cases.append(dict(case=kind, status='rejected', reason=str(exc)))
            else:
                raise AssertionError(f'conflict was accepted: {kind}')
        b = {'functions': {}, 'symbols': {'shared': '0x80010000', 'alias': '0x80010000'}}
        a = {'functions': {'shared': {'address': '0x80010000', 'size': 8}}, 'symbols': {}}
        first.write_text(json.dumps(a))
        second.write_text(json.dumps(b))
        verify.validate_symbol_bindings([('first', source, first), ('second', source, second)])
    report = dict(status='pass', verifier_sha256=hashlib.sha256((PROJECT / 'tools/verify.py').read_bytes()).hexdigest(),
                  registered_units=len(verify.UNITS), consistent_import_and_alias_accepted=True,
                  cases=cases, all_conflicts_rejected=True)
    (PROJECT / 'reports/symbol_bindings_validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'pass: {len(cases)} global-name conflict rejection checks; consistent aliases accepted')

if __name__ == '__main__':
    main()
