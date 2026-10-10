#!/usr/bin/env python3
"""Link the frozen resident-menu probe with a same-binary GX policy switch."""
from pathlib import Path
import hashlib,json,re,shlex,subprocess,sys
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build3ds/validation/gx-matrix-invalidation'
SHARED=ROOT/'build3ds/cmake'
RESIDENT=ROOT/'build3ds/fps-menu-resident-v2'
PORT=ROOT/'port'
source=(OUT/'gx-baseline.cpp').read_text()
candidate=(OUT/'gx-candidate.cpp').read_text()
old='    if(cmd>=0x10 && cmd<=0x1c){g.clip_dirty=true;g.texture_dirty=true;}'
start=candidate.index('    // MODE/PUSH/STORE retain')
end=candidate.index('    (void)np;',start)
replacement=candidate[start:end]
toggle='''    static const bool selective=!std::getenv("PLAYER_LEGACY_MATRIX_INVALIDATION");
    if(!selective){'''+old.strip()+'''}
    else {
'''+replacement+'''    }
'''
assert source.count(old)==1
(OUT/'gx-toggle.cpp').write_text(source.replace(old,toggle))
flags=(SHARED/'CMakeFiles/smoke_player.dir/flags.make').read_text()
args=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
    args+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags,re.M)[1])
subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*args,'-c',str(OUT/'gx-toggle.cpp'),'-o',str(OUT/'gx-toggle.o')],check=True)
probe=OUT/'fps_menu_matrix_benchmark.cpp'
probe.write_text((RESIDENT/'fps_menu_resident_v2_benchmark.cpp').read_text())
cmd=[sys.executable,str(ROOT/'build3ds/build_menu_probe.py'),str(probe)]
for omit in ['viewer_3ds','player_present']:
    cmd+=['--omit-object','CMakeFiles/smoke_player.dir/platform/3ds/'+omit+'.o']
for obj in [RESIDENT/'viewer_resident.o',RESIDENT/'player_present_resident.o',RESIDENT/'gpu_composite.o',OUT/'gx-toggle.o']:
    cmd+=['--extra-object',str(obj)]
subprocess.run(cmd,check=True)
import shutil
for ext in ['elf','3dsx']:
    shutil.copy2(ROOT/('build3ds/menu-compile/'+probe.stem+'.'+ext),OUT/(probe.stem+'.'+ext))
paths=[probe,OUT/'gx-toggle.cpp',OUT/'gx-toggle.o',OUT/(probe.stem+'.elf'),OUT/(probe.stem+'.3dsx'),
       SHARED/'libntr.a',SHARED/'libsm64ds_sseq.a',
       RESIDENT/'viewer_resident.o',RESIDENT/'player_present_resident.o',RESIDENT/'gpu_composite.o']
(OUT/'menu-build.json').write_text(json.dumps({
    'sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
    'legacy_env':'PLAYER_LEGACY_MATRIX_INVALIDATION=1',
    'status':'LINKED; native runtime and timings NOT_EXECUTED',
},indent=2)+'\n')
