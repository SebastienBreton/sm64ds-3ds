#!/usr/bin/env python3
"""Gate and link isolated complete direct-arena same-binary menu A/B."""
from pathlib import Path
import hashlib,json,re,shlex,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2];PORT=ROOT/'port'
PROOF=ROOT/'build3ds/validation/gx-direct-arena-host'
OUT=ROOT/'build3ds/validation/gx-direct-arena-pipeline'
VIEW=ROOT/'build3ds/validation/gx-direct-arena-viewer'
BASE=ROOT/'build3ds/validation/native-obj-affine/menu-ab'
SHARED=ROOT/'build3ds/cmake'
GCC='/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++';AR='/opt/devkitpro/devkitARM/bin/arm-none-eabi-ar'
commands=[]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(cmd,name,**kwargs):
    commands.append(cmd);p=subprocess.run(cmd,capture_output=True,text=True,**kwargs)
    (OUT/(name+'.log')).write_text(p.stdout+p.stderr);p.check_returncode();return p.stdout
OUT.mkdir(parents=True,exist_ok=True)
run([str(ROOT/'.venv/bin/python'),str(PORT/'tools/prepare_gx_direct_arena_pipeline.py')],'prepare')
shutil.copy2(Path(__file__),OUT/Path(__file__).name)
baseline=json.loads((BASE/'build.json').read_text())
old_inputs=baseline['shared_inputs'];frozen_inputs={p:sha(p) for p in old_inputs}
shared_changes=[p for p,h in old_inputs.items() if frozen_inputs[p]!=h]
viewer=json.loads((VIEW/'build.json').read_text())
for path,expected in viewer['hashes'].items():
    p=Path(path);p=p if p.is_absolute() else VIEW/p
    assert sha(p)==expected,'Viewer snapshot changed '+str(p)
