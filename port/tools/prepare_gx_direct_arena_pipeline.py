#!/usr/bin/env python3
"""Prepare a same-binary GX/backend A/B on frozen copies, no common build."""
from pathlib import Path
import hashlib,json,re,shutil
ROOT=Path(__file__).resolve().parents[2]
PORT=ROOT/'port';TESTS=PORT/'tests'
PROOF=ROOT/'build3ds/validation/gx-direct-arena-host'
OUT=ROOT/'build3ds/validation/gx-direct-arena-pipeline'
OUT.mkdir(parents=True,exist_ok=True)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
report=json.loads((PROOF/'report.json').read_text())
assert all(sha(PROOF/n)==h for n,h in report['archive_sha256'].items())
assert sha(PORT/'ntr/gx.cpp')==report['gx_source_sha256']
for name in ['gx_geometry_arena_candidate.h','gx_arena_resources_candidate.h','gx_direct_pipeline_api.h']:
    shutil.copy2(TESTS/name,OUT/name)
original=(PROOF/'gx-original.cpp').read_text()
candidate=(PROOF/'gx-candidate.cpp').read_text()
candidate=re.sub(r'^#define gx_\w+ gx_\w+_candidate\n','',candidate,flags=re.M)
observer_start=candidate.index('void gx_test_state_candidate(')
candidate=candidate[:observer_start]+'}  // namespace ntr\n'
candidate=candidate.replace('const gx_arena_candidate::Packet& gx_test_packet(){return g_packet;}','''void gx_direct_configure(bool enabled){g_direct=enabled;}
bool gx_direct_enabled(){return g_direct;}
const gx_arena_candidate::Packet& gx_direct_packet(){return g_packet;}
size_t gx_direct_triangle_count(){return g_direct?g_packet.triangles().size():g.tris.size();}''')
candidate=candidate.replace('State g;','State g;\nbool g_direct=false;',1)
start=original.index('void emit_tri(');end=original.index('// Assemble according',start)
legacy=original[start:end]
legacy_body=legacy[legacy.index('{')+1:legacy.rfind('}')]
anchor='void emit_tri(const GxVertex &a,const GxVertex &b,const GxVertex &c){'
assert candidate.count(anchor)==1
candidate=candidate.replace(anchor,anchor+'\n    if(!g_direct){'+legacy_body+'\n        return;\n    }',1)
candidate=candidate.replace('g.strip.push_back(v);g_strip_ids.push_back(g_packet.vertex(v));','g.strip.push_back(v);if(g_direct)g_strip_ids.push_back(g_packet.vertex(v));',1)
candidate=candidate.replace('for(const auto& v:g.strip)g_strip_ids.push_back(g_packet.vertex(v));','if(g_direct)for(const auto& v:g.strip)g_strip_ids.push_back(g_packet.vertex(v));',1)
candidate=candidate.replace('g_packet.reconstruct(g.tris);','if(g_direct)g_packet.reconstruct(g.tris);')
candidate=candidate.replace('void gx_append_polygons(const GxTriangle *triangles, size_t count) {','''void gx_append_polygons(const GxTriangle *triangles, size_t count) {
    if(!g_direct){if(count)g.tris.insert(g.tris.end(),triangles,triangles+count);return;}''',1)
