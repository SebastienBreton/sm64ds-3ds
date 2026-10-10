#!/usr/bin/env python3
"""Verify a frozen selective GX invalidation candidate without editing GX."""
from pathlib import Path
import hashlib,json,subprocess

ROOT=Path(__file__).resolve().parent.parent
PORT=ROOT/'port'
OUT=ROOT/'build3ds/validation/gx-matrix-invalidation'
OUT.mkdir(parents=True,exist_ok=True)
source=(PORT/'ntr/gx.cpp').read_text()
old='    if(cmd>=0x10 && cmd<=0x1c){g.clip_dirty=true;g.texture_dirty=true;}'
new='''    // MODE/PUSH/STORE retain the active matrices. POP/RESTORE currently
    // restore position in every non-projection mode, including texture mode.
    if(cmd==0x12 || cmd==0x14)g.clip_dirty=true;
    else if(cmd>=0x15 && cmd<=0x1c){
        if(g.mode==MTX_TEX)g.texture_dirty=true;
        else g.clip_dirty=true;
    }'''
assert source.count(old)==1, 'Review changed GX invalidation before rerunning'
(OUT/'gx-baseline.cpp').write_text(source)
(OUT/'gx-candidate.cpp').write_text(source.replace(old,new))
instrumented=source.replace(old,'    if(!matrix_selective){'+old.strip()+'}\n    else {\n'+new+'\n    }')
instrumented=instrumented.replace('if(g.clip_dirty) {','if(g.clip_dirty) { ++clip_rebuilds;')
instrumented=instrumented.replace('if(g.texture_dirty){','if(g.texture_dirty){ ++texture_rebuilds;')
(OUT/'gx-instrumented.cpp').write_text(instrumented)
header='-DGX_IMPLEMENTATION="'+str(OUT/'gx-instrumented.cpp')+'"'
common=['c++','-std=c++17','-O2','-D__3DS__',header,'-I'+str(PORT/'ntr/include'),
        str(PORT/'tests/test_gx_matrix_invalidation.cpp'),str(PORT/'ntr/texture.cpp')]
reports={}
for name,flags in [('release',[]),('sanitized',['-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
    binary=OUT/('probe-'+name)
    subprocess.run([*common,*flags,'-o',str(binary)],check=True)
    run=subprocess.run([str(binary)],text=True,capture_output=True,check=True)
    assert not run.stderr,run.stderr
    print(run.stdout.strip());reports[name]=run.stdout.strip()
subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=gnu++17','-O2',
    '-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-D__3DS__',
    '-I'+str(PORT/'ntr/include'),'-c',str(OUT/'gx-candidate.cpp'),'-o',str(OUT/'gx-candidate.o')],check=True)
(OUT/'report.json').write_text(json.dumps({
    'baseline_sha256':hashlib.sha256(source.encode()).hexdigest(),
    'candidate_sha256':hashlib.sha256((OUT/'gx-candidate.cpp').read_bytes()).hexdigest(),
    'status':'PASS','reports':reports,'arm11_compile':'PASS',
    'scope':'Exact current GX semantics; independent DS oracle and native game timing still required.',
},indent=2)+'\n')
