#pragma once
#include <cstdint>
namespace bmd_profile {
struct Counters {
    uint64_t lists=0,bytes=0,hits=0,compiles=0,refused=0,compiled=0,chunks=0,bypassed_words=0;
    uint64_t legacy_planners=0,legacy_refused=0,resource_refused=0,source_refused=0,shader_refused=0;
    uint64_t attributes=0,matrices=0,raw_vertices=0,raw_triangles=0,modes[4]{};
    uint64_t gpu_present_attempts=0,gpu_present_success=0,gpu_present_refused=0,gpu_geometry_attempts=0,gpu_geometry_success=0,gpu_geometry_refused=0,held_replays=0,cpu_rebuilds=0,driver_refused=0;
    double validation_ms=0,compile_ms=0,sdk_ms=0,legacy_ms=0,compose_ms=0;
};
#define BMD_COUNTER_FIELDS(F) F(lists) F(bytes) F(hits) F(compiles) F(refused) F(compiled) F(chunks) F(bypassed_words) \
 F(legacy_planners) F(legacy_refused) F(resource_refused) F(source_refused) F(shader_refused) \
 F(attributes) F(matrices) F(raw_vertices) F(raw_triangles) \
 F(gpu_present_attempts) F(gpu_present_success) F(gpu_present_refused) F(gpu_geometry_attempts) F(gpu_geometry_success) F(gpu_geometry_refused) F(held_replays) F(cpu_rebuilds) F(driver_refused) \
 F(validation_ms) F(compile_ms) F(sdk_ms) F(legacy_ms) F(compose_ms)
inline Counters subtract(const Counters& a,const Counters& b){Counters c;
#define BMD_SUB(x) c.x=a.x-b.x;
    BMD_COUNTER_FIELDS(BMD_SUB)
#undef BMD_SUB
    for(unsigned i=0;i<4;++i)c.modes[i]=a.modes[i]-b.modes[i];return c;
}
inline Counters window;
inline void accumulate(const Counters& a){
#define BMD_ADD(x) window.x+=a.x;
    BMD_COUNTER_FIELDS(BMD_ADD)
#undef BMD_ADD
    for(unsigned i=0;i<4;++i)window.modes[i]+=a.modes[i];
}
#undef BMD_COUNTER_FIELDS
}
bmd_profile::Counters bmd_compiled_take();
void bmd_compiled_report(const bmd_profile::Counters&,unsigned scene,unsigned frames);
