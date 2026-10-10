#include "unmatched/actor_registry.h"
// Native lifecycle for the five overlay98 actor profiles. Actors must be gone
// before unloading; shared-handle destructors do not release live actor refs.
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_overlay98_bss_begin[],sm64ds_overlay98_bss_end[];
extern unsigned char g_profile_YAJIRUSI_L[],g_profile_YAJIRUSI_R[],g_profile_BLOCK_S[];
extern unsigned char g_profile_CANNON[],g_profile_WATERBOMB[];
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
void __sinit_ov098_0213bf9c();void __sinit_ov098_0213c058();
void __sinit_ov098_0213c214();void __sinit_ov098_0213c2b4();
}
namespace {
bool loaded=false;
const unsigned ids[]={299,300,194,207,208};
void* const profiles[]={g_profile_YAJIRUSI_L,g_profile_YAJIRUSI_R,g_profile_BLOCK_S,g_profile_CANNON,g_profile_WATERBOMB};
void* previous_profiles[5];
}
extern "C" bool sm64ds_load_overlay98() {
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay98_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay98_bss_begin);
    if(bytes!=352)return false;
    std::memset(sm64ds_overlay98_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(98);
    __sinit_ov098_0213bf9c();__sinit_ov098_0213c058();
    __sinit_ov098_0213c214();__sinit_ov098_0213c2b4();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<5;++i) {previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay98() {
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(98);
    for(unsigned i=0;i<5;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