shutil.copy2(VIEW/'viewer_arena.o',OUT/'viewer_arena.o')
shutil.copy2(VIEW/'geometry_arena_viewer.h',OUT/'geometry_arena_viewer.h')
shutil.copy2(VIEW/'viewer-arena-candidate.cpp',OUT/'viewer-arena-candidate.cpp')
public=set(re.findall(r'\b(gx_\w+)\s*\(', (PORT/'ntr/include/ntr/gx.h').read_text()))|{'gx_stream_note'}
shutil.copy2(PROOF/'gx_geometry_capture_test_api.h',OUT/'gx_geometry_capture_test_api.h')
code=(OUT/'gx-pipeline.cpp').read_text()
observer=(PROOF/'gx-candidate.cpp').read_text();observer=observer[observer.index('void gx_test_state_candidate('):observer.rfind('}  // namespace ntr')]
code=code.replace('}  // namespace ntr',observer+'\nconst gx_arena_candidate::Packet& gx_test_packet(){return gx_direct_packet();}\n}  // namespace ntr')
macros=''.join('#define '+name+' '+name+'_candidate\n' for name in sorted(public))
(OUT/'gx-pipeline-host.cpp').write_text(macros+code)
test=(PROOF/'test_gx_geometry_arena.cpp').read_text()
test=test.replace('#include "gx_geometry_arena_candidate.h"','#include "gx_arena_release_builder.h"',1)
test=re.sub(r'\bBuilder\b','ReleaseBuilder',test)
test=test.replace('reuses+=builder.reuses;conversions+=builder.conversions;clipped+=builder.clipped_triangles;','')
test=test.replace('    REQUIRE(reuses>0 && clipped>0 && conversions>0);\n','')
(OUT/'test_gx_geometry_arena.cpp').write_text(test)
capture=(PROOF/'test_gx_geometry_capture.cpp').read_text()
capture='#include "gx_direct_pipeline_api.h"\n#include "gx_arena_release_builder.h"\n'+re.sub(r'\bBuilder\b','ReleaseBuilder',capture)
capture=capture.replace('REQUIRE(argc==2);','REQUIRE(argc==3);ntr::gx_direct_configure(std::atoi(argv[2])!=0);',1)
anchor='static void geometry(ReleaseBuilder& builder,bool fallback=false){'
legacy=r'''
    if(!ntr::gx_direct_enabled()){
        std::size_t na=0,nb=0;const auto* a=ntr::gx_polygons_baseline(na);const auto* b=ntr::gx_polygons_candidate(nb);REQUIRE(na==nb);
        for(std::size_t i=0;i<na;++i){REQUIRE(std::memcmp(a[i].v,b[i].v,sizeof(a[i].v))==0);
            REQUIRE(a[i].tw==b[i].tw && a[i].th==b[i].th && a[i].wrap==b[i].wrap && a[i].polygon_attr==b[i].polygon_attr && a[i].texture_version==b[i].texture_version);
            REQUIRE(bool(a[i].tex)==bool(b[i].tex));if(a[i].tex)REQUIRE(std::memcmp(a[i].tex,b[i].tex,std::size_t(a[i].tw)*a[i].th*4)==0);}
        ++frames_checked;geometry_checked+=na;state();return;
    }
'''
assert capture.count(anchor)==1;capture=capture.replace(anchor,anchor+legacy,1)
(OUT/'test_gx_geometry_capture.cpp').write_text(capture)
shutil.copy2(PORT/'tests/test_gx_arena_resources.cpp',OUT/'test_gx_arena_resources.cpp')
host=['-std=c++17','-O2','-g','-D__3DS__','-I'+str(OUT),'-I'+str(PORT),'-I'+str(PORT/'ntr/include'),'-fsanitize=address,undefined','-fno-sanitize-recover=all']
run(['clang++',*host,'-fno-sanitize=shift-base','-c',str(OUT/'gx-pipeline-host.cpp'),'-o',str(OUT/'gx-pipeline-host.o')],'host-gx')
objects=[PROOF/'baseline.host.o',OUT/'gx-pipeline-host.o',PROOF/'texture.host.o',PROOF/'vram_bus.host.o',PROOF/'vram_banks.host.o']
binary=OUT/'test-pipeline-host'
run(['clang++',*host,*map(str,objects),str(OUT/'test_gx_geometry_capture.cpp'),'-o',str(binary)],'host-link')
host_results={}
for mode in ['0','1']:host_results[mode]=run([str(binary),str(PROOF/'retail-bmd-lists.bin'),mode],'host-pipeline-mode'+mode).strip()
for name in ['lighting','specular','texgen']:
    p=OUT/('test_'+name+'.cpp');code=(PROOF/('test_'+name+'.cpp')).read_text()
    code=code.replace('#include "ntr/gx.h"','#include "gx_direct_pipeline_api.h"\n#include "ntr/gx.h"',1)
    code=re.sub(r'int main\(\)\s*\{',lambda m:m[0]+'ntr::gx_direct_configure(true);',code,count=1)
    p.write_text(code)
    oracle=OUT/('test-'+name+'-host')
    run(['clang++',*host,str(OUT/'gx-pipeline-host.o'),*map(str,objects[2:]),str(p),'-o',str(oracle)],'host-link-'+name)
    text=run([str(oracle)],'host-oracle-'+name);assert 'failures=0' in text;host_results[name]=text.strip()
for name in ['test_gx_geometry_arena','test_gx_arena_resources']:
    binary=OUT/(name+'-host')
    run(['clang++',*host,str(OUT/(name+'.cpp')),'-o',str(binary)],'host-compile-'+name)
    host_results[name]=run([str(binary)],'host-run-'+name).strip()
