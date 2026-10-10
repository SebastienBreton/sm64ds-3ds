#!/usr/bin/env python3
"""Compare transfer results and bus transactions with the original ARM ROM.

This checks read-before-write ordering and preloaded halfwords under overlap,
not just the final result of a non-overlapping memcpy. No emulator is launched.
"""
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'build3ds/validation/memory-byte-primitives'
BASE, STOP = 0x03000000, 0x04000000

WRAPPER = r'''
#include "ntr/memory_byte_primitives.h"
#include <cstring>
struct Row { unsigned write, addr, width, value; };
static Row rows[8192];
static unsigned count;
struct Bus {
    unsigned char* data;
    unsigned read(uintptr_t a, unsigned w) {
        unsigned v=0; std::memcpy(&v,data+a-0x03000000,w);
        rows[count++]={0,unsigned(a),w,v}; return v;
    }
    void write(uintptr_t a,unsigned v,unsigned w) {
        if(w==2)v&=0xffff;
        rows[count++]={1,unsigned(a),w,v};
        std::memcpy(data+a-0x03000000,&v,w);
    }
};
extern "C" void copy(unsigned char* data,unsigned s,unsigned d,unsigned n) {
    count=0; Bus bus{data}; ntr::memory_copy8(bus,s,d,n);
}
extern "C" void fill(unsigned char* data,unsigned d,unsigned v,unsigned n) {
    count=0; Bus bus{data}; ntr::memory_fill8(bus,d,v,n);
}
extern "C" void fixed32(unsigned char* data,unsigned s,unsigned d,unsigned) {
    count=0; Bus bus{data};
    ntr::memory_copy_register_group(bus,s,d,3);
    ntr::memory_copy_register_group(bus,s+12,d+12,3);
    ntr::memory_copy_register_group(bus,s+24,d+24,2);
}
extern "C" void fixed36(unsigned char* data,unsigned s,unsigned d,unsigned) {
    count=0; Bus bus{data};
    for(unsigned i=0;i<3;++i)ntr::memory_copy_register_group(bus,s+12*i,d+12*i,3);
}
extern "C" void fixed48(unsigned char* data,unsigned s,unsigned d,unsigned) {
    count=0; Bus bus{data};
    for(unsigned i=0;i<4;++i)ntr::memory_copy_register_group(bus,s+12*i,d,3);
}
extern "C" unsigned trace_count(){return count;}
extern "C" const Row* trace_rows(){return rows;}
'''


