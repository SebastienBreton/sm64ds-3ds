// Castle interior overlay: native actors, constructors, resources and residency.
#include "unmatched/actor_registry.h"
#include <cstring>
#include <cstdint>
extern "C" {
unsigned sm64ds_reset_level_data(unsigned);
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
extern unsigned char sm64ds_overlay10_bss_begin[],sm64ds_overlay10_bss_end[];
extern unsigned char g_profile_C1_TRAP[],g_profile_C1_HIKARI[],g_profile_C1_PEACH[];
void __sinit_ov010_0211203c();
void __sinit_ov010_0211211c();
void __sinit_ov010_0211215c();
}
namespace {
bool loaded=false;
const unsigned ids[]={36,37,38};
void*const profiles[]={g_profile_C1_TRAP,g_profile_C1_HIKARI,g_profile_C1_PEACH};
void* previous_profiles[3];
}
extern "C" bool sm64ds_load_overlay10(){
    if(loaded)return false;
    if(sm64ds_reset_level_data(10)!=2232)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay10_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay10_bss_begin);
    if(bytes!=128)return false;
    std::memset(sm64ds_overlay10_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(10);
    __sinit_ov010_0211203c();__sinit_ov010_0211211c();__sinit_ov010_0211215c();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<3;++i){previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay10(){
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(10);
    for(unsigned i=0;i<3;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
