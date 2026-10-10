#!/usr/bin/env python3
"""Differential check of native forward copies against original ARM instructions."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_LR
ROOT=Path(__file__).resolve().parent.parent
source=(ROOT/'port/hal/model_host.cpp').read_text()
a=source.index('void MultiCopy_Int(');b=source.index('// ---- OAM shadow',a)
transfer=(ROOT/'port/ntr/include/ntr/memory_transfer.h').read_text()
units=transfer[transfer.index('inline void memory_copy_units'):transfer.rfind('\n}')]
shim='''#include <cstring>
#include "ntr/memory_byte_primitives.h"
typedef unsigned int u32;
namespace ntr {
static bool bus_enabled;
template<class T> T* xlate(T* p) { return p; }
inline bool memory_bus_required(const void*,const void*){return bus_enabled;}
struct MemoryTransferBus {
    unsigned read(uintptr_t p,unsigned width)const{unsigned v=0;std::memcpy(&v,(void*)p,width);return v;}
    void write(uintptr_t p,unsigned v,unsigned width)const{std::memcpy((void*)p,&v,width);}
};
'''+units+'''
}
extern "C" void set_bus(unsigned enabled){ntr::bus_enabled=enabled;}
extern "C" {
'''+source[a:b]+'\n}\n'
rom=(ROOT/'sm64ds-decomp/extracted/arm9_dec.bin').read_bytes()
uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
uc.mem_map(0x02004000,0x100000);uc.mem_write(0x02004000,rom)
uc.mem_map(0x03000000,0x10000);uc.mem_map(0x04000000,0x1000)
# Block-copy primitive saves registers on the stack.
from unicorn.arm_const import UC_ARM_REG_SP
cases=0
with tempfile.TemporaryDirectory() as td:
    td=Path(td);(td/'copy.cpp').write_text(shim)
    subprocess.run(['clang++','-std=c++17','-O2','-shared','-fPIC','-I'+str(ROOT/'port/ntr/include'),str(td/'copy.cpp'),'-o',str(td/'copy.dylib')],check=True)
    lib=ctypes.CDLL(str(td/'copy.dylib'))
    for name,entry in [('MultiCopy_Int',0x0205a490),('MultiCopy32Bytes',0x0205a4f4)]:
        fn=getattr(lib,name);fn.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int];fn.restype=None
        for length in [0,1,3,4,8,24,31,32,33,36,64,128,992,1024]:
            for displacement in [-64,-32,-8,-4,0,4,8,16,32,64,2048]:
                data=bytes((i*73+(i>>3)*17)&255 for i in range(8192))
                native=ctypes.create_string_buffer(data,len(data))
                src=128;dst=src+displacement
                uc.mem_write(0x03000000,data)
                for reg,val in [(UC_ARM_REG_R0,0x03000000+src),(UC_ARM_REG_R1,0x03000000+dst),(UC_ARM_REG_R2,length),(UC_ARM_REG_LR,0x04000000),(UC_ARM_REG_SP,0x0300f000)]:uc.reg_write(reg,val)
                uc.emu_start(entry,0x04000000,count=10000)
                expected=bytes(uc.mem_read(0x03000000,len(data)))
                for enabled in (0,1):
                    ctypes.memmove(native,data,len(data))
                    lib.set_bus(enabled)
                    fn(ctypes.addressof(native)+src,ctypes.addressof(native)+dst,length)
                    assert native.raw==expected,(name,length,displacement,enabled)
                    cases+=1
report=f'PASS: {cases} native vs original ARM cases; legacy and bus-routed branches, forward/backward overlap, block load-before-store, trailing words, empty copy and non-overlap. Full buffers compared. The host memory adapter tests transfer ordering; physical OR/broadcast routing is checked separately.\n'
(ROOT/'build3ds/validation/copy-primitives-verification.txt').write_text(report)
print(report,end='')
