// Original actors must die before unload. Restore the prior registry after
// LIFO resource destruction; constructor order is checked against the ROM.
#include "unmatched/actor_registry.h"
#include <cstring>
#include <cstdint>
extern "C" {
unsigned sm64ds_reset_level_data(unsigned);
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
extern unsigned char sm64ds_overlay95_bss_begin[],sm64ds_overlay95_bss_end[];
extern unsigned char g_profile_SEESAW[];
extern unsigned char g_profile_KM1_SEESAW[];
extern unsigned char g_profile_KM2_YOKOSEESAW[];
extern unsigned char g_profile_KM3_SEESAW[];
extern unsigned char g_profile_KM3_YOKOSEESAW[];
extern unsigned char g_profile_RC_SEESAW[];
extern unsigned char g_profile_BOMB_SEESAW[];
extern unsigned char g_profile_UDLIFT_TERESA[];
extern unsigned char g_profile_UDLIFT[];
extern unsigned char g_profile_RC_RIFT02[];
extern unsigned char g_profile_OBJ_FLAMETHROWER[];
void __sinit_ov095_02136fe0();
void __sinit_ov095_0213722c();
}
namespace {
bool loaded=false;
const unsigned ids[]={28,133,143,149,150,128,39,32,33,131,318};
void*const profiles[]={g_profile_SEESAW,g_profile_KM1_SEESAW,g_profile_KM2_YOKOSEESAW,g_profile_KM3_SEESAW,g_profile_KM3_YOKOSEESAW,g_profile_RC_SEESAW,g_profile_BOMB_SEESAW,g_profile_UDLIFT_TERESA,g_profile_UDLIFT,g_profile_RC_RIFT02,g_profile_OBJ_FLAMETHROWER};
void* previous_profiles[11];
}
extern "C" bool sm64ds_load_overlay95(){
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay95_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay95_bss_begin);
    if(bytes!=448)return false;
    if(sm64ds_reset_level_data(95)!=0)return false;
    std::memset(sm64ds_overlay95_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(95);
    __sinit_ov095_02136fe0();
    __sinit_ov095_0213722c();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<11;++i){previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay95(){
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(95);
    for(unsigned i=0;i<11;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
