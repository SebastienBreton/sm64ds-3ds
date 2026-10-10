#!/usr/bin/env python3
"""Guard ARM32 primary-store scope, explicit proof and existing address points."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_vtable_stores import adapt, adapt_reviewed, reviewed_tables

def main():
    tables = reviewed_tables()
    assert len(tables) == 11
    checked = 0
    for table in sorted(tables):
        for store in (f'p[0] = (int){table};', f'*(int *)self = (int){table};',
                      f'*(void ***)self = {table};', f'*(void ***)self = (void**){table};'):
            changed, rows = adapt_reviewed(store)
            assert len(rows) == 1 and rows[0]['address_point_offset'] == 8
            assert ' + 8)' in changed
            assert adapt_reviewed(changed) == (changed, [])
            checked += 1
        for offset in (' + 2', '[2]'):
            text = f'p[0] = (int){table}{offset};'
            assert adapt_reviewed(text) == (text, [])
            checked += 1
    unknown = 'p[0] = (int)_ZTV10NotReviewed;'
    assert adapt_reviewed(unknown) == (unknown, [])
    assert adapt(unknown, tables, {'_ZTV10NotReviewed'}) == (unknown, [])
    try:
        adapt(unknown, tables)
    except ValueError:
        pass
    else:
        raise AssertionError('Strict closure accepted unproved layout')
    print(f'PASS {checked} reviewed stores/offsets; idempotence; unknown preserved/strict rejection')

if __name__ == '__main__':
    main()
