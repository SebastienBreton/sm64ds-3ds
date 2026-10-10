#include "unmatched/actor_registry.h"
// Native lifecycle for the fourteen overlay102 actor profiles. Actors must be gone
// before unloading; shared-handle destructors do not release live actor refs.
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_overlay102_bss_begin[],sm64ds_overlay102_bss_end[];
extern unsigned char g_profile_KI_IWA[],g_profile_KI_HASIRA_DAI[],g_profile_BK_TOWER[],g_profile_DOKAN[],g_profile_MC_DODAI[],g_profile_MC_HAZAD[],g_profile_VS_ITEM_BLOCK[],g_profile_HATENA_BLOCK[],g_profile_ITEM_BLOCK[],g_profile_CAP_BLOCK_M[],g_profile_CAP_BLOCK_L[],g_profile_CAP_BLOCK_W[],g_profile_BOMBHEI[],g_profile_SHELL[];
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
void __sinit_ov102_0214d714();
void __sinit_ov102_0214d908();
void __sinit_ov102_0214de70();
void __sinit_ov102_0214dfac();
}
namespace {
bool loaded=false;
const unsigned ids[]={61,59,49,298,340,341,22,20,21,23,25,24,206,285};
void* const profiles[]={g_profile_KI_IWA,g_profile_KI_HASIRA_DAI,g_profile_BK_TOWER,g_profile_DOKAN,g_profile_MC_DODAI,g_profile_MC_HAZAD,g_profile_VS_ITEM_BLOCK,g_profile_HATENA_BLOCK,g_profile_ITEM_BLOCK,g_profile_CAP_BLOCK_M,g_profile_CAP_BLOCK_L,g_profile_CAP_BLOCK_W,g_profile_BOMBHEI,g_profile_SHELL};
void* previous_profiles[14];
}
extern "C" bool sm64ds_load_overlay102() {
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay102_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay102_bss_begin);
    if(bytes!=960)return false;
    std::memset(sm64ds_overlay102_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(102);
    __sinit_ov102_0214d714();__sinit_ov102_0214d908();__sinit_ov102_0214de70();__sinit_ov102_0214dfac();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<14;++i) {previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay102() {
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(102);
    for(unsigned i=0;i<14;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
