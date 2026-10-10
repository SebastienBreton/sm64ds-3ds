#!/usr/bin/env python3
"""Isolated GX indexed-output differential; never mutate shared GX/CMake."""
from pathlib import Path
import hashlib,json,re,shlex,shutil,struct,subprocess
import ndspy.lz10
ROOT=Path(__file__).resolve().parents[2]
PORT=ROOT/'port'
OUT=ROOT/'build3ds/validation/gx-direct-arena-host'
OUT.mkdir(parents=True,exist_ok=True)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
commands=[]
def run(cmd,name):
    commands.append(cmd)
    p=subprocess.run(cmd,capture_output=True,text=True)
    (OUT/(name+'.log')).write_text(p.stdout+p.stderr)
    p.check_returncode();return p.stdout
source=(PORT/'ntr/gx.cpp').read_text();(OUT/'gx-original.cpp').write_text(source)
for name in ['gx_geometry_arena_candidate.h','gx_geometry_capture_test_api.h','test_gx_geometry_arena.cpp','test_gx_geometry_capture.cpp','test_gx_arena_allocator.cpp']:
    shutil.copy2(PORT/'tests'/name,OUT/name)
shutil.copy2(Path(__file__),OUT/Path(__file__).name)
public=set(re.findall(r'\b(gx_\w+)\s*\(', (PORT/'ntr/include/ntr/gx.h').read_text()))|{'gx_stream_note'}
# Numeric/state observer only: explicit fields and canonical texture bytes.
# It excludes the intentionally different output representation, allocators,
# addresses and the private numeric-result-equivalent direction cache.
observer=r'''
void gx_test_state_VARIANT(std::vector<unsigned char>& output){
    output.clear();
    auto add=[&](const auto& value){const auto* p=reinterpret_cast<const unsigned char*>(&value);output.insert(output.end(),p,p+sizeof(value));};
    auto sequence=[&](const auto& values){uint64_t count=values.size();add(count);for(const auto& v:values)add(v);};
    auto texture=[&](const uint32_t* p,int w,int h){bool bound=p!=nullptr;add(bound);if(bound)for(int i=0;i<w*h;++i)add(p[i]);};
    add(g.mode);add(g.proj);add(g.pos);add(g.vec);add(g.tex);add(g.clip);add(g.clip_dirty);
    add(g.proj_stack);add(g.pos_stack);add(g.vec_stack);add(g.proj_sp);add(g.pos_sp);
    add(g.color);add(g.u);add(g.v);add(g.raw_texcoord);add(g.texture_fixed);add(g.texture_dirty);
    add(g.tw);add(g.th);add(g.texture_version);add(g.wrap);add(g.texmode);texture(g.tex_rgba,g.tw,g.th);
    add(g.prim);add(g.vx);add(g.vy);add(g.vz);sequence(g.strip);add(g.strip_parity);
    add(g.vp_x);add(g.vp_y);add(g.vp_w);add(g.vp_h);
    add(g.diffuse);add(g.ambient);add(g.emission);add(g.specular);add(g.use_shininess);add(g.shininess);
    for(const auto& l:g.lights){add(l.dx);add(l.dy);add(l.dz);add(l.r);add(l.g);add(l.b);add(l.spec_recip);}
    add(g.light_mask);add(g.polygon_attr);add(g.active_polygon_attr);add(g_store_count);
    add(g_queue);add(g_queued);add(g_qpos);add(g_params);add(g_have);
    add(g_port_cmd);add(g_port_params);add(g_port_have);add(g_stream_hash);
    add(g_teximage);add(g_plttbase);add(g_texture_frame);add(g_texture_version);add(g_texture_bytes);
    uint64_t entries=g_vram_tex_cache.size();add(entries);
    for(const auto& entry:g_vram_tex_cache){add(entry.first);const auto& c=entry.second;
        sequence(c.pixels);sequence(c.data);sequence(c.index);sequence(c.palette);add(c.palette_offset);add(c.used_frame);add(c.version);}
}
'''
candidate=source.replace('#include "ntr/gx.h"','#include "ntr/gx.h"\n#include "gx_geometry_arena_candidate.h"',1)
candidate=candidate.replace('State g;','State g;\ngx_arena_candidate::Packet g_packet;\nHostVector<uint32_t> g_strip_ids;',1)
start=candidate.index('void emit_tri(');end=candidate.index('// Assemble according',start)
candidate=candidate[:start]+'''void emit_tri(const GxVertex &a,const GxVertex &b,const GxVertex &c){
    const auto* base=g.strip.data();
    g_packet.triangle(g_strip_ids.at(&a-base),g_strip_ids.at(&b-base),g_strip_ids.at(&c-base),
        {g.tex_rgba,g.tw,g.th,g.wrap,g.active_polygon_attr,g.texture_version});
}

'''+candidate[end:]
assert candidate.count('g.strip.push_back(v);')==1
candidate=candidate.replace('g.strip.push_back(v);','g.strip.push_back(v);g_strip_ids.push_back(g_packet.vertex(v));')
candidate=candidate.replace('g.strip.clear();','g.strip.clear();g_strip_ids.clear();')
candidate=candidate.replace('g.tris.clear();++g_texture_frame;','''g.tris.clear();g_packet.clear();g_strip_ids.clear();
    // An unfinished strip is DS state and survives frame boundaries.
    for(const auto& v:g.strip)g_strip_ids.push_back(g_packet.vertex(v));
    ++g_texture_frame;''',1)