class Row(ctypes.Structure):
    _fields_ = [(field, ctypes.c_uint) for field in ('write', 'addr', 'width', 'value')]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rom = (ROOT / 'sm64ds-decomp/extracted/arm9_dec.bin').read_bytes()
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x02004000, 0x100000)
    uc.mem_write(0x02004000, rom)
    uc.mem_map(BASE, 0x10000)
    uc.mem_map(STOP, 0x1000)
    trace = []

    def access(machine, kind, address, width, value, _):
        write = int(kind == UC_MEM_WRITE)
        if not write:
            value = int.from_bytes(machine.mem_read(address, width), 'little')
        trace.append((write, address, width, value & ((1 << (width * 8)) - 1)))

    uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, access, begin=BASE, end=BASE+0xffff)
    cases = transactions = 0
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        source = td / 'candidate.cpp'
        source.write_text(WRAPPER)
        library = td / 'candidate.dylib'
        subprocess.run(['clang++', '-std=c++17', '-O2', '-shared', '-fPIC',
                        '-I'+str(ROOT / 'port/ntr/include'),
                        str(source), '-o', str(library)], check=True)
        native = ctypes.CDLL(str(library))
        for name in ('copy', 'fill', 'fixed32', 'fixed36', 'fixed48'):
            getattr(native, name).argtypes = [ctypes.c_void_p] + [ctypes.c_uint]*3
        native.trace_count.restype = ctypes.c_uint
        native.trace_rows.restype = ctypes.POINTER(Row)
        lengths = (0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 31, 32, 33, 127, 128, 255, 1024)
        for name, entry, sizes in (('copy', 0x0205a61c, lengths), ('fill', 0x0205a588, lengths),
                                   ('fixed32', 0x0205a52c, (32,)), ('fixed36', 0x0205a548, (36,)),
                                   ('fixed48', 0x02056c1c, (48,))):
            is_copy = name != 'fill'
            for length in sizes:
                for align in range(4):
                    variants = (range(-9, 10) if name == 'copy' else range(-8, 9, 4)) if is_copy else (0, 1, 0x7f, 0x80, 0xff, 0x1234, 0xffffffff)
                    for variant in variants:
                        data = bytes((i*73+(i >> 3)*17)&255 for i in range(4096))
                        buffer = ctypes.create_string_buffer(data, len(data))
                        src = BASE+256+align
                        if name.startswith('fixed') and align:
                            continue  # Original LDM/STM contract requires word alignment.
                        dst = src+variant if is_copy else src
                        args = (src, dst, length) if is_copy else (dst, variant, length)
                        getattr(native, name)(buffer, *args)
                        uc.mem_write(BASE, data)
                        trace.clear()
                        for reg, val in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2), args):
                            uc.reg_write(reg, val)
                        uc.reg_write(UC_ARM_REG_LR, STOP)
                        uc.emu_start(entry, STOP, count=10000)
                        case = (name, length, align, variant)
                        assert buffer.raw == bytes(uc.mem_read(BASE, len(data))), ('bytes', case)
                        rows = native.trace_rows()
                        actual = [tuple(getattr(rows[i], f) for f, _ in Row._fields_)
                                  for i in range(native.trace_count())]
                        assert actual == trace, ('transactions', case, actual[:12], trace[:12])
                        assert all(row[2] in (2, 4) for row in actual), ('byte bus access', case)
                        cases += 1
                        transactions += len(actual)
        sanitized_source = td / 'sanitized.cpp'
        sanitized_source.write_text(WRAPPER + r'''
int main() {
    unsigned char data[4096]{};
    const unsigned lengths[]={0,1,2,3,4,5,7,8,9,15,16,31,32,33,127,128,255,1024};
    const unsigned values[]={0,1,0x7f,0x80,0xff,0x1234,0xffffffff};
    for(unsigned n:lengths)for(unsigned a=0;a<4;++a){
        for(int v=-9;v<=9;++v)copy(data,0x03000100+a,0x03000100+a+v,n);
        for(unsigned v:values)fill(data,0x03000100+a,v,n);
    }
    for(int v=-8;v<=8;v+=4){
        fixed32(data,0x03000100,0x03000100+v,32);
        fixed36(data,0x03000100,0x03000100+v,36);
        fixed48(data,0x03000100,0x03000100+v,48);
    }
}
''')
        subprocess.run(['clang++', '-std=c++17', '-O1', '-g',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I'+str(ROOT / 'port/ntr/include'),
                        str(sanitized_source), '-o', str(td / 'sanitized')], check=True)
        subprocess.run([str(td / 'sanitized')], check=True)
        subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',
                        '-std=c++17', '-O2', '-march=armv6k', '-mfpu=vfp', '-mfloat-abi=hard',
                        '-I'+str(ROOT / 'port/ntr/include'),
                        '-c', str(source), '-o', str(td / 'candidate.o')], check=True)
    header = (ROOT / 'port/ntr/include/ntr/memory_byte_primitives.h').read_bytes()
    (OUT / 'memory_byte_primitives.h').write_bytes(header)
    report = dict(status='PASS', cases=cases, transactions=transactions,
                  original_entries={'CpuCopy8': '0205a61c', 'func_0205a588': '0205a588',
                                    'Copy32Bytes': '0205a52c', 'Copy36Bytes': '0205a548',
                                    'Copy48BytesFixed': '02056c1c'},
                  checks=['whole buffer bytes', 'exact ordered bus reads/writes',
                          'odd edges', 'forward/backward overlap', 'all alignments',
                          'zero length', 'no byte stores', 'ASan/UBSan', 'ARM11 compilation'],
                  header_sha256=hashlib.sha256(header).hexdigest(),
                  rom_sha256=hashlib.sha256(rom).hexdigest(),
                  runtime_integration='SDK conditional on physical bank bus; disabled by default')
    (OUT / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
