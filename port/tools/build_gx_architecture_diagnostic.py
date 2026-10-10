#!/usr/bin/env python3
"""Freeze a sampled real-menu GX/runtime diagnostic without shared mutations."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shlex
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
PORT=ROOT/'port'
SHARED=ROOT/'build3ds/cmake'
BASE=ROOT/'build3ds/validation/architecture-integrated-baseline'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=ROOT/'build3ds/validation/gx-architecture-diagnostic')
args=parser.parse_args()
OUT=args.output.resolve();OUT.mkdir(parents=True,exist_ok=True)
GCC='/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'
AR='/opt/devkitpro/devkitARM/bin/arm-none-eabi-ar'
commands=[]


def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command,name,**kwargs):
    commands.append(command)
    result=subprocess.run(command,capture_output=True,text=True,**kwargs)
    (OUT/(name+'.log')).write_text(result.stdout+result.stderr)
    result.check_returncode()
    return result.stdout


def flags(target):
    path=SHARED/f'CMakeFiles/{target}.dir/flags.make'
    shutil.copy2(path,OUT/(target+'-flags.make'))
    text=path.read_text()
    return [t for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']
            for t in shlex.split(re.search(r'^'+key+r' = (.*)$',text,re.M)[1])]


def insert(source,anchor,addition):
    assert source.count(anchor)==1,anchor
    return source.replace(anchor,anchor+'\n'+addition)


baseline=json.loads((BASE/'build.json').read_text())
assert digest(SHARED/'libntr.a')==baseline['shared_input_hashes'][str(SHARED/'libntr.a')], 'Shared archive diverged from baseline; coordinate before linking'
assert digest(PORT/'ntr/gx.cpp')==digest(BASE/'gx.cpp')
shutil.copy2(PORT/'tests/gx_architecture_diagnostic.h',OUT)
shutil.copy2(PORT/'tests/gx_architecture_plan.md',OUT)
shutil.copy2(Path(__file__),OUT/Path(__file__).name)
shutil.copy2(PORT/'ntr/include/ntr/fixed_direction_matrix.h',OUT/'fixed_direction_matrix.h')
shutil.copy2('/opt/devkitpro/libctru/include/3ds/os.h',OUT/'libctru-clock-os.h')
assert '#define SYSCLOCK_SOC            (16756991u)' in (OUT/'libctru-clock-os.h').read_text()
source=(PORT/'ntr/gx.cpp').read_text();(OUT/'gx-original.cpp').write_text(source)
source=source.replace('#include "ntr/gx.h"','#include "ntr/gx.h"\n#include "gx_architecture_diagnostic.h"',1)
for anchor,phase in [
    ('Mat mul(const Mat &a, const Mat &b) {','Multiply'),
    ('GxVertex project(int16_t x, int16_t y, int16_t z) {','Projection'),
    ('void lighting_vector(uint32_t packed, int out[3], bool negate) {','Direction'),
    ('void emit_tri(const GxVertex &a, const GxVertex &b, const GxVertex &c) {','TriangleEmission'),
    ('void push_vertex(const GxVertex &v) {','Assembly'),
    ('void bind_from_vram() {','TextureBind'),
    ('void gx_write_port(uint32_t addr, uint32_t value) {','Port')]:
    source=insert(source,anchor,'    gx_diag::Scope diagnostic_scope(gx_diag::'+phase+');')
source=insert(source,'void exec(uint8_t cmd, const uint32_t *p, int np) {',
    '    gx_diag::Scope diagnostic_scope(gx_diag::category(cmd));')
anchor='void gx_write_fifo(uint32_t word) { gx_stream_note(word); feed(word); }'
assert source.count(anchor)==1
source=source.replace(anchor,'''bool gx_diagnostic_decoder_empty() { return g_queued==0; }
void gx_write_fifo(uint32_t word) { gx_diag::Scope diagnostic_scope(gx_diag::Fifo); gx_stream_note(word); feed(word); }''')
(OUT/'gx-diagnostic.cpp').write_text(source)
runtime=(PORT/'ntr/runtime.cpp').read_text();(OUT/'runtime-original.cpp').write_text(runtime)
runtime=runtime.replace('#include "ntr/gx.h"','#include "ntr/gx.h"\n#include "gx_architecture_diagnostic.h"',1)
runtime=insert(runtime,'void copy_words(const uint32_t *src, uint32_t *dst, int n) {',
    '    ntr::gx_diag::bulk(src,dst,n);\n    ntr::gx_diag::Scope diagnostic_scope(ntr::gx_diag::Bulk,reinterpret_cast<uintptr_t>(dst)==GXFIFO);')
(OUT/'runtime-diagnostic.cpp').write_text(runtime)
probe=(BASE/'fps_menu_integrated_baseline.cpp').read_text()
probe=probe.replace('#include "ntr/gx.h"','#include "ntr/gx.h"\n#include "gx_architecture_diagnostic.h"',1)
probe=insert(probe,'    setvbuf(stdout,nullptr,_IONBF,0);','    ntr::gx_diag::configure();')
anchor='CHECK(scene->Behavior()==1);phase_end(frame,2);CHECK(scene->Render()==1);phase_end(frame,3);'
assert probe.count(anchor)==1
probe=probe.replace(anchor,'CHECK(scene->Behavior()==1);phase_end(frame,2);ntr::gx_diag::begin(frame>=840);CHECK(scene->Render()==1);ntr::gx_diag::end();phase_end(frame,3);')
anchor='    CHECK(player_present(true,false));'
assert probe.count(anchor)==1
probe=probe.replace(anchor,'    ntr::gx_diag::dump();\n'+anchor)
entry=OUT/'fps_menu_gx_architecture_diagnostic.cpp';entry.write_text(probe)
ntr_flags=flags('ntr');probe_flags=flags('smoke_player')
for name in ['gx','runtime']:
    run([GCC,*ntr_flags,'-I'+str(OUT),'-MMD','-MF',str(OUT/(name+'.d')),'-c',
        str(OUT/(name+'-diagnostic.cpp')),'-o',str(OUT/(name+'.o'))],'compile-'+name)
run([GCC,*probe_flags,'-I'+str(OUT),'-MMD','-MF',str(OUT/'probe.d'),'-c',str(entry),'-o',str(OUT/'probe.o')],'compile-probe')
original=OUT/'libntr-original.a';replacement=OUT/'libntr-diagnostic.a'
shutil.copy2(SHARED/'libntr.a',original);shutil.copy2(original,replacement)
members=subprocess.check_output([AR,'t',str(original)],text=True).splitlines()
for name in ['gx.o','runtime.o']: assert members.count(name)==1
member_hashes=lambda path:{m:hashlib.sha256(subprocess.check_output([AR,'p',str(path),m])).hexdigest() for m in members}
before=member_hashes(original)
run([AR,'r',str(replacement),str(OUT/'gx.o'),str(OUT/'runtime.o')],'replace-archive')
run([AR,'s',str(replacement)],'index-archive')
after=member_hashes(replacement)
changed=[m for m in members if before[m]!=after[m]]
assert sorted(changed)==['gx.o','runtime.o']
command=shlex.split((BASE/'production-link.txt').read_text())
old_entry='CMakeFiles/smoke_player.dir/tests/smoke_player.o'
assert command.count(old_entry)==1
command[command.index(old_entry)]=str(OUT/'probe.o')
elf=entry.with_suffix('.elf');binary=elf.with_suffix('.3dsx')
command[command.index('-o')+1]=str(elf)
assert command.count('libntr.a')>=1
command=[str(replacement) if t=='libntr.a' else t for t in command]
command=[str(SHARED/t) if not t.startswith('-') and (SHARED/t).is_file() else t for t in command]
boot=(SHARED/'CMakeFiles/sm64ds_sound_boot.dir/build.make').read_text()
extras=[SHARED/m[1] for m in re.finditer(r'^sm64ds_sound_boot: (.+\.o)$',boot,re.M)]
if not any('sm64ds_menu.dir' in t for t in command):extras+=list((ROOT/'build3ds/menu-compile/objects').glob('*.o'))
existing={Path(t).resolve() for t in command if t.endswith('.o')}
command[1:1]=[str(p) for p in extras if p.resolve() not in existing]
inputs={Path(t) for t in command if t.endswith(('.o','.a','.ld'))}
input_hashes={str(p):digest(p) for p in inputs}
for p in inputs:
    if p==replacement or p==OUT/'probe.o':continue
    expected=baseline['shared_input_hashes'].get(str(p))
    assert expected==digest(p), 'Link input differs from frozen baseline: '+str(p)
run(command,'link',cwd=OUT)
assert input_hashes=={str(p):digest(p) for p in inputs}
run(['/opt/devkitpro/tools/bin/3dsxtool',str(elf),str(binary)],'3dsx')
host_flags=['-std=c++17','-O2','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
    '-D__3DS__','-I'+str(OUT),'-I'+str(PORT/'ntr/include')]
host_objects=[]
for name in ['gx','texture','vram_bus','vram_banks']:
    p=OUT/('gx-diagnostic.cpp' if name=='gx' else name+'.cpp')
    if name!='gx':shutil.copy2(PORT/('ntr/'+name+'.cpp'),p)
    obj=OUT/(name+'.host.o');host_objects.append(obj)
    run(['clang++',*host_flags,'-MMD','-MF',str(OUT/(name+'.host.d')),'-c',str(p),'-o',str(obj)],'host-'+name)
host_oracles={}
for oracle in ['lighting','specular','texgen']:
    original=ROOT/('build3ds/reference-ds/flower-lighting/test_'+oracle+'.cpp')
    shutil.copy2(original,OUT/('test_'+oracle+'-original.cpp'))
    code=original.read_text()
    assert code.count('return failures?1:0;')==1
    code=code.replace('return failures?1:0;','for(unsigned i=2;i<8;++i){std::free(ntr::g_region[i]);ntr::g_region[i]=nullptr;}return failures?1:0;')
    # Active timers/counters are diagnostic side effects, tested against the same
    # independent DS numeric goldens. No expectation is derived from profiling.
    code='#include "gx_architecture_diagnostic.h"\n'+code
    code=re.sub(r'int main\(\)\s*\{',lambda m:m[0]+'ntr::gx_diag::enabled=true;ntr::gx_diag::mode=3;',code,count=1)
    assert 'ntr::gx_diag::enabled=true' in code
    p=OUT/('test_'+oracle+'.cpp');p.write_text(code)
    host_binary=OUT/('test-'+oracle+'-host')
    run(['clang++',*host_flags,*map(str,host_objects),str(p),'-o',str(host_binary)],'host-'+oracle+'-build')
    text=run([str(host_binary)],'host-'+oracle)
    assert 'failures=0' in text,text
    host_oracles[oracle]=text.strip()
dependencies={}
for depfile in OUT.glob('*.d'):
    for token in shlex.split(depfile.read_text().replace('\\\n',' ').split(':',1)[1]):
        p=Path(token)
        if p.is_file():dependencies[str(p.resolve())]=digest(p)
report={'status':'PASS isolated ARM11 link and active diagnostic GX host ASan/UBSan oracles; native runtime and cost decomposition NOT_EXECUTED',
    'production_modified':False,'same_binary_modes':True,'changed_archive_members':changed,
    'modes':{'-1':'diagnostic scopes inactive','0':'counters only','1':'GX ingress inclusive',
        '2':'disjoint command categories inclusive','3':'primitive diagnostics inclusive; assembly/emission nested','4':'bulk ingress inclusive'},
    'sampling':'PLAYER_GX_SAMPLE_RATE power-of-two (default64), PLAYER_GX_SAMPLE_OFFSET; startup-only getenv; independent ordinal per category',
    'measurement_limits':'Estimates, not net budgets. Compare offsets/rates and timer overhead. Inclusive hierarchy levels must not be added. No whole-update gain claimed.',
    'baseline_binary_sha256':baseline['artifact_sha256']['fps_menu_integrated_baseline.3dsx'],
    'host_oracles':host_oracles,'commands':commands,'link_inputs_sha256':input_hashes,'dependencies_sha256':dependencies,
    'archive_sha256':{str(p.relative_to(OUT)):digest(p) for p in sorted(OUT.rglob('*')) if p.is_file() and p.name!='build.json'}}
(OUT/'build.json').write_text(json.dumps(report,indent=2)+'\n')
print(report['status']);print(binary);print(digest(binary))
