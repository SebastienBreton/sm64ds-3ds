#!/usr/bin/env python3
"""Independent ARM11 menu compilation audit; never links guessed dependencies."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import json, subprocess, shutil
root=Path(__file__).resolve().parent.parent
repo=root/'sm64ds-decomp';out=root/'build3ds/menu-compile'
shutil.rmtree(out/'generated',ignore_errors=True)
shutil.rmtree(out/'objects',ignore_errors=True)
rows=json.loads((root/'build3ds/reference-menu/source-inventory.json').read_text())['rows']
result=subprocess.run(['python3',str(repo.parent/'port/tools/hostgen.py'),'--out',str(out/'generated'),'--gcc-local-externs',*[r['symbol'] for r in rows],*(repo.parent/'port/menu/dependency_symbols.txt').read_text().split()],capture_output=True,text=True,check=True)
(out/'hostgen.log').write_text(result.stdout+result.stderr)
subprocess.run(['python3',str(repo.parent/'port/tools/menu_sources.py'),'--tree',str(out/'generated')],check=True)
flags=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-mword-relocations','-ffunction-sections','-D__3DS__','-DSM64DS_PLATFORM_PC','-DSM64DS_NATIVE_SHADOW','-std=gnu++17','-fsigned-char','-fno-strict-aliasing','-fpermissive','-Werror=return-type','-O2','-I'+str(repo.parent/'port'),'-I'+str(repo/'include'),'-I'+str(repo.parent/'port/ntr/include'),'-isystem','/opt/devkitpro/libctru/include']
(out/'objects').mkdir(exist_ok=True)
def compile(source):
    obj=out/'objects'/(source.stem+'.o')
    run=subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*flags,'-c',str(source),'-o',str(obj)],capture_output=True,text=True)
    (out/'objects'/(source.stem+'.log')).write_text(run.stdout+run.stderr)
    return {'source':str(source.relative_to(out)),'success':run.returncode==0,'errors':run.stderr if run.returncode else ''}
with ThreadPoolExecutor(max_workers=4) as pool: reports=list(pool.map(compile,sorted((out/'generated').rglob('*.cpp'))))
subprocess.run(['python3',str(repo.parent/'port/tools/menu_storage.py'),str(repo),str(out/'menu_storage.cpp')],check=True,stdout=subprocess.DEVNULL)
for source in [out/'menu_storage.cpp',repo.parent/'port/menu/lifecycle.cpp',repo.parent/'port/menu/primitives.cpp',repo.parent/'port/menu/input.cpp']:
    obj=out/'objects'/(source.stem+'.o')
    subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*flags,'-c',str(source),'-o',str(obj)],check=True)
summary={'compiled':sum(r['success'] for r in reports),'failed':sum(not r['success'] for r in reports),'results':reports}
(out/'compile.json').write_text(json.dumps(summary,indent=2)+'\n')
print({k:v for k,v in summary.items() if k!='results'})
