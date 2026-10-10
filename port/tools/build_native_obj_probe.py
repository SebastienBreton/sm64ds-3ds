#!/usr/bin/env python3
"""Build the autonomous native OBJ probe from pinned DS full-image fixtures."""
from pathlib import Path
import subprocess
import struct
root=Path(__file__).resolve().parents[2]
out=root/'build3ds/fps-native-obj';out.mkdir(parents=True,exist_ok=True)
port=Path(__file__).resolve().parents[1];platform=port/'platform/3ds'
expected=root/'build3ds/validation/native-obj-128/expected-packed.bin'
assert expected.stat().st_size==128*(256*4+256*192)
merged=root/'build3ds/validation/native-obj-merge-checked/merge-packed.bin'
assert merged.stat().st_size==128*(256*4+256*192)
subprocess.run(['rtk','proxy','/opt/devkitpro/tools/bin/picasso','-o',str(out/'shader.shbin'),str(platform/'gpu_composite.v.pica')],check=True)
raw=(out/'shader.shbin').read_bytes();words=struct.unpack('<'+'I'*(len(raw)//4),raw)
(out/'fps_gpu_composite_shader.h').write_text('#pragma once\n#include <cstdint>\nalignas(4) static const uint32_t fps_gpu_composite_shader[]={'+','.join(hex(x) for x in words)+'};\n')
(out/'expected.S').write_text('.section .rodata\n.balign 4\n.global native_obj_expected\nnative_obj_expected:\n.incbin "'+str(expected)+'"\n')
with (out/'expected.S').open('a') as f:
    f.write('.balign 4\n.global native_obj_merge_expected\nnative_obj_merge_expected:\n.incbin "'+str(merged)+'"\n')
elf=out/'test_native_obj_gpu.elf'
subprocess.run(['rtk','proxy','/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=gnu++17','-O2','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-mword-relocations','-ffunction-sections','-D__3DS__',
               '-I'+str(platform),'-I'+str(port/'tests'),'-I'+str(port/'ntr/include'),'-I'+str(out),'-I/opt/devkitpro/libctru/include',
               str(port/'tests/test_native_obj_gpu.cpp'),str(platform/'native_obj_plan.cpp'),str(platform/'native_obj_gpu.cpp'),str(platform/'native_obj_merge.cpp'),str(platform/'gpu_composite.cpp'),str(port/'ntr/vram_banks.cpp'),str(out/'expected.S'),
               '-L/opt/devkitpro/libctru/lib','-specs=3dsx.specs','-Wl,--gc-sections','-lcitro3d','-lctru','-lm','-o',str(elf)],check=True)
subprocess.run(['rtk','proxy','/opt/devkitpro/tools/bin/3dsxtool',str(elf),str(elf.with_suffix('.3dsx'))],check=True)
print(elf.with_suffix('.3dsx'))
