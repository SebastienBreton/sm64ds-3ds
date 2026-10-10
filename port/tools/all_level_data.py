#!/usr/bin/env python3
"""Relocate reachable numeric level-data islands, excluding code and actor vtables.

A level's original overlay ID determines pointer identity. Adjacent data symbols
are kept together so arrays with interior labels retain their layout. Any edge
from a selected island into code, external modules or unclassified data fails.
Castle uses the existing castle_level_data import, preserving shared identity.
"""
import argparse
import json
from pathlib import Path
import re
import struct

BASE = 0x021111a0

def collect(root):
    main = (root/'extracted/arm9_dec.bin').read_bytes()
    overlays = struct.unpack_from('<52I', main, 0x020758c8-0x02004000)
    roots = struct.unpack_from('<52I', main, 0x02092208-0x02004000)
    if overlays != tuple(range(8,60)):
        raise ValueError('unexpected level-overlay mapping')
    result = []
    for index, (ov, address) in enumerate(zip(overlays, roots)):
        if ov == 9:
            if address != 0x02112bdc: raise ValueError('castle root changed')
            result.append((ov,address,None,[],{})); continue
        cfg = root/f'config/arm9/overlays/ov{ov:03}'
        blob = (root/f'extracted/overlays/overlay_{ov:04}.bin').read_bytes()
        symbols = {}
        functions = []
        for line in (cfg/'symbols.txt').read_text().splitlines():
            m = re.fullmatch(r'(\S+) kind:(\S+) addr:0x([0-9a-f]+)(?: ambiguous)?',line)
            if not m: raise ValueError('unparsed symbol: '+line)
            at = int(m[3],16)
            symbols.setdefault(at,[]).append(m[2])
            size = re.search(r'function\([^,]+,size=0x([0-9a-f]+)\)',m[2])
            if size: functions.append((at,at+int(size[1],16)))
        relocations = {}
        for line in (cfg/'relocs.txt').read_text().splitlines():
            m = re.fullmatch(r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)',line)
            if not m: raise ValueError('unparsed relocation: '+line)
            at,target=int(m[1],16),int(m[3],16)
            relocations[at]=(target,m[2],m[4])
        boundaries = sorted(set(symbols)|{BASE+len(blob)})
        allowed=[]
        for start,end in zip(boundaries,boundaries[1:]):
            if not BASE <= start < end <= BASE+len(blob): continue
            if not all(k.startswith('data') for k in symbols.get(start,[])): continue
            if any(a<end and start<b for a,b in functions): continue
            edges=[(at,edge) for at,edge in relocations.items() if start<=at<end]
            if any(kind!='load' or mod!=f'overlay({ov})' or
                   target not in symbols or not all(k.startswith('data') for k in symbols[target])
                   for at,(target,kind,mod) in edges): continue
            if allowed and allowed[-1][1]==start: allowed[-1]=(allowed[-1][0],end)
            else: allowed.append((start,end))
        def containing(at):
            matches=[i for i,(a,b) in enumerate(allowed) if a<=at<b]
            if len(matches)!=1: raise ValueError(f'ov{ov}: unclassified data edge {at:08x}')
            return matches[0]
        pending=[containing(address)]
        chosen=set()
        edges={}
        while pending:
            i=pending.pop()
            if i in chosen: continue
            chosen.add(i)
            start,end=allowed[i]
            for at,(target,kind,mod) in relocations.items():
                if not start<=at<end: continue
                if at%4 or at+4>end: raise ValueError('split or unaligned pointer')
                if struct.unpack_from('<I',blob,at-BASE)[0]!=target: raise ValueError('ROM pointer mismatch')
                edges[at]=target
                pending.append(containing(target))
        spans=[allowed[i] for i in sorted(chosen)]
        if not any(a<=address and address+28<=b for a,b in spans):
            raise ValueError(f'truncated level header {ov}')
        result.append((ov,address,blob,spans,edges))
    return result

def reset_records(root, levels):
    """Build pristine numeric snapshots whose pointers target live native data."""
    from castle_level_data import SPANS as CASTLE_SPANS
    records=[]
    for ov,address,blob,spans,edges in levels:
        if blob is None:
            blob=(root/'extracted/overlays/overlay_0009.bin').read_bytes()
            spans=CASTLE_SPANS
            names={}
            for line in (root/'config/arm9/overlays/ov009/symbols.txt').read_text().splitlines():
                m=re.fullmatch(r'(\w+) kind:data\S* addr:0x([0-9a-f]+)(?: ambiguous)?',line)
                if m: names[int(m[2],16)]=m[1]
            edges={}
            for line in (root/'config/arm9/overlays/ov009/relocs.txt').read_text().splitlines():
                m=re.fullmatch(r'from:0x([0-9a-f]+) kind:load to:0x([0-9a-f]+) module:overlay\(9\)',line)
                if m and any(a<=int(m[1],16)<b for a,b in spans):
                    at,target=int(m[1],16),int(m[2],16)
                    assert any(a<=target<b for a,b in spans)
                    assert struct.unpack_from('<I',blob,at-BASE)[0]==target
                    edges[at]=target
            target_name=lambda at:names[at]
        else:
            def target_name(at):
                base=next(a for a,b in spans if a<=at<b)
                return f'level_{ov}_{base:08x}+{at-base}'
        for start,end in spans:
            targets={at:target_name(target) for at,target in edges.items() if start<=at<end}
            records.append((ov,start,end,blob,target_name(start),targets))
    return records


