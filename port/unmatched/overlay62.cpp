#include "unmatched/actor_registry.h"
// Native lifecycle for the six overlay62 actor families. Actors must be gone
// before unloading; shared-handle destructors do not release live actor refs.
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_overlay62_bss_begin[],sm64ds_overlay62_bss_end[];
extern unsigned char g_profile_HOLHEI[],g_profile_NOKONOKO[],g_profile_NOKONOKO_S[];
extern unsigned char g_profile_RACE_NOKO[],g_profile_RACE_FLAG[],g_profile_JANGO[];
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
void __sinit_ov062_0211cf30();void __sinit_ov062_0211d2d8();
void __sinit_ov062_0211d4a0();void __sinit_ov062_0211d690();void __sinit_ov062_0211d6fc();
}
namespace {
bool loaded=false;
const unsigned ids[]={190,203,204,188,205,239};
void* const profiles[]={g_profile_HOLHEI,g_profile_NOKONOKO,g_profile_NOKONOKO_S,
    g_profile_RACE_NOKO,g_profile_RACE_FLAG,g_profile_JANGO};
void* previous_profiles[6];
}
extern "C" bool sm64ds_load_overlay62() {
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay62_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay62_bss_begin);
    if(bytes!=960)return false;
    std::memset(sm64ds_overlay62_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(62);
    __sinit_ov062_0211cf30();__sinit_ov062_0211d2d8();__sinit_ov062_0211d4a0();
    __sinit_ov062_0211d690();__sinit_ov062_0211d6fc();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<6;++i) {previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay62() {
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(62);
    for(unsigned i=0;i<6;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
