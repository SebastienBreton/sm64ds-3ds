#!/usr/bin/env python3
"""Frozen mixer experiment; skip channels inactive until the next sequencer tick."""
from pathlib import Path
import hashlib,json,shutil,subprocess
from ndspy.soundArchive import SDAT
ROOT=Path(__file__).resolve().parent.parent
PORT=ROOT/'port'
OUT=ROOT/'build3ds/validation/audio-active-channels'
OUT.mkdir(parents=True,exist_ok=True)
source=(PORT/'third_party/sseqplayer/Player.cpp').read_text()
begin='\tfor (unsigned smpl = 0; smpl < samples; ++smpl)'
active=''' // Channel starts are published by the sequencer clock. A sample can
 // stop a channel but cannot start another; keep original ascending order.
 unsigned activeChannels[16], activeCount=0;
 auto refreshActive=[&](){
  activeCount=0;
  for(unsigned i=0;i<16;++i)
   if(rawPcm[i].enabled || channels[i].state>CS_NONE)activeChannels[activeCount++]=i;
 };
 refreshActive();
'''
loop='\t\tfor (int i = 0; i < 16; ++i)\n\t\t{\n\t\t\tChannel &chn = this->channels[i];'
newloop='\t\tfor (unsigned active=0; active<activeCount; ++active)\n\t\t{\n            const unsigned i=activeChannels[active];\n\t\t\tChannel &chn = this->channels[i];'
clock='   for(unsigned i=0;i<count;++i)players[i]->Run();'
assert source.count(begin)==source.count(loop)==source.count(clock)==1
candidate=source.replace(begin,active+begin).replace(loop,newloop).replace(clock,clock+'\n   refreshActive();')
(OUT/'Player-baseline.cpp').write_text(source)
(OUT/'Player-candidate.cpp').write_text(candidate)
executables={}
for variant,text in [('baseline',source),('candidate',candidate)]:
    tree=OUT/variant
    shutil.copytree(PORT/'third_party/sseqplayer',tree/'third_party/sseqplayer',dirs_exist_ok=True)
    (tree/'audio').mkdir(exist_ok=True)
    for path in (PORT/'audio').glob('*.h'):shutil.copy2(path,tree/'audio'/path.name)
    (tree/'third_party/sseqplayer/Player.cpp').write_text(text)
    exe=OUT/('probe-'+variant)
    subprocess.run(['c++','-std=c++17','-O2','-I'+str(tree/'third_party/sseqplayer'),
        str(PORT/'tests/host_audio_active_channels.cpp'),
        *map(str,sorted((tree/'third_party/sseqplayer').glob('*.cpp'))),'-o',str(exe)],check=True)
    executables[variant]=exe
    pool_probe=OUT/('pool-'+variant)
    subprocess.run(['c++','-std=c++17','-O2','-DPlayer=Sm64dsNitroPlayer',
        '-DTrack=Sm64dsNitroTrack','-DChannel=Sm64dsNitroChannel','-I'+str(PORT),
        str(PORT/'tests/smoke_sseq_pool.cpp'),
        *map(str,sorted((tree/'third_party/sseqplayer').glob('*.cpp'))),'-o',str(pool_probe)],check=True)
    pool_run=subprocess.run([str(pool_probe)],check=True,text=True,capture_output=True)
    (OUT/('pool-'+variant+'.txt')).write_text(pool_run.stdout+pool_run.stderr)
rom=PORT.parent/'extracted/dsd/files/data/sound_data.sdat'
sdat=SDAT.fromFile(str(rom))
fixtures=[(i,0,32768*5) for i,(name,seq) in enumerate(sdat.sequences) if seq is not None]
fixtures += [(55,mode,16384) for mode in range(1,5)]
records=[]
for ident,mode,frames in fixtures:
    row={'sequence':ident,'interpolation':mode,'frames':frames}
    for variant,exe in executables.items():
        prefix=OUT/(variant+'-current')
        subprocess.run([str(exe),str(rom),str(ident),str(mode),str(prefix),str(frames)],
                       check=True,capture_output=True,text=True,timeout=30)
        row[variant]={ext:hashlib.sha256(prefix.with_suffix('.'+ext).read_bytes()).hexdigest() for ext in ['pcm','state']}
    assert row['baseline']==row['candidate'],row
    records.append(row)
    print(f'PASS sequence{ident} interpolation{mode}',flush=True)
tree=OUT/'candidate'
sanitized=OUT/'probe-candidate-sanitized'
subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-fno-omit-frame-pointer',
    '-I'+str(tree/'third_party/sseqplayer'),str(PORT/'tests/host_audio_active_channels.cpp'),
    *map(str,sorted((tree/'third_party/sseqplayer').glob('*.cpp'))),'-o',str(sanitized)],check=True)
san=subprocess.run([str(sanitized),str(rom),'55','0',str(OUT/'sanitized-current'),'163840'],
                   check=True,capture_output=True,text=True,timeout=30)
assert not san.stderr,san.stderr
(OUT/'report.json').write_text(json.dumps({
    'status':'PASS','scope':'baseline/candidate PCM and portable channel/clock/capture state; not independent DS audio oracle',
    'baseline_sha256':hashlib.sha256(source.encode()).hexdigest(),
    'candidate_sha256':hashlib.sha256(candidate.encode()).hexdigest(),
    'sdat_sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'fixtures':records,
    'production':'UNCHANGED','native_runtime':'NOT_EXECUTED',
    'shared_pool_regression':'baseline and candidate PASS; ownership, stop/steal/tails, pause/resume, mute/channel masks, globals, ticks',
    'sanitized_title':'ASan/UBSan PASS',
},indent=2)+'\n')
print(f'PASS {len(records)} sequences/interpolation fixtures; native timing remains unmeasured')
