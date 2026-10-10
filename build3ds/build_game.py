#!/usr/bin/env python3
"""Build the interactive entry against the current native game object graph.

First build smoke_player_3dsx with CMake. This entry has no scripted controls,
stage selection, or default frame limit. It remains a development build.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shlex
import shutil
import subprocess
import sys

root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--user-build', action='store_true', help='Separate manual-play save/log; ignore automated test settings')
parser.add_argument('--source',type=Path,help='Isolated diagnostic copy of game_main.cpp')
parser.add_argument('--output',type=Path,help='Separate output directory for an interactive or diagnostic build')
parser.add_argument('--replace-link-input',action='append',default=[],metavar='OLD=NEW',help='Isolated object/archive candidate forwarded to build_menu_probe.py')
parser.add_argument('--wrap',action='append',default=[],help='Additional symbol wrapper for an isolated diagnostic')
args=parser.parse_args()
if args.user_build and (args.source or args.replace_link_input or args.wrap):
    parser.error('--user-build cannot be combined with diagnostic source/link replacements/wrappers')
build=root/'build3ds/cmake'
out=args.output.resolve() if args.output else root/('build3ds/user-build' if args.user_build else 'build3ds/game')
out.mkdir(parents=True,exist_ok=True)
port=root/'port'
source=args.source.resolve() if args.source else port/'platform/3ds/game_main.cpp'
flags=(build/'CMakeFiles/smoke_player.dir/flags.make').read_text()
arguments=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
    arguments+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags,re.M)[1])
entry=out/'game_entry.o'
if args.user_build:arguments+=['-DSM64DS_USER_BUILD=1']
subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*arguments,
                '-c',str(port/'platform/3ds/game_entry.cpp'),'-o',str(entry)],check=True)
command=[sys.executable,str(root/'build3ds/build_menu_probe.py'),
         str(source),
         '--omit-object','CMakeFiles/smoke_player.dir/platform/3ds/main3ds.o',
         '--extra-object',str(entry)]
for symbol in ['func_02019390','func_0203df40','func_02030e4c',
               '_ZN8dScene_c16SpawnIfNecessaryEv','func_ov007_020b7090','func_02013f28',
               'func_0201a96c','func_0201a8b4','_write_r','getenv','setenv','unsetenv','putenv',
               'memcmp']: # castle-min30: word-wise memcmp (runtime_wrappers.cpp)
    command+=['--wrap',symbol]
for replacement in args.replace_link_input:
    command+=['--replace-link-input',replacement]
for symbol in args.wrap:
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*',symbol):
        parser.error('invalid wrapped symbol')
    command+=['--wrap',symbol]
subprocess.run(command,check=True)
for suffix in ['elf','3dsx']:
    shutil.copy2(root/f'build3ds/menu-compile/{source.stem}.{suffix}',out/f'sm64ds.{suffix}')
sources={str(path.relative_to(root) if path.is_relative_to(root) else path):hashlib.sha256(path.read_bytes()).hexdigest()
         for path in [port/'platform/3ds/game_entry.cpp',source,
                      out/'sm64ds.elf',out/'sm64ds.3dsx']}
replacements=[]
for replacement in args.replace_link_input:
    old,_,new=replacement.partition('=')
    path=Path(new).resolve()
    replacements.append({'old':old,'new':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
(out/'build.json').write_text(json.dumps({'sha256':sources,'isolated_link_inputs':replacements,'extra_wrappers':args.wrap,'user_build':args.user_build,'status':'development; gameplay coverage incomplete'},indent=2)+'\n')
print(out/'sm64ds.3dsx')
