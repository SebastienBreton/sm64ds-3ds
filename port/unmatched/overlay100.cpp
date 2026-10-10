#include "unmatched/actor_registry.h"
// Native lifecycle for the seven overlay100 actor profiles. Actors must be gone
// before unloading; shared-handle destructors do not release live actor refs.
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_overlay100_bss_begin[],sm64ds_overlay100_bss_end[];
extern unsigned char g_profile_BUTTERFLY[],g_profile_IRONBALL[],g_profile_WANWAN2[],g_profile_DOOR[],g_profile_STAR_GATE[],g_profile_FISH[],g_profile_PATH_LIFT[];
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
void __sinit_ov100_021473bc();
void __sinit_ov100_021474e8();
void __sinit_ov100_021475a4();
void __sinit_ov100_02147698();
void sm64ds_init_star_gate();
void __sinit_ov100_02147bc0();
void __sinit_ov100_02147d7c();
}
namespace {
bool loaded=false;
const unsigned ids[]={336,220,337,353,354,344,31};
void* const profiles[]={g_profile_BUTTERFLY,g_profile_IRONBALL,g_profile_WANWAN2,g_profile_DOOR,g_profile_STAR_GATE,g_profile_FISH,g_profile_PATH_LIFT};
void* previous_profiles[7];
}
extern "C" bool sm64ds_load_overlay100() {
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay100_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay100_bss_begin);
    if(bytes!=1152)return false;
    std::memset(sm64ds_overlay100_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(100);
    __sinit_ov100_021473bc();__sinit_ov100_021474e8();__sinit_ov100_021475a4();
    __sinit_ov100_02147698();sm64ds_init_star_gate();__sinit_ov100_02147bc0();__sinit_ov100_02147d7c();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<7;++i) {previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay100() {
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(100);
    for(unsigned i=0;i<7;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
