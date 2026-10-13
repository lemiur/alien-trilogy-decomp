"""Audit every compiled text definition, beyond certified map entries."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import bytecheck
import verify
import luna_until_stall as sessions
P = Path(__file__).resolve().parents[1]
def main():
    latest = sessions.read(P / 'reports/latest.json')
    exported = {}
    unmapped = []
    data_exported = {}
    unmapped_data = []
    manifest = sessions.read(P / 'config/data.json')
    for group in latest['data_checks']:
        mapping = sessions.read(group['symbol_map'])
        verify.validate_object_text_exports(group['object'], mapping['functions'])
        obj = bytecheck.psyqobj.load(group['object'])[0]
        for name, section, offset in obj.xdefs:
            section_name = obj.sections[section]
            if section_name in ('.data', '.sdata', '.rdata'):
                data_exported.setdefault(name, []).append(dict(unit=group['unit'], object=group['object'], offset=offset))
                if name not in manifest.get(group['unit'], {}):
                    unmapped_data.append(dict(symbol=name, unit=group['unit'], object=group['object']))
            if section_name != '.text':
                continue
            exported.setdefault(name, []).append(dict(unit=group['unit'], object=group['object'], offset=offset))
            if name not in mapping['functions']:
                unmapped.append(dict(function=name, unit=group['unit'], object=group['object']))
    duplicates = {n: rows for n, rows in exported.items() if len(rows) > 1}
    data_duplicates = {n: rows for n, rows in data_exported.items() if len(rows) > 1}
    report = dict(status='pass' if not duplicates and not unmapped and not data_duplicates and not unmapped_data else 'requires_correction',
                  latest_report_sha256=sessions.sha(P / 'reports/latest.json'), compiled_text_definition_count=sum(map(len, exported.values())),
                  duplicate_text_definitions=duplicates, unmapped_text_definitions=unmapped,
                  complete_object_text_partition_verified=True,
                  duplicate_initialized_exports=data_duplicates, unmapped_initialized_exports=unmapped_data,
                  compiled_named_initialized_definition_count=sum(map(len, data_exported.values())))
    sessions.write(P / 'reports/object_export_audit.json', report)
    print(report)
if __name__ == '__main__':
    main()
