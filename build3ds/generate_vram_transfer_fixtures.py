#!/usr/bin/env python3
"""Original ARM SDK operations over independently modelled physical VRAM.

The golden bytes come from ARM instructions. OR/broadcast bank semantics are
also validated independently against melonDS by the bank-bus oracle.
"""
from pathlib import Path
import json
import hashlib
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR, UC_ARM_REG_SP

ROOT = Path(__file__).resolve().parent.parent
BASE, VRAM, STOP, SIZE = 0x03000000, 0x06000000, 0x04000000, 0x20000
HEADER = ROOT / 'port/tests/vram_transfer_fixtures.h'
OUT = ROOT / 'build3ds/validation/physical-vram-transfers'
ENTRIES = [0x0205a61c, 0x0205a588, 0x0205a52c, 0x0205a548, 0x02056c1c,
           0x0205a490, 0x0205a4f4, 0x0205a754, 0x0205a47c]


def seed(bank, offset):
    return ((offset*7+(offset >> 3)) if bank == 0 else (offset*11+(offset >> 5))) & 255


def main():
    cases = []
    for mask in (1, 3):
        for align in range(4):
            for n in (1, 31, 33):
                cases.append((0, mask, 0, 128+align, 256+align, n, 0))
            cases.append((1, mask, 0, 0, 256+align, 33, 0x7f))
        for displacement in (-2, -1, 1, 2, 4):
            cases.append((0, mask, 1, 256, 256+displacement, 31, 0))
        for kind, n in ((2, 32), (3, 36), (4, 48), (5, 33), (6, 63)):
            for displacement in (-4, 0, 4):
                cases.append((kind, mask, 1, 256, 256+displacement, n, 0))
        cases.append((7, mask, 0, 128, 256, 8, 0))
        cases.append((8, mask, 0, 0, 256, 33, 0x12345678))
    rom = (ROOT / 'sm64ds-decomp/extracted/arm9_dec.bin').read_bytes()
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x02004000, 0x100000)
    uc.mem_write(0x02004000, rom)
    uc.mem_map(BASE, 0x10000)
    uc.mem_map(VRAM, SIZE)
    uc.mem_map(STOP, 0x1000)
    physical = []
    active = 0

    def access(machine, kind, address, width, value, _):
        offset = address-VRAM
        if kind == UC_MEM_WRITE:
            if width != 1:
                for bank in range(2):
                    if active & (1 << bank):
                        for i in range(width):
                            physical[bank][(offset+i)&(SIZE-1)] = (value >> (8*i)) & 255
        else:
            result = bytearray(width)
            for bank in range(2):
                if active & (1 << bank):
                    for i in range(width):
                        result[i] |= physical[bank][(offset+i)&(SIZE-1)]
            machine.mem_write(address, bytes(result))

    uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, access, begin=VRAM, end=VRAM+SIZE-1)
    initial = [bytes(seed(bank, i) for i in range(SIZE)) for bank in range(2)]
    source = bytearray((i*53+(i >> 2)*19)&255 for i in range(512))
    stream = bytes([16, 8, 0, 0, 0x20, 65, 66, 0x30, 1])
    source[128:128+len(stream)] = stream
    changes, rows = [], []
    for kind, active, from_vram, src, dst, size, value in cases:
        physical = [bytearray(bank) for bank in initial]
        uc.mem_write(BASE, bytes(source))
        src_address = (VRAM if from_vram else BASE)+src
        args = (VRAM+dst, value, size) if kind == 1 else (value, VRAM+dst, size) if kind == 8 else (src_address, VRAM+dst, size)
        for reg, val in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2), args):
            uc.reg_write(reg, val)
        uc.reg_write(UC_ARM_REG_SP, STOP+0xff0)
        uc.reg_write(UC_ARM_REG_LR, STOP)
        uc.emu_start(ENTRIES[kind], STOP, count=100000)
        start = len(changes)
        for bank in range(2):
            for i, (old, new) in enumerate(zip(initial[bank], physical[bank])):
                if old != new:
                    changes.append((bank, i, new))
        rows.append((kind, active, from_vram, src, dst, size, value, start, len(changes)-start))
    output = '#pragma once\n// GENERATED from original ARM ROM by build3ds/generate_vram_transfer_fixtures.py\n'
    output += 'struct TransferGoldenCase {unsigned kind,mask,from_vram,src,dst,size,value,start,count;};\n'
    output += 'struct TransferGoldenChange {unsigned bank,offset,value;};\n'
    output += 'static const unsigned char transfer_source[]={'+','.join(map(str, source))+'};\n'
    output += 'static const TransferGoldenCase transfer_cases[]={\n'+''.join('{'+','.join(map(str, row))+'},\n' for row in rows)+'};\n'
    output += 'static const TransferGoldenChange transfer_changes[]={\n'+''.join('{'+','.join(map(str, row))+'},\n' for row in changes)+'};\n'
    HEADER.write_text(output)
    OUT.mkdir(parents=True, exist_ok=True)
    report = dict(status='GENERATED_NOT_EXECUTED', cases=len(rows), changed_bytes=len(changes),
                  rom_sha256=hashlib.sha256(rom).hexdigest(), header_sha256=hashlib.sha256(output.encode()).hexdigest(),
                  bank_model='separate A/B, mapped singly or overlapping; OR reads, broadcast writes',
                  original_entries=[hex(entry) for entry in ENTRIES])
    (OUT / 'golden-proof.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
