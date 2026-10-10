#!/usr/bin/env python3
"""Emit ROM-checked minigame closure storage for a reviewed audit manifest.

Keep original addresses contiguous within each imported interval. C++ RTTI and
vtable references bind to compiler-emitted objects; retail vtables name their
first callable slot, eight bytes past the native GCC prefix.
"""
import argparse
import json
from pathlib import Path


def generate(root, audit, defined, *, metadata_types=()):
    records = audit['symbols']
    # Explicit reviewed ARM32 class/single-base metadata only. Keep complete
    # physical TI/TS intervals (a name interval can also own a vtable prefix).
    reviewed_metadata = set()
    metadata_vptr_targets = {}
    defined = set(defined)
    if metadata_types:
        from retail_rtti_bridge import generate as prove_metadata, KINDS
        _, proof = prove_metadata(root, audit, metadata_types)
        reviewed_metadata = set(proof['defined_symbols'])
        metadata_vptr_targets = {k: v[1] + ' + 8' for k, v in KINDS.items()}
        defined.difference_update(reviewed_metadata)
        defined.update(metadata_vptr_targets)
    if audit['unresolved']:
        raise ValueError('unresolved audit symbols')
    bases = {'main': 0x02004000, '1': 0x020aa420, '4': 0x020ad660, '6': 0x020bfec0}
    native = {(v['module'], v['address']): n for n, v in records.items()
              if n.startswith(('_ZTI', '_ZTV', '_ZTS')) and n in defined}
    reachable = set()
    pending = list(audit['roots'])
    while pending:
        name = pending.pop()
        if name in reachable or name in defined:
            continue
        reachable.add(name)
        entry = records[name]
        if (entry['module'], entry['address']) in native:
            continue
        pending.extend(r['target'] for r in entry.get('relocations', []))
    def target(name):
        if name in metadata_vptr_targets:
            return metadata_vptr_targets[name]
        entry = records[name]
        alias = native.get((entry['module'], entry['address']))
        if alias:
            return alias + (' + 8' if alias.startswith('_ZTV') else '')
        return name
    emitted = {}
    lines = ['// Generated from reviewed minigame ROM data closure.', 'asm(R"MGDATA(']
    for name, entry in records.items():
        if name not in reachable or name in defined or entry['kind'].startswith('function'):
            continue
        alias = native.get((entry['module'], entry['address']))
        if alias:
            lines += [f'.global {name}', f'.set {name}, {target(name)}']
            continue
        if name.startswith(('_ZTI', '_ZTV', '_ZTS')) and name not in reviewed_metadata:
            raise ValueError(f'Native C++ object missing: {name}')
        emitted.setdefault(entry['module'], []).append((int(entry['address'], 16), name, entry))
    for module, items in emitted.items():
        items.sort()
        previous_end = None
        previous_bss = None
        for index, (start, name, entry) in enumerate(items):
            end = int(entry['end'], 16)
            bss = entry['kind'] == 'bss'
            sec = 'bss' if bss else 'data'
            contiguous = start == previous_end and bss == previous_bss
            if not contiguous:
                if previous_end is not None:
                    lines.append('.popsection')
                lines += [f'.pushsection .{sec}.mg_{module}_{start:x},\"aw\",%'+('nobits' if bss else 'progbits'), '.balign 4']
            lines += [f'.global {name}', f'{name}:']
            if bss:
                lines.append(f'.zero {end-start}')
            else:
                binary = root/('extracted/arm9_dec.bin' if module == 'main'
                               else f'extracted/overlays/overlay_{int(module):04}.bin')
                cursor = start
                for reloc in sorted(entry['relocations'], key=lambda r: int(r['at'], 16)):
                    at = int(reloc['at'], 16)
                    if at > cursor:
                        lines.append(f'.incbin "{binary.resolve()}", {cursor-bases[module]}, {at-cursor}')
                    lines.append('.word '+target(reloc['target']))
                    cursor = at+4
                if cursor < end:
                    lines.append(f'.incbin "{binary.resolve()}", {cursor-bases[module]}, {end-cursor}')
            previous_end, previous_bss = end, bss
        lines.append('.popsection')
    lines += [')MGDATA");']
    lines += ['#include <cstring>', '#include <cstdint>', 'extern "C" {']
    spans = []
    for module, items in emitted.items():
        if module not in ('4', '6'):
            continue
        for start, name, entry in items:
            lines.append(f'extern unsigned char {name}[];')
            spans.append((module, name, entry['bytes'], entry['kind']=='bss'))
    lines += ['}', 'namespace {',
              'struct Span {unsigned char* bytes;unsigned char* initial;unsigned size;unsigned overlay;};']
    for i, (_, _, size, bss) in enumerate(spans):
        if not bss:
            lines.append(f'unsigned char initial_{i}[{size}];')
    lines += ['Span spans[]={']
    for i, (module, name, size, bss) in enumerate(spans):
        lines.append(f'{{{name},'+('nullptr' if bss else f'initial_{i}')+f',{size},{module}}},')
    lines += ['};bool captured4=false,captured6=false;}',
              'extern "C" bool sm64ds_reset_minigame_storage(unsigned overlay){',
              ' if(overlay!=4 && overlay!=6)return false;',
              ' bool& captured=overlay==4?captured4:captured6;',
              ' for(auto& s:spans)if(s.overlay==overlay){',
              '  if(!s.initial)std::memset(s.bytes,0,s.size);',
              '  else {if(!captured)std::memcpy(s.initial,s.bytes,s.size);std::memcpy(s.bytes,s.initial,s.size);}',
              ' }captured=true;return true;', '}',
              'extern "C" int sm64ds_minigame_storage_owner(const void* object){',
              ' const auto p=reinterpret_cast<std::uintptr_t>(object);',
              ' for(const auto& s:spans){const auto b=reinterpret_cast<std::uintptr_t>(s.bytes);if(p>=b && p-b<s.size)return s.overlay;}',
              ' return -1;', '}']
    return '\n'.join(lines)+'\n'


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('audit', type=Path)
    p.add_argument('defined', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--metadata-types', type=Path)
    a = p.parse_args()
    a.output.write_text(generate(a.root, json.loads(a.audit.read_text()), set(a.defined.read_text().splitlines()), metadata_types=a.metadata_types.read_text().splitlines() if a.metadata_types else ()))
