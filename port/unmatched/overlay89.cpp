#include "unmatched/actor_registry.h"
// Native lifecycle for the two overlay89 actor families. Actors must be gone
// before unloading; shared-handle destructors do not release live actor refs.
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_overlay89_bss_begin[],sm64ds_overlay89_bss_end[];
extern unsigned char g_profile_OBJ_KEY[],g_profile_LAST_STAR[];
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
void __sinit_ov089_021328d4();
}
namespace {
bool loaded=false;
const unsigned ids[]={282,283};
void* const profiles[]={g_profile_OBJ_KEY,g_profile_LAST_STAR};
void* previous_profiles[2];
}
extern "C" bool sm64ds_load_overlay89() {
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay89_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay89_bss_begin);
    if(bytes!=256)return false;
    std::memset(sm64ds_overlay89_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(89);
    __sinit_ov089_021328d4();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<2;++i) {previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay89() {
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(89);
    for(unsigned i=0;i<2;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
