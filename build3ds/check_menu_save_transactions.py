#!/usr/bin/env python3
"""Compile independent host integration probe without touching shared CMake."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
repo=root/'sm64ds-decomp'
out=root/'build3ds/validation/menu-save-transactions'
out.mkdir(parents=True,exist_ok=True)
def run(args): subprocess.run([str(x) for x in args],check=True)
generated=out/'dispatcher.c'
run(['python3',repo.parent/'port/tools/menu_sources.py',repo/'src/func_ov007_020cc600.c',generated])
objects=[]
for source in [generated,repo/'src/func_02013c84.c']:
    obj=out/(source.stem+'.o');objects.append(obj)
    run(['clang','-c','-I'+str(repo/'include'),source,'-o',obj])
sources=['_ZN8SaveData16SetDefaultValuesEP12FileSaveData.cpp','_ZN8SaveData12ReadFileDataEjP12FileSaveData.cpp','_ZN8SaveData8SaveFileEjP12FileSaveData.cpp','_ZN8SaveData13EraseSaveFileEjPc.cpp']
run(['clang++','-std=c++17','-DSM64DS_PLATFORM_PC','-I'+str(repo/'include'),repo.parent/'port/tests/menu_save_transactions.cpp',repo.parent/'port/hal/save_cart.cpp',*[repo/'src'/s for s in sources],*objects,'-o',out/'probe'])
with tempfile.TemporaryDirectory(prefix='sm64ds-menu-save-') as temp:
    result=subprocess.run([str(out/'probe'),str(Path(temp)/'save.sav')],check=True,capture_output=True,text=True)
(out/'result.txt').write_text(result.stdout)
print(result.stdout,end='')
