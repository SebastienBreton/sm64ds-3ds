#!/usr/bin/env python3
"""Original ARM fill16/copy16/fill32 over an independent physical bank model.

This writes a NEW fixture archive. The older nine transfer fixtures are untouched.
VRAM halfword/word addresses are normalized in this bus model, independently of
Unicorn's ordinary-memory handling. Ordinary copy pointers are aligned.
"""
from pathlib import Path
import argparse
import hashlib
import json
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR, UC_ARM_REG_SP

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=ROOT/'build3ds/validation/physical-vram-stores')
OUT = parser.parse_args().output.resolve()
OUT.mkdir(parents=True, exist_ok=True)
ENTRIES = [0x0205a448, 0x0205a460, 0x0205a4a8]
BANK_SIZES = [0x20000]*4+[0x10000,0x4000,0x4000,0x8000,0x4000]
RAM, VRAM, OAM, STOP = 0x03000000, 0x06000000, 0x07000000, 0x04000000
MASK64 = (1 << 64)-1


def physical_seed(bank, offset):
    return ((offset*53+(offset >> (bank % 5+1))*19+bank*41)^0xa5)&255


def flat_seed(region, offset):
    return ((offset*(53 if region==0 else 17 if region==1 else 23)
             +(offset >> (2 if region==0 else 3))*19)^([0xc3,0x6b,0x51][region]))&255


def mix(value, terms):
    for term in terms:
        value = ((value*1099511628211)^term)&MASK64
    return value


profiles = {
    0: ('A MainBG', [0x81,0,0,0,0,0,0,0,0]),
    1: ('A/B MainBG overlap', [0x81,0x81,0,0,0,0,0,0,0]),
    2: ('C SubBG', [0,0,0x84,0,0,0,0,0,0]),
    3: ('attached unmapped', [0]*9),
    4: ('legacy null bus', [0]*9),
}


def banks_at(profile, address):
    # Deliberately separate from the production page-table/resolver code.
    if profile in (0,1) and 0x06000000 <= address < 0x06200000:
        offset = address & 0x7ffff
        if offset < 0x20000:
            return [(b, offset) for b in range(profile+1)]
    if profile == 2 and 0x06200000 <= address < 0x06400000:
        return [(2, address & 0x1ffff)]
    return []


def cases():
    lengths = [-2147483648,-1,0,1,2,3,17,2048,4096]
    values = [0,0x1f,0xf0,0xffff]
    for profile in range(4):
        addresses = ([0x06200000,0x06200001,0x06200102,0x0621ffff,0x06220001,0x063fffff]
                     if profile==2 else
                     [0x06000000,0x06000001,0x06000102,0x0601ffff,0x06020000,0x0607ffff,0x06080001,0x06fffffc])
        for dst in addresses:
            for length in lengths:
                for value in values:
                    yield (0,profile,RAM+0x1000,dst,length,value)
                    yield (2,profile,RAM+0x1000,dst,length,value)
                for value in (0x12345678,0xffffffff):
                    yield (2,profile,RAM+0x1000,dst,length,value)
                yield (1,profile,RAM+0x1000,dst,length,0)
    for profile in range(3):
        base = 0x06200000 if profile==2 else 0x06000000
        alias = 0x20000 if profile==2 else 0x80000
        for align in (0,1):
            for displacement in (-4,-2,-1,0,1,2,4):
                for length in lengths:
                    yield (1,profile,base+0x100+align,base+0x100+align+displacement,length,0)
        for offset in (0,1,alias):
            for length in lengths:
                yield (1,profile,base+0x100+offset,RAM+0x3000,length,0)
        for source,destination in [(base+alias+0x100,base+0x102),
                                   (base+0x100,base+alias+0x102),
                                   (base+alias+0x101,base+0x101)]:
            for length in lengths:
                yield (1,profile,source,destination,length,0)
    # Real original fallback, always aligned ordinary source and destination.
    for profile in (0,4):
        for destination in (RAM+0x3000, VRAM+0x100 if profile==4 else OAM+0x100):
            for length in lengths:
                for value in values:
                    yield (0,profile,RAM+0x1000,destination,length,value)
                    yield (2,profile,RAM+0x1000,destination,length,value)
                yield (1,profile,RAM+0x1000,destination,length,0)
        for length in lengths:
            yield (1,profile,VRAM+0x100,RAM+0x3000,length,0)