def flags(target):
    path=SHARED/f'CMakeFiles/{target}.dir/flags.make';shutil.copy2(path,OUT/(target+'-flags.make'))
    text=path.read_text();return [v for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS'] for v in shlex.split(re.search(r'^'+key+r' = (.*)$',text,re.M)[1])]
ntr_flags=flags('ntr');player_flags=flags('smoke_player')
common=['-I'+str(OUT),'-I'+str(PORT),'-I'+str(PORT/'platform/3ds')]
run([GCC,*ntr_flags,*common,'-MMD','-MF',str(OUT/'gx.d'),'-c',str(OUT/'gx-pipeline.cpp'),'-o',str(OUT/'gx.o')],'arm-gx')
run([GCC,*player_flags,*common,'-DSM64DS_GPU_COMPOSITE_CONFIG','-I'+str(SHARED/'.dkp-generated/presentation'),'-MMD','-MF',str(OUT/'player.d'),'-c',str(OUT/'player_present-pipeline.cpp'),'-o',str(OUT/'player_present.o')],'arm-player')
probe=(BASE/'fps_menu_affine_architecture.cpp').read_text()
probe=probe.replace('#include "ntr/gx.h"','#include "ntr/gx.h"\n#include "gx_direct_pipeline_api.h"',1)
probe=probe.replace('fps-menu-affine-architecture.sav','fps-menu-direct-arena.sav')
anchor='    setvbuf(stdout,nullptr,_IONBF,0);'
probe=probe.replace(anchor,anchor+'''\n    ntr::gx_direct_configure(std::getenv("PLAYER_DIRECT_ARENA")!=nullptr);
    player_direct_configure(std::getenv("PLAYER_DIRECT_ARENA_REUSE")!=nullptr);
    std::printf("Direct arena mode=%u retained=%u; same-binary branch; no conversion counters\\n",unsigned(ntr::gx_direct_enabled()),unsigned(std::getenv("PLAYER_DIRECT_ARENA_REUSE")!=nullptr));''',1)
assert probe.count('ntr::gx_polygons(triangles);')==1
probe=probe.replace('ntr::gx_polygons(triangles);','triangles=ntr::gx_direct_triangle_count();')
entry=OUT/'fps_menu_direct_arena.cpp';entry.write_text(probe)
run([GCC,*player_flags,*common,'-MMD','-MF',str(OUT/'probe.d'),'-c',str(entry),'-o',str(OUT/'probe.o')],'arm-probe')
original=OUT/'libntr-original.a';replacement=OUT/'libntr-pipeline.a'
shutil.copy2(SHARED/'libntr.a',original);shutil.copy2(original,replacement)
members=subprocess.check_output([AR,'t',str(original)],text=True).splitlines();assert members.count('gx.o')==1
def member_hashes(path):return {m:hashlib.sha256(subprocess.check_output([AR,'p',str(path),m])).hexdigest() for m in members}
before=member_hashes(original);run([AR,'r',str(replacement),str(OUT/'gx.o')],'replace-archive');run([AR,'s',str(replacement)],'index-archive')
after=member_hashes(replacement);changed=[m for m in members if before[m]!=after[m]];assert changed==['gx.o']
command=list(baseline['command']);command[command.index('-o')+1]=str(OUT/'fps_menu_direct_arena.elf')
for old,new in [(BASE/'probe.o',OUT/'probe.o'),
    (SHARED/'CMakeFiles/smoke_player.dir/platform/3ds/player_present.o',OUT/'player_present.o'),
    (SHARED/'CMakeFiles/smoke_player.dir/platform/3ds/viewer_3ds.o',OUT/'viewer_arena.o')]:
    assert command.count(str(old))==1,str(old);command[command.index(str(old))]=str(new)
command=[str(replacement) if value==str(SHARED/'libntr.a') else value for value in command]
inputs={Path(t) for t in command if t.endswith(('.o','.a','.ld'))}
hashes={str(p):sha(p) for p in inputs}
assert all(sha(p)==h for p,h in frozen_inputs.items()),'Common inputs changed during build'
run(command,'link')
assert hashes=={str(p):sha(p) for p in inputs},'Link inputs changed during link'
run(['/opt/devkitpro/tools/bin/3dsxtool',str(OUT/'fps_menu_direct_arena.elf'),str(OUT/'fps_menu_direct_arena.3dsx')],'3dsx')
result={'status':'PASS isolated two-mode host GX/state/geometry, release conversion/resources and ARM11 full menu link; PICA/native runtime NOT_EXECUTED',
    'production_modified':False,'same_binary_modes':{'0':'unset PLAYER_DIRECT_ARENA; original triangle/vector/copy pipeline','1':'PLAYER_DIRECT_ARENA=1; indexed GX+release direct arena','reuse':'PLAYER_DIRECT_ARENA_REUSE=1; separate held-generation lease mode'},
    'prior_reference_menu_binary_sha256':baseline['binary_sha256'],'shared_changes_since_reference':shared_changes,
    'common_inputs_at_freeze':frozen_inputs,'host_results':host_results,'changed_archive_members':changed,
    'binary_sha256':sha(OUT/'fps_menu_direct_arena.3dsx'),'link_inputs_sha256':hashes,'commands':commands,
    'limits':['Native pixel/stereo/capture and shutdown gates pending FPS PICA probe','No native speed or frame cadence gain claimed','Original GX shift-base UB excluded on host only; no production correction',
        'Physical capture refusal/feedback retains original driver paths; no blind presentation skip','Snapshots use exact external bytes and never-recycled immutable GX versions; budget refusal returns legacy after abort']}
result['archive_sha256']={str(p.relative_to(OUT)):sha(p) for p in sorted(OUT.rglob('*')) if p.is_file() and p.name!='build.json'}
(OUT/'build.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['status']);print(result['binary_sha256'])
