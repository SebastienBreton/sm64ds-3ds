// Original actors must die before unload. Restore the prior registry after
// LIFO resource destruction; constructor order is checked against the ROM.
#include "unmatched/actor_registry.h"
#include <cstring>
#include <cstdint>
extern "C" {
unsigned sm64ds_reset_level_data(unsigned);
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
extern unsigned char sm64ds_overlay91_bss_begin[],sm64ds_overlay91_bss_end[];
extern unsigned char g_profile_UPDOWN_LIFT[];
extern unsigned char g_profile_HS_UPDOWN_LIFT[];
extern unsigned char g_profile_KM3_LIFT[];
extern unsigned char g_profile_KM2_SUSUMU[];
extern unsigned char g_profile_KM1_DERU[];
extern unsigned char g_profile_KM3_DERU01[];
extern unsigned char g_profile_KM3_DERU02[];
extern unsigned char g_profile_RC_RIFT01[];
extern unsigned char g_profile_KM2_RIFUT01[];
extern unsigned char g_profile_KM2_RIFUT02[];
extern unsigned char g_profile_BK_TRANSBAR[];
extern unsigned char g_profile_DOSUN[];
extern unsigned char g_profile_PILE[];
extern unsigned char g_profile_HYUHYU[];
void __sinit_ov091_02134524();
void __sinit_ov091_021345dc();
void __sinit_ov091_021346e8();
void __sinit_ov091_02134930();
void __sinit_ov091_021349c4();
void __sinit_ov091_02134a30();
}
namespace {
bool loaded=false;
const unsigned ids[]={29,30,157,144,138,154,155,124,147,146,55,161,27,231};
void*const profiles[]={g_profile_UPDOWN_LIFT,g_profile_HS_UPDOWN_LIFT,g_profile_KM3_LIFT,g_profile_KM2_SUSUMU,g_profile_KM1_DERU,g_profile_KM3_DERU01,g_profile_KM3_DERU02,g_profile_RC_RIFT01,g_profile_KM2_RIFUT01,g_profile_KM2_RIFUT02,g_profile_BK_TRANSBAR,g_profile_DOSUN,g_profile_PILE,g_profile_HYUHYU};
void* previous_profiles[14];
}
extern "C" bool sm64ds_load_overlay91(){
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay91_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay91_bss_begin);
    if(bytes!=672)return false;
    if(sm64ds_reset_level_data(91)!=0)return false;
    std::memset(sm64ds_overlay91_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(91);
    __sinit_ov091_02134524();
    __sinit_ov091_021345dc();
    __sinit_ov091_021346e8();
    __sinit_ov091_02134930();
    __sinit_ov091_021349c4();
    __sinit_ov091_02134a30();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<14;++i){previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay91(){
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(91);
    for(unsigned i=0;i<14;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
