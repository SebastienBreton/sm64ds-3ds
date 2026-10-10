// Original actors must die before unload. Restore the prior registry after
// LIFO resource destruction; constructor order is checked against the ROM.
#include "unmatched/actor_registry.h"
#include <cstring>
#include <cstdint>
extern "C" {
unsigned sm64ds_reset_level_data(unsigned);
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
extern unsigned char sm64ds_overlay78_bss_begin[],sm64ds_overlay78_bss_end[];
extern unsigned char g_profile_BOMBKING[];
void __sinit_ov078_02126660();
}
namespace {
bool loaded=false;
const unsigned ids[]={189};
void*const profiles[]={g_profile_BOMBKING};
void* previous_profiles[1];
}
extern "C" bool sm64ds_load_overlay78(){
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay78_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay78_bss_begin);
    if(bytes!=608)return false;
    if(sm64ds_reset_level_data(78)!=0)return false;
    std::memset(sm64ds_overlay78_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(78);
    __sinit_ov078_02126660();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<1;++i){previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay78(){
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(78);
    for(unsigned i=0;i<1;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