candidate=candidate.replace('g = State{};','g = State{};g_packet.clear();g_strip_ids.clear();',1)
candidate=candidate.replace('const GxTriangle *gx_polygons(size_t &count) {','const GxTriangle *gx_polygons(size_t &count) {\n    g_packet.reconstruct(g.tris);',1)
candidate=candidate.replace('if (count) g.tris.insert(g.tris.end(), triangles, triangles + count);','''for(size_t i=0;i<count;++i){const auto& t=triangles[i];
        uint32_t ids[3];for(unsigned k=0;k<3;++k)ids[k]=g_packet.vertex(t.v[k]);
        g_packet.triangle(ids[0],ids[1],ids[2],gx_arena_candidate::Material::from(t));
    }''',1)
candidate=candidate.replace('void gx_render(Framebuffer &fb) {','void gx_render(Framebuffer &fb) {\n    g_packet.reconstruct(g.tris);',1)
closing='}  // namespace ntr'
assert candidate.count(closing)==1 and source.count(closing)==1
candidate=candidate.replace(closing,'const gx_arena_candidate::Packet& gx_test_packet(){return g_packet;}\n'+observer.replace('VARIANT','candidate')+closing)
baseline=source.replace(closing,observer.replace('VARIANT','baseline')+closing)
for label,code in [('baseline',baseline),('candidate',candidate)]:
    macros=''.join('#define '+name+' '+name+'_'+label+'\n' for name in sorted(public))
    (OUT/('gx-'+label+'.cpp')).write_text(macros+code)

# Independent file-relative BMD table extraction; owned immutable fixture words.
records=[];skipped=[];blob=bytearray()
for path in sorted((ROOT/'sm64ds-decomp/extracted/dsd/files/data').rglob('*.bmd')):
    raw=path.read_bytes()
    try:
        data=ndspy.lz10.decompress(raw[4:]) if raw.startswith(b'LZ77') else raw
        if len(data)<0x30:raise ValueError('short header')
        u32=lambda off:struct.unpack_from('<I',data,off)[0]
        groups,table=u32(0xc),u32(0x10)
        if groups>4096 or table+8*groups>len(data):raise ValueError('invalid group table')
        for gi in range(groups):
            count,ptr=u32(table+8*gi),u32(table+8*gi+4)
            if count>65536 or ptr+16*count>len(data):raise ValueError('invalid list table')
            for k in range(count):
                size,offset=u32(ptr+16*k+8),u32(ptr+16*k+12)
                if not size or not offset:continue
                if size%4 or offset+size>len(data):raise ValueError('invalid display-list span')
                words=data[offset:offset+size]
                blob+=struct.pack('<I',size//4)+words
                records.append({'path':str(path.relative_to(ROOT)),'group':gi,'list':k,'offset':offset,'words':size//4,
                    'file_sha256':sha(path),'decoded_sha256':hashlib.sha256(data).hexdigest(),'words_sha256':hashlib.sha256(words).hexdigest()})
    except (ValueError,struct.error) as e:skipped.append({'path':str(path.relative_to(ROOT)),'reason':str(e)})
fixture=OUT/'retail-bmd-lists.bin';fixture.write_bytes(struct.pack('<I',len(records))+blob)
(OUT/'retail-bmd-lists.json').write_text(json.dumps({'lists':records,'skipped':skipped},indent=2)+'\n')
base_flags=['-std=c++17','-O2','-g','-D__3DS__','-I'+str(OUT),'-I'+str(PORT),'-I'+str(PORT/'ntr/include'),
    '-fsanitize=address,undefined','-fno-sanitize-recover=all']
objects=[]
for name in ['baseline','candidate','texture','vram_bus','vram_banks']:
    p=OUT/('gx-'+name+'.cpp') if name in ['baseline','candidate'] else PORT/'ntr'/(name+'.cpp')
    obj=OUT/(name+'.host.o');objects.append(str(obj))
    # Legacy GX VTX_10 sign-extends then left shifts negative signed numbers.
    # This known original shift-base UB is excluded, not claimed corrected.
    extra=['-fno-sanitize=shift-base'] if name in ['baseline','candidate'] else []
    run(['clang++',*base_flags,*extra,'-MMD','-MF',str(OUT/(name+'.host.d')),'-c',str(p),'-o',str(obj)],'compile-'+name)
run(['clang++',*base_flags,*objects,str(OUT/'test_gx_geometry_capture.cpp'),'-o',str(OUT/'test-capture-host')],'link-host')
output=run([str(OUT/'test-capture-host'),str(fixture)],'host-capture')
oracles={}
for name in ['lighting','specular','texgen']:
    original=ROOT/'build3ds/reference-ds/flower-lighting'/('test_'+name+'.cpp')
    shutil.copy2(original,OUT/('test_'+name+'-original.cpp'))
    code=original.read_text()
    assert code.count('return failures?1:0;')==1
    code=code.replace('return failures?1:0;','for(unsigned i=2;i<8;++i){std::free(ntr::g_region[i]);ntr::g_region[i]=nullptr;}return failures?1:0;')
    code=''.join('#define '+function+' '+function+'_candidate\n' for function in sorted(public))+code
    p=OUT/('test_'+name+'.cpp');p.write_text(code)
    oracle=OUT/('test-'+name+'-host')
    run(['clang++',*base_flags,str(OUT/'candidate.host.o'),*objects[2:],str(p),'-o',str(oracle)],'link-oracle-'+name)
    result=run([str(oracle)],'host-oracle-'+name)
    assert 'failures=0' in result
    oracles[name]=result.strip()
synthetic=OUT/'test-arena-host'
run(['clang++',*base_flags,str(OUT/'test_gx_geometry_arena.cpp'),'-o',str(synthetic)],'compile-synthetic')
synthetic_output=run([str(synthetic)],'host-synthetic')
allocator=OUT/'test-allocator-host'
run(['clang++',*base_flags,str(OUT/'test_gx_arena_allocator.cpp'),'-o',str(allocator)],'compile-allocator')
allocator_output=run([str(allocator)],'host-allocator')
# Compile the actual isolated indexed engine and conversion on ARM11. No link
# against the common archive, no emulator access or runtime claim here.
flags_text=(ROOT/'build3ds/cmake/CMakeFiles/ntr.dir/flags.make').read_text()
flags=[token for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS'] for token in shlex.split(re.search(r'^'+key+r' = (.*)$',flags_text,re.M)[1])]
for name in ['gx-candidate.cpp','test_gx_geometry_arena.cpp','test_gx_geometry_capture.cpp','test_gx_arena_allocator.cpp']:
    run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*flags,'-I'+str(OUT),'-I'+str(PORT),'-MMD','-MF',str(OUT/(name+'.arm.d')),'-c',str(OUT/name),'-o',str(OUT/(name+'.arm.o'))],'arm-'+name)
