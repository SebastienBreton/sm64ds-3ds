#!/usr/bin/env python3
"""Instrument the actual cooperative game entry, without changing game cadence.

Unlike the direct route harness, this counts presentations without a new game
update. Audio remains the real NDSP pump. Times include native waits and the
cooperative handoff; they are Azahar guest-clock estimates, not ARM11 hardware.
"""
from pathlib import Path
import argparse
import subprocess
import sys

ROOT=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,help='Separate artifact directory; keeps earlier profile evidence intact')
args=parser.parse_args()
OUT=args.output.resolve() if args.output else ROOT/'build3ds/validation/interactive-cadence-profile'
OUT.mkdir(parents=True,exist_ok=True)
source=(ROOT/'port/platform/3ds/game_main.cpp').read_text()

def replace(old,new):
    global source
    assert source.count(old)==1,old
    source=source.replace(old,new)

replace(' require_runtime(sm64ds_menu_poll_input(),"local HID transfer");',r'''
 // Optional diagnostic input fixture: the same genuine file-menu touches as
 // the archived castle route. No scene, position or progression writes.
 if(std::getenv("SM64DS_PROFILE_STAGE_INPUT")){
  std::uint16_t touch4[4]{};
  if(updates>=700 && updates<705){touch4[0]=122;touch4[1]=165;touch4[2]=1;}
  if(updates>=900 && updates<905){touch4[0]=44;touch4[1]=70;touch4[2]=1;}
  require_runtime(sm64ds_local_input_publish(0,touch4),"profile file-menu input");
 }else require_runtime(sm64ds_menu_poll_input(),"local HID transfer");''')

replace('static bool frame_hook(uint64_t frame){',r'''
struct CadenceBucket {
 unsigned frames=0,updates=0;double resume=0,audio=0,present=0;
};
static CadenceBucket cadence[2];
static unsigned cadence_last_updates=0,cadence_scene=~0u,cadence_warm=0,cadence_count=0;
static double cadence_exit=0;
static bool frame_hook(uint64_t frame){
 const double cadence_start=v3ds_time_ms();
 const unsigned cadence_delta=updates-cadence_last_updates;
 cadence_last_updates=updates;
 if(cadence_scene!=current_scene){
  cadence_scene=current_scene;cadence_warm=0;cadence_count=0;
  cadence[0]=CadenceBucket{};cadence[1]=CadenceBucket{};
 }
 const double cadence_resume=cadence_exit?cadence_start-cadence_exit:0;
 const double cadence_audio_start=v3ds_time_ms();''')
replace(' const char* capture=std::getenv("SM64DS_GAME_CAPTURE_FRAME");',
''' const double cadence_audio_end=v3ds_time_ms();
 const char* capture=std::getenv("SM64DS_GAME_CAPTURE_FRAME");''')
# Include the common cadence wait in measured presentation time. Otherwise the
# timer would omit the wait between present_end and cadence_exit.
presentation=' if(!player_present(capturing,false)){failed=true;return false;}'
observed_wait='\n observed_cadence::after_frame(frame);'
has_observed_wait=presentation+observed_wait in source
if has_observed_wait:
    replace(presentation+observed_wait,presentation)
replace(presentation,
''' const double cadence_present_start=v3ds_time_ms();
 if(!player_present(capturing,false)){failed=true;return false;}
 const double cadence_present_end=v3ds_time_ms();
 if(++cadence_warm>120 && !capturing){
  auto& bucket=cadence[cadence_delta?1:0];
  ++bucket.frames;bucket.updates+=cadence_delta;
  bucket.resume+=cadence_resume;
  bucket.audio+=cadence_audio_end-cadence_audio_start;
  bucket.present+=cadence_present_end-cadence_present_start;
  if(++cadence_count==240){
   for(unsigned kind=0;kind<2;++kind){
    const auto& b=cadence[kind];const double n=b.frames?b.frames:1;
    std::printf("Interactive cadence scene=%u kind=%s vblanks=%u updates=%u mean_ms resume=%.6f audio_pump=%.6f present=%.6f total=%.6f\\n",
     cadence_scene,kind?"new-update":"held-update",b.frames,b.updates,
     b.resume/n,b.audio/n,b.present/n,(b.resume+b.audio+b.present)/n);
   }
   cadence[0]=CadenceBucket{};cadence[1]=CadenceBucket{};cadence_count=0;
  }
 }''')
replace(' if(frame%120==0)std::printf("Game vblank=%llu updates=%u scene=%u\\n",(unsigned long long)frame,updates,current_scene);\n return true;',
''' if(frame%120==0)std::printf("Game vblank=%llu updates=%u scene=%u\\n",(unsigned long long)frame,updates,current_scene);
 cadence_exit=v3ds_time_ms();
 return true;''')
if has_observed_wait:
    replace(' const double cadence_present_end=v3ds_time_ms();',
            ' observed_cadence::after_frame(frame);\n const double cadence_present_end=v3ds_time_ms();')

path=OUT/'game_cadence_profile.cpp'
path.write_text(source)
with (OUT/'build.log').open('w') as log:
    subprocess.run([sys.executable,str(ROOT/'build3ds/build_game.py'),
                    '--source',str(path),'--output',str(OUT)],
                   stdout=log,stderr=subprocess.STDOUT,check=True)
print(OUT/'sm64ds.3dsx')
