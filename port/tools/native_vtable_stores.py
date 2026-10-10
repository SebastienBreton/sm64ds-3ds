#!/usr/bin/env python3
"""Adapt explicit ARM9 primary-vptr stores in private source copies.

Only the reviewed p[0] integer-array store shape is accepted. A linked native
ELF must prove each target's two-word GCC prefix before the transformation.
Reviewed dereferenced integer and void-pointer stores are also accepted.
Unknown layouts reject the transformation; explicitly proven manual tables are
left at their existing address point.
"""
import re
import json
from pathlib import Path

STORE = re.compile(r'(\b\w+\s*\[\s*0\s*\]\s*=\s*\(\s*(?:int|u32|s32)\s*\)\s*)'
                   r'(_ZTV\w+)\b(?!\s*(?:\+|\[))')

POINTER_INT = re.compile(r'(\*\s*\(\s*(?:int|u32|s32)\s*\*\s*\)\s*\w+\s*=\s*\(\s*(?:int|u32|s32)\s*\)\s*)'
                         r'(_ZTV\w+)\b(?!\s*(?:\+|\[))')
POINTER_VOID = re.compile(r'(\*\s*\(\s*void\s*\*\s*\*\s*\*\s*\)\s*\w+\s*=\s*)'
                          r'(?:\(\s*void\s*\*\s*\*\s*\)\s*)?'
                          r'(_ZTV\w+)\b(?!\s*(?:\+|\[))')


def adapt(text, proven_tables, manual_tables=()):
    records = []

    def replace(match):
        name = match[2]
        if name in manual_tables:
            return match[0]
        if name not in proven_tables:
            raise ValueError('Unproved native vtable prefix: ' + name)
        records.append({'symbol': name, 'address_point_offset': 8,
                        'original_store': match[0]})
        return match[1] + '((char*)' + name + ' + 8)'

    text = STORE.sub(replace, text)
    text = POINTER_INT.sub(replace, text)
    def replace_void(match):
        adapted = replace(match)
        if adapted == match[0]:
            return adapted
        return match[1] + '(void**)((char*)' + match[2] + ' + 8)'
    return POINTER_VOID.sub(replace_void, text), records


def reviewed_tables():
    """ARM32 layouts proved by linked ELF and the natural Bob V5 replay."""
    manifest = json.loads(Path(__file__).with_name('native_vtable_layouts.json').read_text())
    assert manifest['pointer_bytes'] == 4
    assert all(row['primary_address_point_offset'] == 8
               for row in manifest['gcc_primary_tables'].values())
    return set(manifest['gcc_primary_tables'])


def adapt_reviewed(text):
    """Only adapt proved tables; leave unknown layouts for the linked audit.

    Unknown symbols are not classified as manual tables or silently offset.
    The stricter adapt() remains available to private batches requiring closure.
    """
    proved = reviewed_tables()
    records = []
    def replace(match):
        if match[2] not in proved:
            return match[0]
        changed, rows = adapt(match[0], proved)
        records.extend(rows)
        return changed
    for pattern in (STORE, POINTER_INT, POINTER_VOID):
        text = pattern.sub(replace, text)
    return text, records
