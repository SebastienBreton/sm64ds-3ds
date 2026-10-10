#!/usr/bin/env python3
"""Link a focused entry point against the current native player objects.

Build smoke_player first. This does not replace its executable or bypass linker
errors; unreachable game paths are removed by the normal section collector.
"""
from pathlib import Path
import re,shlex,subprocess,sys,argparse
ROOT=Path(__file__).resolve().parent.parent
BUILD=ROOT/'build3ds/cmake'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source',type=Path)
parser.add_argument('--object-target',action='append',default=[])
parser.add_argument('--wrap',action='append',default=[],help='wrap a linked symbol for a focused diagnostic')
options=parser.parse_args()
source=options.source.resolve()
name=source.stem
flags=(BUILD/'CMakeFiles/smoke_player.dir/flags.make').read_text()
args=[]
for key in ['CXX_INCLUDES','CXX_FLAGS']:
    args+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags,re.M)[1])
obj=BUILD/(name+'.o');elf=BUILD/(name+'.elf')
# A failed rebuild must never leave a runnable stale probe behind.
if name=='smoke_player':raise ValueError('use the production CMake target for smoke_player')
for artifact in (obj,elf,BUILD/(name+'.3dsx')):artifact.unlink(missing_ok=True)
compiler='/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'
subprocess.run([compiler,*args,'-Dmain=smoke_main','-c',str(source),'-o',str(obj)],check=True)
command=shlex.split((BUILD/'CMakeFiles/smoke_player.dir/link.txt').read_text())
old='CMakeFiles/smoke_player.dir/tests/smoke_player.o'
assert command.count(old)==1
command[command.index(old)]=str(obj)
for symbol in options.wrap:
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*',symbol):raise ValueError('invalid wrapped symbol')
    command.insert(1,'-Wl,--wrap='+symbol)
for target in options.object_target:
    if not re.fullmatch(r'[A-Za-z0-9_]+',target):raise ValueError('invalid object target')
    manifest=(BUILD/'CMakeFiles'/f'{target}.dir'/'build.make').read_text()
    objects=[BUILD/m[1] for m in re.finditer(r'^'+re.escape(target)+r': (.+\.o)$',manifest,re.M)]
    if not objects:raise ValueError(f'no compiled objects for {target}')
    existing={(BUILD/arg).resolve() for arg in command if arg.endswith('.o')}
    command[1:1]=[str(obj) for obj in objects if obj.resolve() not in existing]
command[command.index('-o')+1]=str(elf)
run=subprocess.run(command,cwd=BUILD,capture_output=True,text=True)
(BUILD.parent/(name+'-link.log')).write_text(run.stdout+run.stderr)
if run.returncode:
    print(run.stderr[-6000:]);sys.exit(run.returncode)
subprocess.run(['/opt/devkitpro/tools/bin/3dsxtool',str(elf),str(BUILD/(name+'.3dsx'))],check=True)
print('Built focused player probe:',name)
