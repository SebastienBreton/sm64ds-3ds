#!/usr/bin/env python3
"""Benchmark the existing DS input route with full audio and real stereo draws.

The original menu/door progression is retained. Run with the archived before.sav
from castle-interior-walk: its key was earned naturally in the recorded earlier
rabbit route. This harness does not earn that key from an absent save.
Presentation begins at the
interior control anchor; its first 60 updates warm the renderer, then the same
390-update route is measured. Captures run outside the measured interval.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

ROOT=Path(__file__).resolve().parent.parent
PORT=ROOT/'port'
OUT=ROOT/'build3ds/validation/architecture-castle-stereo-baseline'
OUT.mkdir(parents=True,exist_ok=True)
base=PORT/'tests/smoke_castle_interior_walk.cpp'
source=base.read_text()
(OUT/'original-route.cpp').write_text(source)
for header in ['player_telemetry.h','player_interior_route.h']:
    source=source.replace('"'+header+'"','"'+str(PORT/'tests'/header)+'"')
source='#include "platform/3ds/viewer_3ds.h"\n#include <algorithm>\nbool player_present(bool,bool);\n'+source


def replace(old,new):
    global source
    assert source.count(old)==1,old
    source=source.replace(old,new)


replace('int main(){', '''struct StereoSample {double game,audio,present,total;};
static StereoSample stereo_samples[playerRouteLength];
static unsigned stereo_count=0;
static bool stereo_renderer=false;
static unsigned long long stereo_triangles=0;
static void stereo_report(){
 double game=0,audio=0,present=0,total=0,ordered[playerRouteLength];
 for(unsigned i=0;i<stereo_count;++i){const auto& s=stereo_samples[i];
  game+=s.game;audio+=s.audio;present+=s.present;total+=s.total;ordered[i]=s.total;}
 std::sort(ordered,ordered+stereo_count);
 std::printf("Castle stereo measured%u ms/update: game=%.6f audio=%.6f presentation=%.6f total=%.6f updates_per_second=%.6f p50=%.6f p95=%.6f triangles=%llu\\n",
  stereo_count,game/stereo_count,audio/stereo_count,present/stereo_count,total/stereo_count,
  1000.0*stereo_count/total,ordered[stereo_count/2],ordered[(stereo_count*95)/100],stereo_triangles);
}
int main(){''')
replace(' setvbuf(stdout,nullptr,_IONBF,0);', ''' setvbuf(stdout,nullptr,_IONBF,0);
 setenv("PLAYER_NATIVE_COMPOSITE","1",1);
 setenv("PLAYER_PREPARED_PPU","1",1);
 setenv("PLAYER_PROFILE_GPU","1",1);
 setenv("SM64DS_PROFILE","1",1);''')
replace('   ntr::gx_begin_frame();','   const double stereo_game_start=v3ds_time_ms();\n   ntr::gx_begin_frame();')
replace('   CHECK(sm64ds_native_sound_render(pcm,samples));', '''   const double stereo_audio_start=v3ds_time_ms();
   CHECK(sm64ds_native_sound_render(pcm,samples));
   const double stereo_present_start=v3ds_time_ms();
   if(interiorControlFrame && !returnedMenu && data_0209f2f8==2){
     if(!stereo_renderer){v3ds_renderer_init();stereo_renderer=true;}
     CHECK(ntr::io_read(0x04000304,2)&0x8000);
     CHECK(player_present(false,false));
     CHECK(v3ds_native_stereo());
     const double end=v3ds_time_ms();
     const unsigned elapsed=stageFrames-interiorControlFrame;
     if(elapsed>=playerRouteSettle && elapsed<playerRouteSettle+playerRouteLength){
       CHECK(stereo_count<playerRouteLength);
       stereo_samples[stereo_count++]={stereo_audio_start-stereo_game_start,
         stereo_present_start-stereo_audio_start,end-stereo_present_start,end-stereo_game_start};
       size_t count=0;ntr::gx_polygons(count);stereo_triangles+=count;
     }
   }''')
replace('if(++stageFrames==limit || keyRouteComplete){CHECK(keyRouteComplete);', '''if(++stageFrames==limit || keyRouteComplete){
       CHECK(keyRouteComplete);CHECK(stereo_count==playerRouteLength);stereo_report();
       CHECK(!(ntr::io_read(0x04000064,4)&0x80000000u));
       CHECK(player_present(true,false));CHECK(v3ds_native_stereo());
       std::puts("Castle stereo capture: engineA_upper=1 native_stereo=1 after measured window");
       v3ds_exit();''')
probe=OUT/'fps_castle_stereo_route.cpp'
probe.write_text(source)
with (OUT/'build.log').open('w') as log:
    result=subprocess.run(['python3',str(ROOT/'build3ds/build_menu_probe.py'),str(probe)],
                          stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,(OUT/'build.log')
for ext in ['elf','3dsx']:
    shutil.copy2(ROOT/('build3ds/menu-compile/fps_castle_stereo_route.'+ext),
                 OUT/('fps_castle_stereo_route.'+ext))
paths=[base,probe,OUT/'fps_castle_stereo_route.elf',OUT/'fps_castle_stereo_route.3dsx',
       PORT/'tests/player_interior_route.h',ROOT/'build3ds/cmake/libntr.a',
       ROOT/'build3ds/cmake/libsm64ds_gpu_composite.a',Path(__file__).resolve()]
(OUT/'build.json').write_text(json.dumps({
 'status':'PASS native link; runtime NOT_EXECUTED',
 'scope':'Archived naturally earned-key save, original menu/door input progression,60 renderer warmup updates,390 measured interior route updates with complete audio and actual stereo required',
 'required_input_save':str(ROOT/'build3ds/validation/castle-interior-walk/before.sav'),
 'simulation_rate':'Original30Hz logic; benchmark measures capacity, does not implement60Hz motion',
 'sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}},indent=2)+'\n')
print(OUT/'fps_castle_stereo_route.3dsx')
