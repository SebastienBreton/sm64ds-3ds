// Original actors must die before unload. Restore the prior registry after
// LIFO resource destruction; constructor order is checked against the ROM.
#include "unmatched/actor_registry.h"
#include <cstring>
#include <cstdint>
extern "C" {
unsigned sm64ds_reset_level_data(unsigned);
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
extern unsigned char sm64ds_overlay14_bss_begin[],sm64ds_overlay14_bss_end[];
extern unsigned char g_profile_SWITCHDOOR[];
extern unsigned char g_profile_WANWAN[];
extern unsigned char g_profile_WANWAN_SHUTTER[];
void __sinit_ov014_021130ac();
void __sinit_ov014_02113118();
void __sinit_ov014_021132fc();
}
namespace {
bool loaded=false;
const unsigned ids[]={40,219,41};
void*const profiles[]={g_profile_SWITCHDOOR,g_profile_WANWAN,g_profile_WANWAN_SHUTTER};
void* previous_profiles[3];
}
extern "C" bool sm64ds_load_overlay14(){
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay14_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay14_bss_begin);
    if(bytes!=160)return false;
    if(sm64ds_reset_level_data(14)!=4664)return false;
    std::memset(sm64ds_overlay14_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(14);
    __sinit_ov014_021130ac();
    __sinit_ov014_02113118();
    __sinit_ov014_021132fc();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<3;++i){previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay14(){
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(14);
    for(unsigned i=0;i<3;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
