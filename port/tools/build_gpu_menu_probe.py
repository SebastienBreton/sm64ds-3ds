#!/usr/bin/env python3
"""Compile copied presentation candidate and link a distinct retail menu probe."""
from pathlib import Path
import subprocess
import re
import shlex
import shutil
import argparse

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--tag',default='');parser.add_argument('--reuse-player',type=Path);args=parser.parse_args()
assert re.fullmatch('[a-z0-9-]*',args.tag)
out=root/('build3ds/fps-menu-resident'+('-'+args.tag if args.tag else ''))
out.mkdir(parents=True,exist_ok=True)
platform=Path(__file__).resolve().parents[1] / 'platform/3ds'
subprocess.run(['python3',str(Path(__file__).resolve().parents[1] / 'tools/build_gpu_composite_probe.py'),'--viewer'],check=True)
shader_dir=root/'build3ds/fps-gpu-composite-viewer'
flags=(root/'build3ds/cmake/CMakeFiles/smoke_player.dir/flags.make').read_text()
compile_args=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
    compile_args+=shlex.split(re.search(r'^'+key+r' = (.*)$',flags,re.M)[1])
compile_args+=['-DSM64DS_GPU_COMPOSITE_CANDIDATE','-I'+str(platform),'-I'+str(shader_dir)]
source=(platform/'player_present.cpp').read_text()
anchor='        const double native_start=v3ds_time_ms();\n'
assert source.count(anchor)==1
insert=r'''        if(std::getenv("PLAYER_RESIDENT_COMPOSITE")){
            const auto a=ntr::ppu_prepare_candidates_cached(ntr::ENGINE_A);
            const auto b=ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B);
            static unsigned used=0,fallback=0;
            const bool capture_active=(ntr::io_read(0x04000064,4)&0x80000000u)!=0;
            const unsigned display_mode_a=(ntr::io_read(0x04000000,4)>>16)&3;
            // VRAM scanline feedback and FIFO require their coherent driver.
            if(!capture_active && display_mode_a<2 && v3ds_gpu_present_candidates(vertices.data(),vertices.size(),batches.data(),batches.size(),a,b,a_upper,convergence,true,a_upper)){
                ++used;
                if(used%60==0)std::printf("Resident menu state: used=%u fallback=%u A blend=%04x master=%04x objalpha=%u B blend=%04x master=%04x objalpha=%u capture=%u\n",used,fallback,a.state.blend,a.state.master,unsigned(a.state.has_obj_alpha),b.state.blend,b.state.master,unsigned(b.state.has_obj_alpha),unsigned(capture_active));
                v3ds_profile(0,batch_ms);
                if(capture){
                    v3ds_gpu_candidate_geometry(&geometry_left.px[0][0],&geometry_right.px[0][0]);
                    v3ds_gpu_candidate_panel(0,&engine_left.px[0][0]);
                    v3ds_gpu_candidate_panel(2,&engine_b.px[0][0]);
                    ntr::ppu_write_bmp("player-native-geometry.bmp",geometry_left);
                    ntr::ppu_write_bmp("player-native-upper.bmp",engine_left);
                    ntr::ppu_write_bmp("player-native-bottom.bmp",engine_b);
                    static uint32_t pixels[400*240];
                    v3ds_read_top(pixels,false);write_bmp400("player-gpu-left.bmp",pixels);
                    v3ds_read_top(pixels,true);write_bmp400("player-gpu-right.bmp",pixels);
                }
                return true;
            }
            ++fallback;
            if(fallback==1)std::printf("Resident menu fallback A mode=%u objalpha=%u B mode=%u objalpha=%u capture=%u\n",unsigned((a.state.blend>>6)&3),unsigned(a.state.has_obj_alpha),unsigned((b.state.blend>>6)&3),unsigned(b.state.has_obj_alpha),unsigned(capture_active));
        }
'''
candidate=out/'player_present_resident.cpp'
candidate.write_text(args.reuse_player.read_text() if args.reuse_player else '#include "gpu_composite_viewer.h"\n'+source.replace(anchor,insert+anchor))
objects=[]
for src,name in [(platform/'viewer_3ds.cpp','viewer_resident.o'),(platform/'gpu_composite.cpp','gpu_composite.o'),(candidate,'player_present_resident.o')]:
    obj=out/name
    subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*compile_args,'-c',str(src),'-o',str(obj)],check=True)
    objects.append(obj)
probe=out/('fps_menu_resident'+('_'+args.tag.replace('-','_') if args.tag else '')+'_benchmark.cpp')
probe.write_text((Path(__file__).resolve().parents[1] / 'tests/fps_menu_benchmark.cpp').read_text())
command=['python3',str(root/'build3ds/build_menu_probe.py'),str(probe),
         '--omit-object','CMakeFiles/smoke_player.dir/platform/3ds/viewer_3ds.o',
         '--omit-object','CMakeFiles/smoke_player.dir/platform/3ds/player_present.o']
for obj in objects:command+=['--extra-object',str(obj)]
subprocess.run(command,check=True)
for suffix in ['.o','.elf','.3dsx']:
    src=root/('build3ds/menu-compile/'+probe.stem+suffix)
    shutil.copy2(src,out/src.name)
for file in ['gpu_composite.cpp','gpu_composite.h','gpu_composite.v.pica','gpu_composite_viewer.h','viewer_3ds.cpp']:
    shutil.copy2(platform/file,out/file)
print(out/(probe.stem+'.3dsx'))
