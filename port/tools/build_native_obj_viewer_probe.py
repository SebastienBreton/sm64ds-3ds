#!/usr/bin/env python3
"""Build resident OBJ integration oracle without mutating shared CMake objects."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[2]
out=root/'build3ds/fps-native-obj-viewer';out.mkdir(parents=True,exist_ok=True)
port=Path(__file__).resolve().parents[1];platform=port/'platform/3ds'
golden=root/'build3ds/validation/native-obj-merge-checked/merge-packed.bin'
assert golden.stat().st_size==128*(1024+256*192)
(out/'expected.S').write_text('.section .rodata\n.balign 4\n.global native_obj_merge_expected\nnative_obj_merge_expected:\n.incbin "'+str(golden)+'"\n')
compiler=['rtk','proxy','/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++']
flags=['-std=gnu++17','-O2','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-mword-relocations','-ffunction-sections',
       '-D__3DS__','-DPLAYER_SHARED_GFX','-DSM64DS_GPU_COMPOSITE_CANDIDATE','-DSM64DS_GPU_COMPOSITE_BUILD',
       '-DSM64DS_NATIVE_OBJ_CANDIDATE','-DSM64DS_RESIDENT_LIFETIME_DIAGNOSTICS',
       '-I'+str(platform),'-I'+str(port/'tests'),'-I'+str(port/'ntr/include'),
       '-I'+str(root/'build3ds/cmake/.dkp-generated/viewer_shaders_bin'),
       '-I'+str(root/'build3ds/cmake/.dkp-generated/gpu_composite_shaders_bin'),'-I/opt/devkitpro/libctru/include']
sources=[port/'tests/test_native_obj_viewer.cpp']+[platform/s for s in
    ('viewer_3ds.cpp','native_obj_plan.cpp','native_obj_gpu.cpp','native_obj_merge.cpp','gpu_composite.cpp')]
objects=[]
for source in sources:
    obj=out/(source.name+'.o');subprocess.run(compiler+flags+['-c',str(source),'-o',str(obj)],check=True);objects.append(obj)
elf=out/'test_native_obj_viewer.elf'
subprocess.run(compiler+flags+[str(obj) for obj in objects]+[str(out/'expected.S'),
    str(root/'build3ds/cmake/libntr.a'),
    str(root/'build3ds/cmake/CMakeFiles/viewer_shaders_bin.dir/.dkp-generated/viewer_shaders_bin/viewer_shaders_shbin.o'),
    str(root/'build3ds/cmake/CMakeFiles/gpu_composite_shaders_bin.dir/.dkp-generated/gpu_composite_shaders_bin/gpu_composite_shaders_shbin.o'),
    '-L/opt/devkitpro/libctru/lib','-specs=3dsx.specs','-Wl,--gc-sections','-lcitro3d','-lctru','-lm','-o',str(elf)],check=True)
subprocess.run(['rtk','proxy','/opt/devkitpro/tools/bin/3dsxtool',str(elf),str(elf.with_suffix('.3dsx'))],check=True)
print(elf.with_suffix('.3dsx'))
