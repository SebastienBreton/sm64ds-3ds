#!/usr/bin/env python3
"""Link menu probes in their own directory, reading existing production objects."""
from pathlib import Path
import argparse,json,re,shlex,subprocess
from cmake_link import expand_response_files
root=Path(__file__).resolve().parent.parent;shared=root/'build3ds/cmake';out=root/'build3ds/menu-compile'
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('--wrap',action='append',default=[]);p.add_argument('--extra-object',action='append',type=Path,default=[]);p.add_argument('--omit-object',action='append',default=[])
p.add_argument('--replace-link-input',action='append',default=[],metavar='OLD=NEW',help='Replace an exact object/archive input with an isolated candidate; OLD is relative to shared CMake directory')
a=p.parse_args();source=a.source.resolve();name=source.stem
out.mkdir(parents=True,exist_ok=True)
flags=(shared/'CMakeFiles/smoke_player.dir/flags.make').read_text();args=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
    args+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags,re.M)[1])
obj=out/(name+'.o');elf=out/(name+'.elf');binary=out/(name+'.3dsx')
for path in [obj,elf,binary]:path.unlink(missing_ok=True)
subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*args,'-c',str(source),'-o',str(obj)],check=True)
command=expand_response_files(shlex.split((shared/'CMakeFiles/smoke_player.dir/link.txt').read_text()),shared)
for omitted in a.omit_object:
    assert omitted.endswith('.o') and command.count(omitted)==1, omitted
    command.remove(omitted)
old='CMakeFiles/smoke_player.dir/tests/smoke_player.o';assert command.count(old)==1
command[command.index(old)]=str(obj)
command[command.index('-o')+1]=str(elf)
# Absolutize existing object/archive inputs so the linker runs outside shared build.
command=[str(shared/token) if not token.startswith('-') and (shared/token).is_file() else token for token in command]
replaced_inputs=set()
for replacement in a.replace_link_input:
    old,sep,new=replacement.partition('=')
    if not sep: p.error('--replace-link-input requires OLD=NEW')
    oldpath=(shared/old).resolve();newpath=Path(new).resolve()
    if oldpath.suffix not in ('.o','.a') or newpath.suffix!=oldpath.suffix or not newpath.is_file():
        p.error('replacement must be an existing object/archive of the same kind')
    matches=[i for i,token in enumerate(command) if not token.startswith('-') and Path(token).resolve()==oldpath]
    if not matches:p.error('link input not found: '+old)
    for i in matches:command[i]=str(newpath)
    replaced_inputs.add(oldpath)
# Once production owns the menu closure, reuse that exact object set.
new=[] if any('sm64ds_menu.dir' in token for token in command) else list((out/'objects').glob('*.o'))
manifest=(shared/'CMakeFiles/sm64ds_sound_boot.dir/build.make').read_text()
new += [shared/m[1] for m in re.finditer(r'^sm64ds_sound_boot: (.+\.o)$',manifest,re.M)]
existing={Path(token).resolve() for token in command if token.endswith('.o')}
command[1:1]=[str(path) for path in new if path.resolve() not in existing and path.resolve() not in replaced_inputs]
command[1:1]=[str(path.resolve()) for path in a.extra_object if path.resolve() not in existing]
command[1:1]=['-Wl,--wrap='+symbol for symbol in a.wrap]
(out/(name+'-link-command.json')).write_text(json.dumps(command,indent=2)+'\n')
result=subprocess.run(command,cwd=out,capture_output=True,text=True)
(out/(name+'-link.log')).write_text(result.stdout+result.stderr)
if result.returncode:print(result.stderr[-8000:]);raise SystemExit(result.returncode)
subprocess.run(['/opt/devkitpro/tools/bin/3dsxtool',str(elf),str(binary)],check=True)
print(binary)