layout=OUT/'layout.cpp';layout.write_text('#include "gx_geometry_arena_candidate.h"\nextern "C" { extern const unsigned gx_arena_layout[]={sizeof(ntr::GxVertex),sizeof(ntr::GxTriangle),sizeof(ntr::gx_arena_candidate::Triangle),sizeof(V3dsVert),sizeof(V3dsBatch)}; }\n')
run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',*flags,'-I'+str(OUT),'-I'+str(PORT),'-c',str(layout),'-o',str(OUT/'layout.arm.o')],'arm-layout')
run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-objcopy','-O','binary','-j','.rodata',str(OUT/'layout.arm.o'),str(OUT/'layout.arm.bin')],'arm-layout-extract')
sizes=dict(zip(['GxVertex','GxTriangle','IndexedTriangle','V3dsVert','V3dsBatch'],struct.unpack('<5I',(OUT/'layout.arm.bin').read_bytes())))
dependencies={}
for p in OUT.glob('*.d'):
    for token in shlex.split(p.read_text().replace('\\\n',' ').split(':',1)[1]):
        q=Path(token)
        if q.is_file():dependencies[str(q.resolve())]=sha(q)
report={'status':'PASS isolated host differential and ARM11 compilation; native runtime NOT_EXECUTED',
    'production_modified':False,'gx_source_sha256':hashlib.sha256(source.encode()).hexdigest(),
    'retail_bmd_lists':len(records),'retail_bmd_words':sum(r['words'] for r in records),'skipped_files':skipped,
    'host_capture':output.strip(),'host_synthetic':synthetic_output.strip(),'host_allocator':allocator_output.strip(),'independent_ds_oracles':oracles,
    'arm11_type_bytes':sizes,'header_sha256':sha(OUT/'gx_geometry_arena_candidate.h'),
    'sanitizers':'ASan + UBSan; original GX objects exclude pre-existing negative VTX_10 shift-base UB only',
    'limits':['No physical 3DS runtime, PICA lease/lifetime or pixel proof yet','Retail command lists use synthetic scene matrices/materials; not a complete true-menu replay',
        'State observer excludes allocation/addresses, direction result cache and opaque physical texture-view internals; their numerical/resource outputs remain observed',
        'No inter-frame transformed geometry reuse or GX command-list cache exercised','No native speed claim'],
    'commands':commands,'dependencies_sha256':dependencies}
report['archive_sha256']={str(p.relative_to(OUT)):sha(p) for p in sorted(OUT.rglob('*')) if p.is_file() and p.name!='report.json'}
assert hashlib.sha256(source.encode()).hexdigest()==sha(PORT/'ntr/gx.cpp'),'Shared GX changed during isolated proof'
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(report['status']);print(output.strip());print('retail lists',len(records),'words',report['retail_bmd_words'])
