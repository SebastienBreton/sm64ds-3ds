#!/usr/bin/env python3
"""Check the VRAM-safe decoder's bytes and transactions against original ARM."""
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_LR, UC_ARM_REG_SP
from check_memory_byte_primitives import ROOT, BASE, STOP, WRAPPER, Row

OUT = ROOT / 'build3ds/validation/memory-lz16'


def fixtures():
    for size in (0, 1, 2, 3, 5, 8, 17):
        literals = bytes((i*53+19)&255 for i in range(size))
        stream = ((size << 8) | 16).to_bytes(4, 'little')
        for start in range(0, size, 8):
            stream += b'\0' + literals[start:start+8]
        yield f'synthetic-literal-{size}', stream
    # Both pending-byte parity and committed halfword backreferences.
    for seed_count in (2, 3, 4, 5):
        for displacement in range(1, seed_count+1):
            size = seed_count + 9
            flags = 0x80 >> seed_count
            stream = ((size << 8) | 16).to_bytes(4, 'little')
            stream += bytes([flags]) + bytes(range(33, 33+seed_count))
            stream += (((9-3) << 12) | (displacement-1)).to_bytes(2, 'big')
            yield f'synthetic-reference-{seed_count}-{displacement}', stream
    files = ROOT / 'sm64ds-decomp/extracted/dsd/files'
    selected = 0
    for path in sorted((files / 'MG').rglob('*')):
        if not path.is_file():
            continue
        raw = path.read_bytes()
        stream = raw[4:] if raw[:4] == b'LZ77' else raw
        if len(stream) >= 4 and stream[0] == 16 and (int.from_bytes(stream[:4], 'little') >> 8) <= 40000:
            yield str(path.relative_to(files)), stream
            selected += 1
            if selected == 64:
                break


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rom = (ROOT / 'sm64ds-decomp/extracted/arm9_dec.bin').read_bytes()
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x02004000, 0x100000)
    uc.mem_write(0x02004000, rom)
    uc.mem_map(BASE, 0x40000)
    uc.mem_map(STOP, 0x1000)
    trace = []

    def access(machine, kind, address, width, value, _):
        write = int(kind == UC_MEM_WRITE)
        if not write:
            value = int.from_bytes(machine.mem_read(address, width), 'little')
        trace.append((write, address, width, value & ((1 << (width*8))-1)))

    uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, access, begin=BASE, end=BASE+0x3ffff)
    report = []
    pattern = bytes((i*73+(i >> 3)*17)&255 for i in range(0x40000))
    wrapper = WRAPPER.replace('rows[8192]', 'rows[1048576]') + r'''
extern "C" void decompress(unsigned char* data,unsigned s,unsigned d) {
    count=0; Bus bus{data}; ntr::memory_decompress_lz16(bus,s,d);
}
'''
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        source = td / 'candidate.cpp'
        source.write_text(wrapper)
        library = td / 'candidate.dylib'
        subprocess.run(['clang++', '-std=c++17', '-O2', '-shared', '-fPIC',
                        '-I'+str(ROOT / 'port/ntr/include'),
                        str(source), '-o', str(library)], check=True)
        native = ctypes.CDLL(str(library))
        native.decompress.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint]
        native.trace_count.restype = ctypes.c_uint
        native.trace_rows.restype = ctypes.POINTER(Row)
        fixture_files = []
        for name, stream in fixtures():
            data = bytearray(pattern)
            data[0x1000:0x1000+len(stream)] = stream
            fixture_file = td / f'fixture-{len(fixture_files)}.bin'
            fixture_file.write_bytes(data)
            fixture_files.append(str(fixture_file))
            buffer = ctypes.create_string_buffer(bytes(data), len(data))
            native.decompress(buffer, BASE+0x1000, BASE+0x20000)
            uc.mem_write(BASE, bytes(data))
            trace.clear()
            for reg, val in ((UC_ARM_REG_R0, BASE+0x1000), (UC_ARM_REG_R1, BASE+0x20000),
                             (UC_ARM_REG_SP, STOP+0xff0), (UC_ARM_REG_LR, STOP)):
                uc.reg_write(reg, val)
            uc.emu_start(0x0205a754, STOP, count=2000000)
            assert buffer.raw == bytes(uc.mem_read(BASE, len(data))), ('bytes', name)
            rows = native.trace_rows()
            actual = [tuple(getattr(rows[i], f) for f, _ in Row._fields_)
                      for i in range(native.trace_count())]
            assert actual == trace, ('transactions', name, actual[:12], trace[:12])
            assert all(row[2] == 2 for row in actual if row[0]), ('byte destination store', name)
            report.append(dict(fixture=name, stream_sha256=hashlib.sha256(stream).hexdigest(),
                               output_bytes=int.from_bytes(stream[:4], 'little') >> 8,
                               transactions=len(actual)))
        sanitized_source = td / 'sanitized.cpp'
        sanitized_source.write_text(wrapper + r'''
#include <cstdio>
int main(int argc,char** argv) {
    unsigned char data[0x40000];
    for(int i=1;i<argc;++i) {
        FILE* f=std::fopen(argv[i],"rb");
        if(!f)return 1;
        const auto bytes=std::fread(data,1,sizeof data,f);std::fclose(f);
        if(bytes!=sizeof data)return 2;
        decompress(data,0x03001000,0x03020000);
    }
}
''')
        subprocess.run(['clang++', '-std=c++17', '-O1', '-g',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I'+str(ROOT / 'port/ntr/include'),
                        str(sanitized_source), '-o', str(td / 'sanitized')], check=True)
        subprocess.run([str(td / 'sanitized'), *fixture_files], check=True)
        subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',
                        '-std=c++17', '-O2', '-march=armv6k', '-mfpu=vfp', '-mfloat-abi=hard',
                        '-I'+str(ROOT / 'port/ntr/include'),
                        '-c', str(source), '-o', str(td / 'candidate.o')], check=True)
    header = (ROOT / 'port/ntr/include/ntr/memory_byte_primitives.h').read_bytes()
    summary = dict(status='PASS', original_entry='0205a754', cases=len(report),
                   transactions=sum(row['transactions'] for row in report),
                   header_sha256=hashlib.sha256(header).hexdigest(),
                   rom_sha256=hashlib.sha256(rom).hexdigest(),
                   checks=['whole buffer bytes', 'exact ordered bus reads/writes',
                           'pending byte parity', 'odd final byte', 'no byte destination stores',
                           'ASan/UBSan', 'ARM11 compilation'],
                   runtime_integration='SDK conditional on physical bank bus; disabled by default', fixtures=report)
    (OUT / 'report.json').write_text(json.dumps(summary, indent=2)+'\n')
    (OUT / 'memory_byte_primitives.h').write_bytes(header)
    print(json.dumps({k: v for k, v in summary.items() if k != 'fixtures'}))


if __name__ == '__main__':
    main()