def main():
    rom = (ROOT/'sm64ds-decomp/extracted/arm9_dec.bin').read_bytes()
    machine = Uc(UC_ARCH_ARM,UC_MODE_ARM)
    machine.mem_map(0x02004000,0x100000)
    machine.mem_write(0x02004000,rom)
    machine.mem_map(RAM,0x10000)
    machine.mem_map(VRAM,0x1000000)
    machine.mem_map(OAM,0x2000)
    machine.mem_map(STOP,0x1000)
    initial = [bytes(physical_seed(b,i) for i in range(size)) for b,size in enumerate(BANK_SIZES)]
    flats = [bytes(flat_seed(r,i) for i in range(size)) for r,size in enumerate([0x10000,0x2000,0x2000])]
    current = None
    physical = []
    touched = []
    reads = writes = events = 0
    read_hash = write_hash = event_hash = access_hash = 0

    def read_vram(uc, kind, address, width, value, user):
        nonlocal reads,read_hash,access_hash
        if current[1]==4:
            return
        assert width in (2,4)
        aligned = address & ~(width-1)
        result = 0
        for bank,offset in banks_at(current[1],aligned):
            result |= int.from_bytes(physical[bank][offset:offset+width],'little')
        reads += 1
        read_hash = mix(read_hash,(aligned,width,result))
        access_hash = mix(access_hash,(0,aligned,width,result))
        # Feed the ARM load from this physical oracle, not Unicorn flat VRAM.
        uc.mem_write(address,result.to_bytes(width,'little'))

    def write_vram(uc, kind, address, width, value, user):
        nonlocal writes,write_hash,events,event_hash,access_hash
        if current[1]==4:
            return
        assert width in (2,4)
        value &= (1 << (8*width))-1
        aligned = address & ~(width-1)
        writes += 1
        write_hash = mix(write_hash,(aligned,width,value))
        access_hash = mix(access_hash,(1,aligned,width,value))
        for bank,offset in banks_at(current[1],aligned):
            physical[bank][offset:offset+width] = value.to_bytes(width,'little')
            touched[bank].append((offset,offset+width))
            events += 1
            event_hash = mix(event_hash,(bank,offset,width,value))

    machine.hook_add(UC_HOOK_MEM_READ,read_vram,begin=VRAM,end=OAM-1)
    machine.hook_add(UC_HOOK_MEM_WRITE,write_vram,begin=VRAM,end=OAM-1)
    rows,patches,blob = [],[],bytearray()
    totals = [0,0,0]
    for current in cases():
        kind,profile,source,destination,length,value = current
        physical = [bytearray(b) for b in initial]
        touched = [[] for _ in initial]
        reads = writes = events = read_hash = write_hash = event_hash = access_hash = 0
        for address,data in zip((RAM,VRAM,OAM),flats):
            machine.mem_write(address,data)
        args = (source,destination,length) if kind==1 else (value,destination,length)
        for register,argument in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2),args):
            machine.reg_write(register,argument & 0xffffffff)
        machine.reg_write(UC_ARM_REG_SP,STOP+0xff0)
        machine.reg_write(UC_ARM_REG_LR,STOP)
        machine.emu_start(ENTRIES[kind],STOP,count=100000)
        # Generated full-state patches, not a recomputed native transfer model.
        start = len(patches)
        for bank,intervals in enumerate(touched):
            merged = []
            for a,b in sorted(intervals):
                if merged and a<=merged[-1][1]:
                    merged[-1][1]=max(b,merged[-1][1])
                else:
                    merged.append([a,b])
            for a,b in merged:
                patches.append((bank,a,len(blob),b-a))
                blob.extend(physical[bank][a:b])
        # Ordinary memory outside the physical bus stays on the legacy path.
        # VRAM flat bytes touched by Unicorn are NOT a physical-bus oracle.
        for region,(address,before) in enumerate(zip((RAM,VRAM,OAM),flats)):
            if region==1 and profile!=4:
                continue
            after = bytes(machine.mem_read(address,len(before)))
            differing = [i for i,(a,b) in enumerate(zip(before,after)) if a!=b]
            if differing:
                a,b = differing[0],differing[-1]+1
                patches.append((9+region,a,len(blob),b-a))
                blob.extend(after[a:b])
        rows.append((*current,start,len(patches)-start,reads,read_hash,writes,write_hash,events,event_hash,reads+writes,access_hash))
        totals[kind] += 1
    header = '''#pragma once
// GENERATED from original ARM ROM by port/tools/generate_vram_store_fixtures.py.
// Independent VRAM normalization/OR/broadcast; ordinary unaligned RAM excluded.
struct StoreGoldenCase {unsigned kind,profile,source,destination;int bytes;unsigned value,patch_start,patch_count,reads;unsigned long long read_hash;unsigned writes;unsigned long long write_hash;unsigned events;unsigned long long event_hash;unsigned accesses;unsigned long long access_hash;};
struct StoreGoldenPatch {unsigned region,offset,blob_start,bytes;};
'''
    header += 'static const StoreGoldenCase store_cases[]={\n'
    for row in rows:
        cells = [str(v)+('ull' if i in (9,11,13,15) else 'u' if i!=4 else '') for i,v in enumerate(row)]
        header += '{'+','.join(cells)+'},\n'
    header += '};\nstatic const StoreGoldenPatch store_patches[]={\n'
    header += ''.join('{'+','.join(str(v)+'u' for v in row)+'},\n' for row in patches)+'};\n'
    header += 'static const unsigned char store_blob[]={\n'
    header += ''.join(','.join(map(str,blob[i:i+32]))+',\n' for i in range(0,len(blob),32))+'};\n'
    (OUT/'vram_store_fixtures.h').write_text(header)
    disassembler = Cs(CS_ARCH_ARM,CS_MODE_ARM)
    instructions = []
    for address,length in zip(ENTRIES,[0x18,0x1c,0x4c]):
        body = rom[address-0x02004000:address-0x02004000+length]
        instructions.append({'entry':hex(address),'bytes':body.hex(),
                             'asm':[f'{i.address:08x} {i.mnemonic} {i.op_str}' for i in disassembler.disasm(body,address)]})
    report = {'status':'GENERATED_NOT_EXECUTED','cases':len(rows),
              'kind_counts':dict(zip(['MultiStore16','MultiCopyHalf','MultiStore32Bytes'],totals)),
              'patches':len(patches),'blob_bytes':len(blob),
              'rom_sha256':hashlib.sha256(rom).hexdigest(),
              'header_sha256':hashlib.sha256(header.encode()).hexdigest(),
              'profiles':profiles,'original_instructions':instructions,
              'scope':'VRAM ARM stores/loads modeled separately with width normalization; aligned ordinary RAM fallback; entire9banks and ordinary buffers plus ordered read/write/bank-event traces.',
              'excluded':'Unaligned ordinary RAM ARM9 semantics are not inferred from Unicorn.'}
    (OUT/'golden-proof.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/Path(__file__).name).write_bytes(Path(__file__).read_bytes())
    print(json.dumps({k:report[k] for k in ['status','cases','kind_counts','patches','blob_bytes']}))


if __name__=='__main__':
    main()
