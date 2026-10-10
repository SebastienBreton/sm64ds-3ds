#!/usr/bin/env python3
"""Import reviewed initialized Stage/font tables with explicit relocations.

Tables are writable because original initialization mutates several globals.
Any new relocation in these ranges is an error requiring explicit review.
"""
from pathlib import Path
import argparse
import re
import struct
POINTERS = {
    0x02099fb4:0x0204c24c, 0x02099fb8:0x0204c194,
    0x02099fbc:0x0204c0a8, 0x02099fc0:0x0204c0e8,
    0x0208ee14:0x02018eb8, 0x0208ee18:0x02018eb0,
    0x0208ee1c:0x02018ea8, 0x0208ee20:0x02018ea0,
    0x02092188:0x02018eb8, 0x0209218c:0x02029838,
    0x02092190:0x020297f4, 0x02092194:0x02018ea0,
    0x0209267c: 0x020914d4,
    0x020927a0: 0x02092788,
    0x020927a4: 0x02092790,
    0x020927a8: 0x02092798,
    0x020927ac: 0x02092780,
    0x02092830: 0x0203128c,
    0x02092834: 0x02031154,
    0x02092838: 0x020313d8,
}
RESOURCE_TARGETS = [0x0210da48, 0x0210d9b8, 0x0210da50, 0x0210d9f8,
                    0x0210da40, 0x0210d9a0, 0x0210d9c0, 0x0210e7d8,
                    0x0210e3a0, 0x0211094c, 0x0211095c, 0x0210d9a8]
RESOURCE_POINTERS = {0x020756f0+4*i: address for i,address in enumerate(RESOURCE_TARGETS)}
SPANS = [
    # WDW water heights read by dActor_c::GetWaterHeightWDW (levels-render).
    (0x02075244, 0x02075250),
    (0x02099fb4,0x02099fc4),
    (0x0208ee14,0x0208ee24), (0x02092188,0x02092198),
    (0x02086484, 0x02086758),
    (0x020926c8, 0x020926d0),
    (0x020756f0, 0x02075720),
    (0x02075b14, 0x02075f14),
    (0x02092668, 0x02092680),
    (0x02092780, 0x020927b0),
    (0x02092830, 0x0209283c),
    (0x02075258, 0x02075264),
    (0x020755b8, 0x020755bc),
    (0x020755c0, 0x02075638),
    (0x020756d0, 0x020756f0),
    (0x02075720, 0x02075b04),
    # Embedded SPA archive. 02078000 is a symbol inside its payload, not
    # the end: the header's texture offset+size reaches IDENTITY_MATRIX4X3.
    (0x02075f14, 0x02082128),
    (0x02082200, 0x02082214),
    (0x0208a0e0, 0x0208a0e4),
    (0x0208a178, 0x0208c378),
    (0x0208ee00, 0x0208ee04),
    (0x0208ee3c, 0x0208ee44),
    (0x0208f668, 0x0208f670),
    (0x02092660, 0x02092664),
    (0x02092778, 0x0209277c),
    (0x02092808, 0x02092830),
    (0x0209283c, 0x0209435c),
    (0x02099fcc, 0x02099fd0),
]

