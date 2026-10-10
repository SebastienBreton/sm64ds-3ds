#!/usr/bin/env python3
"""Compile the isolated native policy oracle, including the signed-shift repair."""
from pathlib import Path
import json,re,shlex,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parent.parent
PORT=ROOT/'port'
OUT=ROOT/'build3ds/validation/audio-active-channels'
SHARED=ROOT/'build3ds/cmake'
candidate=(OUT/'Player-toggle.cpp').read_text()
candidate=candidate.replace(' static const bool selective=!std::getenv("PLAYER_LEGACY_AUDIO_CHANNEL_SCAN");',
    ' extern bool sm64ds_audio_test_selective;\n const bool selective=sm64ds_audio_test_selective;')
(OUT/'Player-policy-oracle.cpp').write_text(candidate)
flags=(SHARED/'CMakeFiles/sm64ds_sseq.dir/flags.make').read_text()
args=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
    args+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags,re.M)[1])
args+=['-I'+str(PORT/'third_party/sseqplayer')]
objects=[]
for src,name in [(OUT/'Player-policy-oracle.cpp','Player-policy-oracle'),(PORT/'third_party/sseqplayer/Channel.cpp','Channel-signed-repair')]:
    obj=OUT/(name+'.o');objects.append(obj)
    subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*args,'-c',str(src),'-o',str(obj)],check=True)
cmd=[sys.executable,str(ROOT/'build3ds/build_menu_probe.py'),str(PORT/'tests/smoke_audio_active_channels.cpp')]
for obj in objects:cmd+=['--extra-object',str(obj)]
subprocess.run(cmd,check=True)
for ext in ['elf','3dsx']:
    shutil.copy2(ROOT/('build3ds/menu-compile/smoke_audio_active_channels.'+ext),OUT/('audio-policy-oracle.'+ext))
print(OUT/'audio-policy-oracle.3dsx')
