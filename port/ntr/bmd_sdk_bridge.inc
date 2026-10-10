// Private include after GX and bmd_instance_evaluator.inc; never shared code.
#include "ntr/bmd_frame_store.h"
#include "ntr/bmd_sync_admission.h"
#include <map>
#include <cstdlib>
#include <cstdio>
#include "ntr/mmio.h"
#include "ntr/bmd_compiled_profile.h"
#ifdef __3DS__
#include <3ds.h>
static double bmd_now_ms(){return svcGetSystemTick()*(1000.0/SYSCLOCK_ARM11);}
#else
#include <chrono>
static double bmd_now_ms(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
#endif

namespace bmd_native {
using Counters=bmd_profile::Counters;
Counters totals;
bool enabled(){static const bool value=[](){const char* s=std::getenv("PLAYER_BMD_COMPILED_LISTS");return s && std::atoi(s)!=0;}();return value;}
FrameStore store;TextureLeases texture_leases;
size_t triangle_count(){return enabled()?store.triangle_count():0;}
uint64_t next_identity=0,completed_raw_generation=~uint64_t(0),cpu_revision=~uint64_t(0),cpu_raw_generation=~uint64_t(0);
uint32_t current_raw_base=0;
struct Entry {std::shared_ptr<bmd_program::Program> program;uint64_t identity=0;ntr::HostVector<uint32_t> refused_words;
    std::shared_ptr<gi::FastPlan> fast; // gpu-geometry-ingame bulk-evaluation plan (lazily built)
};
using MapPair=std::pair<const uint64_t,Entry>;
std::map<uint64_t,Entry,std::less<uint64_t>,ntr::HostAllocator<MapPair>> cache;
size_t input_bytes=0;
struct Active {
    std::shared_ptr<bmd_program::Program> program;Evaluator evaluator;
    bmd_program::ChunkTransfer<Evaluator> transfer;Active* previous=nullptr;
    explicit Active(std::shared_ptr<bmd_program::Program> p,uint64_t identity,std::shared_ptr<gi::FastPlan>* plan=nullptr):
        program(p),evaluator(std::move(p),identity,texture_leases,plan){}
};
Active* active=nullptr;
void new_frame(){if(!enabled())return;store.clear();current_raw_base=0;completed_raw_generation=~uint64_t(0);cpu_revision=cpu_raw_generation=~uint64_t(0);}
void reset(){if(!enabled())return;new_frame();cache.clear();input_bytes=0;}
bool flush_legacy(){
    using namespace ntr;
    if(mesh_packet.triangles().empty())return mesh_packet.vertices().empty();
    if(g.prim!=-1 || !mesh_strip.empty())return false;
    const auto before=store.occurrences();const auto start=bmd_now_ms();
    const bool gpu_supported=store.append_legacy(mesh_packet,++next_identity,texture_leases);
    totals.legacy_ms+=bmd_now_ms()-start;++totals.legacy_planners;
    if(store.occurrences()==before){++totals.resource_refused;return false;}
    if(!gpu_supported)++totals.legacy_refused;
    totals.raw_vertices+=mesh_packet.vertices().size();totals.raw_triangles+=mesh_packet.triangles().size();
    mesh_packet.clear(true);mesh_matrix_valid=false;mesh_cpu_generation=~uint64_t(0);return true;
}
bool build_cpu(ntr::gx_geometry::Packet& output,const native_mesh::Packet& current){
    if(!enabled() || !store.occurrences()){current_raw_base=0;return false;}
    if(cpu_revision!=store.revision() || cpu_raw_generation!=current.generation()){
        ++totals.cpu_rebuilds;current_raw_base=store.cpu_packet(output,current);cpu_revision=store.revision();cpu_raw_generation=current.generation();
    }
    return true;
}
std::shared_ptr<const prepared_mesh::Frame> prepared() try {
    using namespace ntr;if(!enabled() || !mesh_packet.requested() || !store.occurrences())return {};
    if(active && !active->transfer.finished())return {};
    // On HELD, there are no new raw writes. Never flush/recompose the same frame.
    if(completed_raw_generation!=mesh_packet.generation()){
        if(!flush_legacy())return {};
        completed_raw_generation=mesh_packet.generation();
    }
    const auto start=bmd_now_ms();auto frame=store.prepared();totals.compose_ms+=bmd_now_ms()-start;return frame;
} catch(const std::bad_alloc&) {
    // Legacy append commits transactionally. Completed occurrences and the
    // current raw tail remain available to the ordinary CPU reconstruction.
    ++totals.resource_refused;return {};
}
}

extern "C" bool bmd_program_consume_dma(const uint32_t* source,unsigned count){
    using namespace bmd_native;if(!active)return false;
    const auto result=active->transfer.consume(source,count);
    if(result==bmd_program::ConsumeResult::NotRegistered)return false;
    if(result!=bmd_program::ConsumeResult::Consumed){
        // Private safety failure, never silently replay a live tail after a
        // compiled prefix. No production activation until cold migration exists.
        std::printf("FATAL compiled BMD chunk capability result=%u consumed=%u count=%u\n",unsigned(result),active->transfer.cursor().words_consumed,count);std::abort();
    }
    ++totals.chunks;totals.bypassed_words+=count;
    if(active->transfer.finished()){
        totals.attributes+=active->evaluator.instance.attributes.size()*sizeof(native_mesh::Attribute);
        totals.matrices+=active->evaluator.instance.matrices.size()*sizeof(native_mesh::Matrix);
        if(!store.append_reserved(std::move(active->evaluator.instance))){
            std::puts("FATAL private reserved BMD occurrence invariant");std::abort();
        }
        ++totals.compiled;
        // Publish before the final original DMA completion callback can nest.
        ntr::mesh_packet.clear(true);ntr::mesh_matrix_valid=false;
        active=active->previous;
    }
    return true;
}
extern "C" void bmd_compiled_submit(int channel,int source,int bytes,void (*original_sdk)(int,int,int),int (*arena_contains)(const void*)){
    using namespace bmd_native;using namespace ntr;
    ++totals.lists;totals.bytes+=bytes>0?unsigned(bytes):0;if(unsigned(g.mode)<4)++totals.modes[g.mode];
    bool original_called=false,raw_disabled=false;
    auto original=[&](){original_called=true;const auto start=bmd_now_ms();original_sdk(channel,source,bytes);totals.sdk_ms+=bmd_now_ms()-start;};
    if(!enabled()){original();return;}
    if(!io_ready() || channel<0 || channel>3){++totals.source_refused;original();return;}
    const auto domain=sync_admission(true,active!=nullptr,channel,uint32_t(source),bytes,
        uint32_t(io_read(0x040000b8u+12u*unsigned(channel),4)),
        uint32_t(io_read(0x04000210u,4)),[arena_contains](uint32_t address){
            return arena_contains(reinterpret_cast<const void*>(uintptr_t(address)))!=0;
        });
    if(domain!=SyncRefusal::None){++totals.source_refused;original();return;}
    try {
    if(!mesh_packet.requested() || (g.mode!=MTX_POS && g.mode!=MTX_POSVEC)){++totals.shader_refused;original();return;}
    const auto* words=static_cast<const uint32_t*>(host_ptr(uint32_t(source)));const size_t count=unsigned(bytes)/4;
    const uint64_t key=(uint64_t(uint32_t(source))<<32)|unsigned(bytes);auto found=cache.find(key);
    const auto validation=bmd_now_ms();
    const bool same=found!=cache.end() && (found->second.program->matches(words,count) ||
        (found->second.refused_words.size()==count && !std::memcmp(found->second.refused_words.data(),words,unsigned(bytes))));
    totals.validation_ms+=bmd_now_ms()-validation;
    if(same)++totals.hits;
    else {
        if(found==cache.end() && (cache.size()>=512 || input_bytes+unsigned(bytes)>4*1024*1024)){++totals.refused;original();return;}
        Entry entry;entry.program=std::allocate_shared<bmd_program::Program>(HostAllocator<bmd_program::Program>{});entry.identity=++next_identity;
        const auto compiling=bmd_now_ms();const bool accepted=entry.program->compile(words,count);totals.compile_ms+=bmd_now_ms()-compiling;++totals.compiles;
        if(!accepted){entry.refused_words.assign(words,words+count);
            auto& s=gi::stats;++s.refusal[unsigned(entry.program->refusal)&7];if(entry.program->refused_command)++s.refused_commands[entry.program->refused_command];}
        if(found==cache.end()){found=cache.emplace(key,std::move(entry)).first;input_bytes+=unsigned(bytes);}else found->second=std::move(entry);
    }
    if(found->second.program->refusal!=bmd_program::Refusal::None){++totals.refused;++gi::stats.refused_submits;gi::stats.refused_words+=unsigned(bytes)/4;original();return;}
    Active context(found->second.program,found->second.identity,&found->second.fast);
    if(!context.evaluator.preflight(*context.program)){++totals.shader_refused;original();return;}
    if(!flush_legacy()){++totals.shader_refused;original();return;}
    if(!store.reserve_occurrence()){++totals.resource_refused;original();return;}
    mesh_packet.clear(false);mesh_matrix_valid=false;raw_disabled=true;
    // A miss just compiled these exact words; a hit just compared them.
    if(!context.transfer.start(*context.program,context.evaluator,words,count,true)){
        mesh_packet.clear(true);++totals.shader_refused;original();return;
    }
    context.previous=active;active=&context;original();active=context.previous;
    if(!context.transfer.finished()){
        std::printf("FATAL compiled BMD SDK returned before final chunk consumed=%u expected=%u\n",context.transfer.cursor().words_consumed,unsigned(count));std::abort();
    }
    } catch(const std::bad_alloc&) {
        // Never replay an original call that has already begun. Pre-admission
        // failures leave original GX state untouched and retain legacy spans.
        if(original_called)throw;
        if(raw_disabled){mesh_packet.clear(true);mesh_matrix_valid=false;}
        ++totals.resource_refused;original();
    }
}
std::shared_ptr<const prepared_mesh::Frame> bmd_compiled_frame(){return bmd_native::prepared();}
// game-coverage (2026-10-09): compiled programs are keyed on the DS source
// address of each display list. A scene/level handoff frees and reloads every
// model, so entries of the previous scene can never hit again; they were never
// evicted (newlib heap grew ~1-2.5 MB per course visit until bad_alloc/abort,
// and past 512 entries / 4 MiB input every new list fell back to the CPU path).
// Called by the scene handoff between game updates (never inside a transfer).
// Frames already prepared keep their programs alive through shared_ptr.
extern "C" void bmd_compiled_scene_reset(){
    using namespace bmd_native;
    if(!enabled() || active)return;
    cache.clear();input_bytes=0;texture_leases.retire_unused();
}
// Read-only (castle-30fps): true when bmd_compiled_frame() would return this
// same immutable frame again (no raw GX writes or new occurrences since).
bool bmd_compiled_frame_held(const prepared_mesh::Frame* frame){
    using namespace bmd_native;
    if(!frame || !enabled() || !ntr::mesh_packet.requested() || !store.occurrences() || active)return false;
    if(completed_raw_generation!=ntr::mesh_packet.generation())return false;
    return store.current()==frame;
}
void bmd_compiled_driver_refused(){++bmd_native::totals.driver_refused;}
void bmd_compiled_gpu_result(const std::shared_ptr<const prepared_mesh::Frame>& frame,bool geometry,bool success){
    auto& c=bmd_native::totals;
    if(geometry){++c.gpu_geometry_attempts;if(success)++c.gpu_geometry_success;else ++c.gpu_geometry_refused;}
    else {++c.gpu_present_attempts;if(success)++c.gpu_present_success;else ++c.gpu_present_refused;}
    // Identity is only compared while the original immutable Frame still lives.
    // No address recycling can turn a later Frame into a HELD replay.
    static std::weak_ptr<const prepared_mesh::Frame> previous;
    if(success && frame){const auto old=previous.lock();if(old && old==frame)++c.held_replays;previous=frame;}
}
bool bmd_compiled_geometry(){return bmd_native::enabled() && bmd_native::store.occurrences()!=0;}
bmd_profile::Counters bmd_compiled_take(){static bmd_profile::Counters previous;const auto out=bmd_profile::subtract(bmd_native::totals,previous);previous=bmd_native::totals;return out;}
void bmd_compiled_report(const bmd_profile::Counters& c,unsigned scene,unsigned frames){
    std::printf("Compiled BMD GPU presentAttempts=%llu presentSuccess=%llu presentRefused=%llu geometryAttempts=%llu geometrySuccess=%llu geometryRefused=%llu heldReplays=%llu cpuRebuilds=%llu driverRefused=%llu\n",(unsigned long long)c.gpu_present_attempts,(unsigned long long)c.gpu_present_success,(unsigned long long)c.gpu_present_refused,(unsigned long long)c.gpu_geometry_attempts,(unsigned long long)c.gpu_geometry_success,(unsigned long long)c.gpu_geometry_refused,(unsigned long long)c.held_replays,(unsigned long long)c.cpu_rebuilds,(unsigned long long)c.driver_refused);
    size_t cache_capacity=0;for(const auto& e:bmd_native::cache)cache_capacity+=e.second.program->capacity_bytes()+e.second.refused_words.capacity()*4;
    std::printf("Compiled BMD window scene=%u frames=%u cacheCapacityBytes=%u (excludes_Planner_private_scratch_and_map_control_blocks)\n",scene,frames,unsigned(cache_capacity));
    std::printf("Compiled BMD mode=%u lists=%llu bytes=%llu hits=%llu compiles=%llu compiled=%llu refused=%llu chunks=%llu bypassedWords=%llu legacyPlanners=%llu legacyRefused=%llu resourceRefused=%llu sourceRefused=%llu shaderRefused=%llu attrsBytes=%llu matricesBytes=%llu rawVertices=%llu rawTriangles=%llu validateMs=%.9f compileMs=%.9f sdkInclusiveMs=%.9f legacyMs=%.9f composeMs=%.9f topologyBuilds=%llu topologyHits=%llu textureBytes=%u modes=%llu,%llu,%llu,%llu\n",
        bmd_native::enabled(),(unsigned long long)c.lists,(unsigned long long)c.bytes,(unsigned long long)c.hits,(unsigned long long)c.compiles,(unsigned long long)c.compiled,(unsigned long long)c.refused,(unsigned long long)c.chunks,(unsigned long long)c.bypassed_words,(unsigned long long)c.legacy_planners,(unsigned long long)c.legacy_refused,(unsigned long long)c.resource_refused,(unsigned long long)c.source_refused,(unsigned long long)c.shader_refused,(unsigned long long)c.attributes,(unsigned long long)c.matrices,(unsigned long long)c.raw_vertices,(unsigned long long)c.raw_triangles,c.validation_ms,c.compile_ms,c.sdk_ms,c.legacy_ms,c.compose_ms,(unsigned long long)bmd_native::store.topology_builds(),(unsigned long long)bmd_native::store.topology_hits(),unsigned(bmd_native::texture_leases.bytes()),(unsigned long long)c.modes[0],(unsigned long long)c.modes[1],(unsigned long long)c.modes[2],(unsigned long long)c.modes[3]);
}
extern "C" void bmd_compiled_stats(){bmd_compiled_report(bmd_native::totals,~0u,0);}
// gpu-geometry-ingame counters (bulk evaluation coverage and verification).
extern "C" void gx_gpu_ingame_stats(){
    const auto& s=bmd_native::gi::stats;
    std::printf("GX GPU ingame options=%u occurrences fast=%llu slow=%llu refusedTexmode=%llu refusedMask=%llu plans=%llu litGpu=%llu litCpu=%llu verified=%llu mismatched=%llu\n",
        gx_gpu_ingame::options(),(unsigned long long)s.fast,(unsigned long long)s.slow,(unsigned long long)s.refused_texmode,(unsigned long long)s.refused_mask,
        (unsigned long long)s.plan_builds,(unsigned long long)s.lit_gpu,(unsigned long long)s.lit_cpu,(unsigned long long)s.verified,(unsigned long long)s.mismatched);
    std::printf("GX GPU ingame refusedSubmits=%llu refusedWords=%llu compileRefusals=",(unsigned long long)s.refused_submits,(unsigned long long)s.refused_words);
    for(auto n:s.refusal)std::printf("%llu,",(unsigned long long)n);
    std::printf(" commands=");for(unsigned c=0;c<256;++c)if(s.refused_commands[c])std::printf("%02x:%llu,",c,(unsigned long long)s.refused_commands[c]);std::puts("");
}
