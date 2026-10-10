#!/usr/bin/env python3
"""Build an isolated GPU composition oracle without shared CMake objects."""
from pathlib import Path
import subprocess
import struct
import argparse

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--viewer',action='store_true');args=parser.parse_args()
out=root/('build3ds/fps-gpu-composite-viewer' if args.viewer else 'build3ds/fps-gpu-composite')
out.mkdir(parents=True,exist_ok=True)
platform=Path(__file__).resolve().parents[1] / 'platform/3ds'
shader=out/'gpu_composite.shbin'
subprocess.run(['/opt/devkitpro/tools/bin/picasso','-o',str(shader),str(platform/'gpu_composite.v.pica')],check=True)
raw=shader.read_bytes()
words=struct.unpack('<'+'I'*(len(raw)//4),raw)
(out/'fps_gpu_composite_shader.h').write_text('#pragma once\n#include <cstdint>\nalignas(4) static const uint32_t fps_gpu_composite_shader[]={\n'+','.join(hex(x) for x in words)+'};\n')
name='test_gpu_composite_viewer' if args.viewer else 'test_gpu_composite'
elf=out/(name+'.elf')
extra=[]
if args.viewer:
    generated=root/'build3ds/cmake/.dkp-generated/viewer_shaders_bin'
    extra=['-DSM64DS_GPU_COMPOSITE_CANDIDATE','-I'+str(generated),str(platform/'viewer_3ds.cpp'),str(generated/'viewer_shaders_shbin.S')]
subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=gnu++17','-O2','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-mword-relocations','-ffunction-sections','-D__3DS__',
               '-I'+str(platform),'-I'+str(out),'-I'+str(Path(__file__).resolve().parents[1] / 'ntr/include'),'-I/opt/devkitpro/libctru/include',
               str(root/('port/tests/'+name+'.cpp')),str(platform/'gpu_composite.cpp'),str(Path(__file__).resolve().parents[1] / 'ntr/ppu.cpp'),*extra,
               '-L/opt/devkitpro/libctru/lib','-specs=3dsx.specs','-Wl,--gc-sections','-lcitro3d','-lctru','-lm','-o',str(elf)],check=True)
subprocess.run(['/opt/devkitpro/tools/bin/3dsxtool',str(elf),str(elf.with_suffix('.3dsx'))],check=True)
print(elf.with_suffix('.3dsx'))
