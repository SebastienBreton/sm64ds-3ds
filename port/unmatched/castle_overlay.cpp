// Complete numeric/BSS/constructor lifecycle for overlay9 (castle grounds).
// Its code remains resident. Call unload only after its actors are destroyed.
#include <cstring>
#include "unmatched/actor_registry.h"
#include <cstddef>
#include <cstdint>
extern "C" {
unsigned sm64ds_reset_level_data(unsigned);
extern unsigned char g_profile_SBIRD[],g_profile_MC_WATER[],g_profile_MC_METALNET[],g_profile_MC_FLAG[];
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
extern unsigned char sm64ds_castle_bss_begin[],sm64ds_castle_bss_end[];
void __sinit_ov009_02112458();
void __sinit_ov009_02112524();
void __sinit_ov009_02112a5c();
void __sinit_ov009_02112ac8();
}
namespace {
bool loaded=false;
const unsigned ids[]={343,338,339,342};
void* const profiles[]={g_profile_SBIRD,g_profile_MC_WATER,g_profile_MC_METALNET,g_profile_MC_FLAG};
void* previous_profiles[4];
}
extern "C" bool sm64ds_load_castle_overlay() {
    if(loaded)return false;
    if(sm64ds_reset_level_data(9)!=4044)return false;
    const auto size=reinterpret_cast<uintptr_t>(sm64ds_castle_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_castle_bss_begin);
    if(size!=704)return false;
    std::memset(sm64ds_castle_bss_begin,0,size);
    const int previous=sm64ds_set_overlay_scope(9);
    // ROM static initializer table order, excluding its terminal null.
    __sinit_ov009_02112458();
    __sinit_ov009_02112524();
    __sinit_ov009_02112a5c();
    __sinit_ov009_02112ac8();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<4;++i){previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;
    return true;
}
extern "C" bool sm64ds_unload_castle_overlay() {
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(9);
    for(unsigned i=0;i<4;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;
    return true;
}
