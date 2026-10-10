#!/usr/bin/env python3
"""Compile independent golden fixtures, production seams and true __real bodies.

No CMake mutation and no emulator launch. Host Mach-O dispatch uses explicit
symbol renaming; ARM11 uses GNU --wrap against the unrenamed original objects.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shlex
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
PORT=ROOT/'port'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=ROOT/'build3ds/validation/physical-vram-stores')
OUT=parser.parse_args().output.resolve()
OUT.mkdir(parents=True,exist_ok=True)
commands=[]


def run(args,name):
    commands.append(args)
    result=subprocess.run(args,cwd=ROOT,capture_output=True,text=True)
    (OUT/(name+'.log')).write_text(result.stdout+result.stderr)
    if result.returncode:
        (OUT/'build-failure.json').write_text(json.dumps({'status':'FAIL','step':name,'returncode':result.returncode,'commands':commands},indent=2)+'\n')
        raise RuntimeError(name+' failed\n'+result.stdout[-3000:]+result.stderr[-3000:])
    return result.stdout


for source,name in [
    (PORT/'platform/3ds/vram_store16.cpp','vram_store16.cpp'),
    (PORT/'tests/smoke_physical_vram_stores.cpp','smoke_physical_vram_stores.cpp'),
    (PORT/'ntr/vram_bus.cpp','vram_bus.cpp'),(PORT/'ntr/vram_banks.cpp','vram_banks.cpp'),
    (ROOT/'sm64ds-decomp/src/MultiStore16.c','MultiStore16.c'),
    (ROOT/'sm64ds-decomp/src/MultiCopyHalf.c','MultiCopyHalf.c'),
    (PORT/'menu/primitives.cpp','primitives-original.cpp'),
    (PORT/'platform/3ds/cpu_mode.h','cpu_mode.h'),
    (Path(__file__),Path(__file__).name),
]:
    shutil.copy2(source,OUT/name)
primitives=(OUT/'primitives-original.cpp').read_text()
begin=primitives.index('extern "C" void MultiStore32Bytes(')
end=primitives.index('\n}',begin)+2
(OUT/'MultiStore32Bytes.cpp').write_text('#include <cstdint>\n'+primitives[begin:end]+'\n')

host=['-O2','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
      '-D__3DS__','-DSM64DS_VRAM_STORE_HOST','-I'+str(OUT),
      '-I'+str(PORT/'ntr/include'),'-I'+str(ROOT/'sm64ds-decomp/include')]
# Host-only pointer ABI adaptation for the two u32 pointer casts in the
# otherwise unchanged matched copy body. Native uses the original types.h.
(OUT/'host-abi').mkdir(exist_ok=True)
(OUT/'host-abi/types.h').write_text('#include <stdint.h>\ntypedef uintptr_t u32;\ntypedef uint16_t u16;\ntypedef int32_t s32;\n')
inputs=['vram_store16.cpp','vram_bus.cpp','vram_banks.cpp','MultiStore16.c','MultiCopyHalf.c','MultiStore32Bytes.cpp','smoke_physical_vram_stores.cpp']
for name in inputs:
    flags=host[:]
    if name=='MultiCopyHalf.c':flags=['-I'+str(OUT/'host-abi'),*flags]
    if name=='vram_bus.cpp':
        flags+=['-Dvram_cpu_read=vram_cpu_read_real','-Dvram_cpu_write=vram_cpu_write_real']
    if name in ['MultiStore16.c','MultiCopyHalf.c','MultiStore32Bytes.cpp']:
        symbol=name.split('.')[0]
        flags+=['-D'+symbol+'=__real_'+symbol]
    compiler='clang' if name.endswith('.c') else 'clang++'
    flags+=['-std=c11' if name.endswith('.c') else '-std=c++17']
    run([compiler,*flags,'-MMD','-MF',str(OUT/(name+'.host.d')),
         '-c',str(OUT/name),'-o',str(OUT/(name+'.host.o'))],name+'-host-build')
host_binary=OUT/'smoke_physical_vram_stores-host'
run(['clang++',*host,*(str(OUT/(name+'.host.o')) for name in inputs),'-o',str(host_binary)],'host-link')
host_result=run([str(host_binary)],'host-sanitized')
assert '\nPASS\n' in host_result

flags_path=ROOT/'build3ds/cmake/CMakeFiles/ntr.dir/flags.make'
shutil.copy2(flags_path,OUT/'native-flags.make')
native=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
    native+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags_path.read_text(),re.M)[1])
native=['-I'+str(OUT),'-I'+str(ROOT/'sm64ds-decomp/include'),*native]
toolchain='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
for name in inputs:
    c=name.endswith('.c')
    flags=[f for f in native if not(c and f.startswith('-std='))]
    if c:flags+=['-std=gnu11']
    run([toolchain+('gcc' if c else 'g++'),*flags,'-MMD','-MF',str(OUT/(name+'.native.d')),
         '-c',str(OUT/name),'-o',str(OUT/(name+'.native.o'))],name+'-native-build')
elf=OUT/'smoke_physical_vram_stores.elf'
wrappers=['MultiStore16','MultiCopyHalf','MultiStore32Bytes',
          '_ZN3ntr13vram_cpu_readEmj','_ZN3ntr14vram_cpu_writeEmmj']
run([toolchain+'g++','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
     '-specs=3dsx.specs','-Wl,--gc-sections',*('-Wl,--wrap='+s for s in wrappers),
     *(str(OUT/(name+'.native.o')) for name in inputs),
     '-L/opt/devkitpro/libctru/lib','-lctru','-lm','-o',str(elf)],'native-link')
run(['/opt/devkitpro/tools/bin/3dsxtool',str(elf),str(elf.with_suffix('.3dsx'))],'native-3dsx')
run([toolchain+'objdump','-d','-C',str(elf)],'native-disassembly')
dependencies={}
for depfile in OUT.glob('*.d'):
    for token in shlex.split(depfile.read_text().replace('\\\n',' ').split(':',1)[1]):
        path=Path(token)
        if path.is_file():dependencies[str(path.resolve())]=hashlib.sha256(path.read_bytes()).hexdigest()
golden=json.loads((OUT/'golden-proof.json').read_text())
assert golden['header_sha256']==hashlib.sha256((OUT/'vram_store_fixtures.h').read_bytes()).hexdigest()
report={'status':'PASS host ASan/UBSan; original ARM fixtures; ARM11 LINKED_NOT_EXECUTED',
        'host':host_result,'golden':golden,'production_changed':False,
        'native':{'status':'NOT_EXECUTED','binary':str(elf.with_suffix('.3dsx')),'shared_objects_used':False},
        'host_dispatch':'Mach-O explicit __real symbol renames of unchanged bodies; exact production wrapper called directly.',
        'host_abi':'ARM64 host buffers translated for legacy calls; diagnostic types.h widens only u32 pointer casts in MultiCopyHalf to uintptr_t. Native types and DS addresses unchanged.',
        'native_dispatch':'GNU --wrap routes actual public calls; __real resolves to original unrenamed objects.',
        'commands':commands,'dependencies_sha256':dependencies,
        'archive_sha256':{str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted(OUT.rglob('*')) if p.is_file() and p.name!='report.json'}}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(report['status']);print(host_result);print(OUT)