(OUT/'gx-pipeline.cpp').write_text(candidate)
shutil.copy2(PORT/'platform/3ds/player_present.cpp',OUT/'player_present-original.cpp')
driver=(OUT/'player_present-original.cpp').read_text()
legacy_driver=driver.replace('bool player_present(bool capture, bool perspective) {','static bool player_present_legacy(bool capture, bool perspective) {',1)
tail=driver[driver.index('    if(!probe && std::getenv("PLAYER_NATIVE_COMPOSITE")){'):]
tail=tail[:tail.index('    if(auto* ds_capture=ntr::io_display_capture()){')]
tail=tail.replace('v3ds_gpu_geometry(vertices.data(),vertices.size(),batches.data(),batches.size(),','v3ds_arena::geometry(prepared,')
tail=tail.replace('v3ds_gpu_present_candidates(vertices.data(),vertices.size(),batches.data(),batches.size(),','v3ds_arena::present_candidates(prepared,')
assert 'vertices.data()' not in tail and 'batches.data()' not in tail
tail=tail.replace('                return true;','                guard.keep=true;return true;')
tail=tail.replace('        return true;','        guard.keep=true;return true;')
prefix=r'''
#include "geometry_arena_viewer.h"
#include "gx_direct_pipeline_api.h"
#include "gx_arena_release_builder.h"
#include "gx_arena_resources_candidate.h"
namespace {
bool direct_reuse=false;
bool direct_ready=false;
const ntr::gx_arena_candidate::Packet* direct_source=nullptr;
ntr::gx_arena_candidate::Prepared direct_prepared{};
float direct_convergence=1.f;
ntr::HostVector<V3dsBatch> direct_batches;
ntr::HostVector<float> direct_depths;
ntr::gx_arena_candidate::ReleaseBuilder direct_builder;
ntr::gx_arena_candidate::Resources direct_resources;
struct DirectGuard {uint64_t token;bool keep=false;~DirectGuard(){if(!keep){v3ds_arena::abort(token);direct_ready=false;}}};
}
void player_direct_configure(bool reuse_between_presentations){direct_reuse=reuse_between_presentations;direct_ready=false;}
bool player_present(bool capture,bool perspective){
    if(!ntr::gx_direct_enabled() || !std::getenv("PLAYER_NATIVE_COMPOSITE") ||
       std::getenv("PLAYER_DEPTH_PROBE") || std::getenv("PLAYER_ALPHA_PROBE")){
        direct_ready=false;return player_present_legacy(capture,perspective);
    }
    const double batch_start=v3ds_time_ms();
    const auto& packet=ntr::gx_direct_packet();
    if(packet.triangles().size()>0x8000/3){std::fprintf(stderr,"FATAL: Player GPU vertex capacity exceeded\n");return false;}
    if(packet.vertices().size()>0x8000){
        // Many discarded/unfinished primitives can have little output. Keep
        // exact legacy handling rather than imposing a new silent truncation.
        direct_ready=false;return player_present_legacy(capture,perspective);
    }
    if(!direct_reuse || !direct_ready || direct_source!=&packet ||
       direct_prepared.geometry_generation!=packet.generation() ||
       !v3ds_arena::valid(direct_prepared) || !direct_resources.external_unchanged()){
        V3dsDirectLease lease{};
        if(!v3ds_arena::acquire(lease))return false;
        DirectGuard preparation_guard{lease.frame_token};
        direct_ready=false;
        direct_builder.reserve(packet.vertices().size());
        const auto nb=packet.triangles().size()+1,nd=packet.triangles().size()*5+1;
        if(direct_batches.size()<nb)direct_batches.resize(nb);
        if(direct_depths.size()<nd)direct_depths.resize(nd);
        if(!direct_builder.build(packet,{lease.vertices,lease.vertex_capacity,
            direct_batches.data(),direct_batches.size(),direct_depths.data(),direct_depths.size(),lease.frame_token})){
            std::fprintf(stderr,"FATAL: clipped GPU vertex capacity exceeded\n");return false;
        }
        if(!direct_resources.freeze(direct_batches.data(),direct_builder.batch_count())){
            // Explicit bounded resources refusal: retire the acquired list
            // before the unchanged legacy driver reconstructs polygons.
            v3ds_arena::abort(lease.frame_token);preparation_guard.keep=true;
            return player_present_legacy(capture,perspective);
        }
        direct_prepared=direct_builder.prepared();direct_source=&packet;
        direct_convergence=direct_builder.convergence();direct_ready=true;
        preparation_guard.keep=true;
    }
    const auto prepared=direct_prepared;
    const float convergence=direct_convergence;
    const bool probe=false;
    const double batch_ms=v3ds_time_ms()-batch_start;
    DirectGuard guard{prepared.frame_token};
'''
(OUT/'player_present-pipeline.cpp').write_text(legacy_driver+prefix+tail+'    return false;\n}\n')
shutil.copy2(Path(__file__),OUT/Path(__file__).name)
# Distinct class, same numerical body, no ODR disagreement with the frozen
# diagnostic Builder retained by the viewer. Remove statistical side effects.
header=(PROOF/'gx_geometry_arena_candidate.h').read_text()
begin=header.index('class Builder {');body=header[begin:header.rfind('\n}')]
release=body.replace('class Builder {','class ReleaseBuilder {',1)
for a,b in [
    ('    std::uint64_t conversions=0,reuses=0,clipped_triangles=0;\n',''),
    ('conversions=reuses=clipped_triangles=0;valid_=true;','valid_=true;'),
    ('++conversions;',''),('else ++reuses;',''),('    ++clipped_triangles;\n','')]:
    assert a in release,a;release=release.replace(a,b)
(OUT/'gx_arena_release_builder.h').write_text('#pragma once\n#include "gx_geometry_arena_candidate.h"\nnamespace ntr::gx_arena_candidate {\n'+release+'\n}\n')
manifest={'status':'PREPARED sources only; GX same-binary branch and release Builder NOT_YET_TESTED',
    'production_modified':False,'gx_reference_sha256':sha(PROOF/'gx-original.cpp'),
    'counter_removal':'Distinct ReleaseBuilder; only class name/statistical declarations and updates removed. No arithmetic changes.',
    'files_sha256':{p.name:sha(p) for p in OUT.iterdir() if p.is_file() and p.name!='prepare.json'}}
(OUT/'prepare.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(manifest['status']);print(OUT)