def generate(root):
    levels=collect(root)
    aliases=json.loads(Path(__file__).with_name('native_actor_overlay_manifest.json').read_text())['level_aliases']
    emitted_aliases=set()
    lines=['// Numeric data only, generated from the user ROM.', 'asm(R"LEVELS(']
    for ov,address,blob,spans,edges in levels:
        if blob is None: continue
        for start,end in spans:
            lines += [f'.pushsection .data.level_{ov}_{start:08x},"aw",%progbits','.balign 4',
                      f'level_{ov}_{start:08x}:']
            if ov==10 and start<=0x021122f8<end:
                # The trap actor and stage data share the same retail CLPS block.
                lines += ['.global data_ov010_021122f8', f'.set data_ov010_021122f8,level_10_{start:08x}+{0x021122f8-start}']
            for name,(owner,at) in aliases.items():
                if owner==ov and start<=at<end:
                    lines += [f'.global {name}', f'.set {name},level_{ov}_{start:08x}+{at-start}']
                    emitted_aliases.add(name)
            cursor=start
            while cursor<end:
                if cursor in edges:
                    target=edges[cursor]
                    base=next(a for a,b in spans if a<=target<b)
                    lines.append(f'.word level_{ov}_{base:08x}+{target-base}')
                    cursor+=4
                else:
                    stop=min(cursor+16,end)
                    stop=min([stop]+[a for a in edges if cursor<a<stop])
                    lines.append('.byte '+','.join(str(v) for v in blob[cursor-BASE:stop-BASE]))
                    cursor=stop
            lines.append('.popsection')
    if emitted_aliases!=set(aliases):raise ValueError('shared actor data alias outside native level islands')
    lines += ['.pushsection .data.level_roots,"aw",%progbits','.balign 4',
              '.global data_02092208','.type data_02092208, %object','data_02092208:']
    for ov,address,blob,spans,edges in levels:
        if blob is None: lines.append('.word data_ov009_02112bdc')
        else:
            base=next(a for a,b in spans if a<=address<b)
            lines.append(f'.word level_{ov}_{base:08x}+{address-base}')
    lines += ['.size data_02092208, .-data_02092208','.popsection']
    records=reset_records(root,levels)
    for ov,start,end,blob,destination,targets in records:
        lines += [f'.pushsection .rodata.level_initial_{ov}_{start:08x},"a",%progbits',
                  '.balign 4',f'level_initial_{ov}_{start:08x}:']
        cursor=start
        while cursor<end:
            if cursor in targets:
                lines.append('.word '+targets[cursor]);cursor+=4
            else:
                stop=min([min(cursor+16,end)]+[at for at in targets if cursor<at<min(cursor+16,end)])
                lines.append('.byte '+','.join(str(v) for v in blob[cursor-BASE:stop-BASE]));cursor=stop
        lines.append('.popsection')
    lines += ['.pushsection .rodata.level_reset_ranges,"a",%progbits','.balign 4',
              '.global sm64ds_level_reset_ranges','sm64ds_level_reset_ranges:']
    for ov,start,end,blob,destination,targets in records:
        lines += [f'.word {destination}',f'.word level_initial_{ov}_{start:08x}',
                  f'.word {end-start}',f'.word {ov}']
    lines += ['.global sm64ds_level_reset_ranges_end','sm64ds_level_reset_ranges_end:', '.popsection',
              '.pushsection .rodata.level_address_ranges,"a",%progbits','.balign 4',
              '.global sm64ds_level_address_ranges','sm64ds_level_address_ranges:']
    for ov,start,end,blob,destination,targets in records:
        lines += [f'.word {ov},{start},{end-start},{destination}']
    lines += ['.word 9,0x02113c20,704,sm64ds_castle_bss_begin',
              '.word 10,0x02112d00,128,sm64ds_overlay10_bss_begin',
              '.global sm64ds_level_address_ranges_end','sm64ds_level_address_ranges_end:',
              '.popsection',')LEVELS");','']
    return '\n'.join(lines)

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root',type=Path);ap.add_argument('output',type=Path)
    args=ap.parse_args();args.output.write_text(generate(args.root))
