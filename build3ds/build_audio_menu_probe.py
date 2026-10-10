#!/usr/bin/env python3
"""Same-binary active-channel scan benchmark using the resident menu fixture."""
from pathlib import Path
import hashlib,json,re,shlex,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parent.parent
PORT=ROOT/'port'
SHARED=ROOT/'build3ds/cmake'
OUT=ROOT/'build3ds/validation/audio-active-channels'
RESIDENT=ROOT/'build3ds/fps-menu-resident-v2'
candidate=(OUT/'Player-candidate.cpp').read_text()
candidate=candidate.replace('#include <stdexcept>','#include <stdexcept>\n#include <cstdlib>')
candidate=candidate.replace(' unsigned activeChannels[16], activeCount=0;',
    ' static const bool selective=!std::getenv("PLAYER_LEGACY_AUDIO_CHANNEL_SCAN");\n unsigned activeChannels[16], activeCount=0;')
candidate=candidate.replace('   if(rawPcm[i].enabled || channels[i].state>CS_NONE)',
    '   if(!selective || rawPcm[i].enabled || channels[i].state>CS_NONE)')
(OUT/'Player-toggle.cpp').write_text(candidate)
flags=(SHARED/'CMakeFiles/sm64ds_sseq.dir/flags.make').read_text()
args=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
    args+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags,re.M)[1])
args+=['-I'+str(PORT/'third_party/sseqplayer')]
subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*args,'-c',str(OUT/'Player-toggle.cpp'),'-o',str(OUT/'Player-toggle.o')],check=True)
probe=OUT/'fps_menu_audio_benchmark.cpp'
probe.write_text((RESIDENT/'fps_menu_resident_v2_benchmark.cpp').read_text())
cmd=[sys.executable,str(ROOT/'build3ds/build_menu_probe.py'),str(probe)]
for omit in ['viewer_3ds','player_present']:
    cmd+=['--omit-object','CMakeFiles/smoke_player.dir/platform/3ds/'+omit+'.o']
for obj in [RESIDENT/'viewer_resident.o',RESIDENT/'player_present_resident.o',RESIDENT/'gpu_composite.o',
            OUT/'Player-toggle.o',ROOT/'build3ds/validation/gx-matrix-invalidation/gx-toggle.o']:
    cmd+=['--extra-object',str(obj)]
subprocess.run(cmd,check=True)
for ext in ['elf','3dsx']:
    shutil.copy2(ROOT/('build3ds/menu-compile/'+probe.stem+'.'+ext),OUT/(probe.stem+'.'+ext))
paths=[probe,OUT/'Player-toggle.cpp',OUT/'Player-toggle.o',OUT/(probe.stem+'.elf'),OUT/(probe.stem+'.3dsx'),
       SHARED/'libntr.a',SHARED/'libsm64ds_sseq.a',
       RESIDENT/'viewer_resident.o',RESIDENT/'player_present_resident.o',RESIDENT/'gpu_composite.o',
       ROOT/'build3ds/validation/gx-matrix-invalidation/gx-toggle.o']
(OUT/'menu-build.json').write_text(json.dumps({
    'sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
    'legacy_env':'PLAYER_LEGACY_AUDIO_CHANNEL_SCAN=1',
    'status':'LINKED; native runtime and timings NOT_EXECUTED',
},indent=2)+'\n')
