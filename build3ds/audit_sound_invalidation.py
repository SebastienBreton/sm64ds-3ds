#!/usr/bin/env python3
"""Execute the supplied ROM's ARM7 invalidation selectors with Unicorn.

Only the downstream player/hardware-stop calls are intercepted. The original
loops, track lookup, range comparisons, format checks and flag writes execute.
Addresses below are offsets in the extracted ARM7 load image, not claims about
the relocated runtime PC of each SDK section.
"""
import hashlib
import json
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

ROOT=Path(__file__).resolve().parent.parent
BLOB=(ROOT/'sm64ds-decomp/extracted/arm7.bin').read_bytes()
BASE=0x2380000
PLAYERS=0x3807b04
TRACKS=0x3807d44
CHANNELS=0x38075c4
FUNCTIONS={0x1b:0x2386068,0x1c:0x238600c,0x1d:0x2384148}

def word(address):return struct.unpack_from('<I',BLOB,address-BASE)[0]
def branch_target(address):
    ins=word(address)
    assert ins&0x0e000000==0x0a000000
    displacement=ins&0xffffff
    if displacement&0x800000:displacement-=0x1000000
    return address+8+displacement*4

def run(command,records,start=0x1000,end=0x1fff):
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    cpu.mem_map(BASE,0x30000);cpu.mem_write(BASE,BLOB)
    cpu.mem_map(0x3800000,0x10000);cpu.mem_map(0x3000000,0x10000)
    def w32(a,v):cpu.mem_write(a,struct.pack('<I',v))
    def w8(a,v):cpu.mem_write(a,bytes([v]))
    for i in range(16):cpu.mem_write(PLAYERS+i*0x24+8,b'\xff'*16)
    track=0
    for index,record in enumerate(records):
        if command==0x1d:
            w8(CHANNELS+index*0x54+1,record.get('format',0))
            w8(CHANNELS+index*0x54+3,0x82)
            w32(CHANNELS+index*0x54+0x44,record['address'])
        else:
            w8(PLAYERS+index*0x24,record.get('active',1))
            w32(PLAYERS+index*0x24+0x20,record.get('bank',0))
            for slot,address in enumerate(record.get('tracks',[])):
                w8(PLAYERS+index*0x24+8+slot,track)
                w32(TRACKS+track*0x40+0x28,address);track+=1
    stopped=[]
    sentinel=0x300ff00
    def hook(uc,address,size,user):
        if address==sentinel:uc.emu_stop()
        elif address==0x238592c:
            player=uc.reg_read(UC_ARM_REG_R0)
            assert (player-PLAYERS)%0x24==0
            stopped.append((player-PLAYERS)//0x24)
            w8(player,0);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        elif address==0x2383634:
            assert uc.reg_read(UC_ARM_REG_R1)==0
            stopped.append(uc.reg_read(UC_ARM_REG_R0))
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    # Separate handlers are deliberate: sequence/bank stop a player, wave stops
    # one hardware channel after clearing its pending-start flag.
    cpu.hook_add(UC_HOOK_CODE,hook)
    cpu.reg_write(UC_ARM_REG_R0,start);cpu.reg_write(UC_ARM_REG_R1,end)
    cpu.reg_write(UC_ARM_REG_SP,0x300f000);cpu.reg_write(UC_ARM_REG_LR,sentinel)
    cpu.emu_start(FUNCTIONS[command],sentinel,count=10000)
    assert cpu.reg_read(UC_ARM_REG_PC)==sentinel
    if command==0x1d:
        for i in range(len(records)):
            value=cpu.mem_read(CHANNELS+i*0x54+3,1)[0]
            assert value==(0x80 if i in stopped else 0x82)
    return stopped

def main():
    assert word(0x2386064)==PLAYERS and word(0x23860fc)==PLAYERS
    assert word(0x23859f4)==TRACKS and word(0x23841c0)==CHANNELS
    for command,target in FUNCTIONS.items():
        handler=branch_target(0x2386d60+command*4)
        assert branch_target(handler+8)==target
    cases=[
        (0x1b,[{'tracks':[0xfff]},{'tracks':[0x1000]},{'tracks':[0x1fff]},
               {'tracks':[0x2000]},{'tracks':[0x1001],'active':0},
               {'tracks':[0x3000,0x1200,0x1300]},{'tracks':[]}],[1,2,5]),
        (0x1c,[{'bank':0xfff},{'bank':0x1000},{'bank':0x1fff},
               {'bank':0x2000},{'bank':0x1001,'active':0}],[1,2]),
        (0x1d,[{'address':0xfff},{'address':0x1000},{'address':0x1fff},
               {'address':0x2000},{'address':0x1001,'format':1}],[1,2]),
    ]
    results=[]
    for command,records,expected in cases:
        actual=run(command,records);assert actual==expected,(command,actual,expected)
        results.append({'command':hex(command),'records':records,'stopped':actual})
    report={'arm7_sha256':hashlib.sha256(BLOB).hexdigest(),'range':[0x1000,0x1fff],
            'range_end_inclusive':True,'cases':results,
            'scope':'Original ARM7 selection logic. Downstream stop routines intercepted; not complete mixer validation.'}
    dest=ROOT/'build3ds/validation/arm7-sound-invalidation';dest.mkdir(parents=True,exist_ok=True)
    (dest/'reference.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS original ARM7 sequence/bank/player and PCM-channel selection, inclusive boundaries, inactive-player and PSG exclusion')

if __name__=='__main__':main()