def generate(root):
    cfg=root/'config/arm9'
    blob=(root/'extracted/arm9_dec.bin').read_bytes()
    spa = blob[0x02075f14-0x02004000:0x02082128-0x02004000]
    if spa[:8] != b' APS10_1':
        raise ValueError('unexpected embedded particle archive signature')
    texture_size, texture_offset = struct.unpack_from('<II', spa, 0x14)
    if texture_offset + texture_size != len(spa):
        raise ValueError('embedded particle archive span does not cover its payload')
    cursor = texture_offset
    for _ in range(struct.unpack_from('<H', spa, 0xa)[0]):
        if cursor + 0x20 > len(spa):
            raise ValueError('truncated particle texture header')
        record_size = struct.unpack_from('<I', spa, cursor + 0x1c)[0]
        if record_size < 0x20 or cursor + record_size > len(spa):
            raise ValueError('truncated particle texture payload')
        cursor += record_size
    if cursor != len(spa):
        raise ValueError('particle texture records do not cover archive')
    inside=lambda a:any(start<=a<end for start,end in SPANS)
    names={}
    targets={}
    for line in (cfg/'symbols.txt').read_text().splitlines():
        m=re.fullmatch(r'(\w+) kind:(\S+) addr:0x([0-9a-f]+)(?: ambiguous)?',line)
        if not m:continue
        targets[int(m[3],16)]=m[1]
        if not inside(int(m[3],16)):continue
        if not m[2].startswith('data'):raise ValueError('non-data symbol: '+line)
        # GCC emits its own canonical callback vtable. Only the original
        # address-named dispatch array is imported for original initializers.
        if m[1]=='_ZTVN8dGraph_c10callback_cE': continue
        names.setdefault(int(m[3],16),[]).append(m[1])
    relocs={}
    resources={}
    for line in (cfg/'overlays/ov002/symbols.txt').read_text().splitlines():
        m=re.fullmatch(r'(\w+) kind:(\S+) addr:0x([0-9a-f]+)(?: ambiguous)?',line)
        if m and int(m[3],16) in RESOURCE_TARGETS:
            if m[2]!='bss':raise ValueError('resource handle is not BSS: '+line)
            resources[int(m[3],16)]=m[1]
    for line in (cfg/'relocs.txt').read_text().splitlines():
        m=re.match(r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)',line)
        if not m or not inside(int(m[1],16)):continue
        address,target=int(m[1],16),int(m[3],16)
        # SPA is a self-contained binary resource: these scanner matches are
        # numeric particle/texture fields, never native pointers. Preserve all
        # bytes, including values that happen to match ARM9 address ranges.
        if 0x02075f14 <= address < 0x02082128:
            continue
        resource = address in RESOURCE_POINTERS
        expected = RESOURCE_POINTERS if resource else POINTERS
        module = 'overlay(2)' if resource else 'main'
        if m[2]!='load' or m[4]!=module or expected.get(address)!=target:
            raise ValueError('unexpected relocation: '+line)
        if struct.unpack_from('<I',blob,address-0x02004000)[0]!=target:
            raise ValueError('ROM pointer mismatch')
        relocs[address]=(resources if resource else targets)[target]
    if set(relocs)!=set(POINTERS)|set(RESOURCE_POINTERS):raise ValueError('missing reviewed relocation')
    # These symbols are absolute overlay IDs: callers take their address.
    # ROM literal loads at0202e024/054/05c pass the values to the callback.
    overlay_ids = struct.unpack_from('<3I', blob, 0x0202e07c-0x02004000)
    if overlay_ids != (60, 98, 102):
        raise ValueError('unexpected overlay ID literals')
    for address, instruction in [(0x0202e024, 0xe59f0050),
                                 (0x0202e054, 0xe59f0024),
                                 (0x0202e05c, 0xe59f0020)]:
        if struct.unpack_from('<I', blob, address-0x02004000)[0] != instruction:
            raise ValueError('unexpected overlay ID load instruction')
    lines=['// Generated initialized Stage and font data from local ROM.', 'asm(R"TABLES(']
    for overlay_id in overlay_ids:
        lines += [f'.global overlay_{overlay_id}',
                  f'.set overlay_{overlay_id}, {overlay_id}']
    for start,end in SPANS:
        if start not in names:raise ValueError('unnamed span start')
        lines += [f'.pushsection .data.stage_tables_{start:08x},"aw",%progbits','.balign 4']
        cursor=start
        while cursor<end:
            for name in names.get(cursor,[]):
                lines += [f'.global {name}',f'.type {name}, %object',f'{name}:']
            if cursor in relocs:
                if cursor+4>end or any(cursor+i in names for i in range(1,4)):
                    raise ValueError('split pointer')
                lines.append('.word '+relocs[cursor]);cursor+=4;continue
            stop=min(cursor+16,end)
            for boundary in range(cursor+1,stop):
                if boundary in names or boundary in relocs:stop=boundary;break
            raw=blob[cursor-0x02004000:stop-0x02004000]
            if len(raw)!=stop-cursor:raise ValueError('short ROM read')
            lines.append('.byte '+','.join(str(v) for v in raw));cursor=stop
        lines.append('.popsection')
    return '\n'.join(lines+[')TABLES");',''])

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root',type=Path);ap.add_argument('output',type=Path)
    a=ap.parse_args();a.output.write_text(generate(a.root))
