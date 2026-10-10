// Original actors must die before unload. Restore the prior registry after
// LIFO resource destruction; constructor order is checked against the ROM.
#include "unmatched/actor_registry.h"
#include <cstring>
#include <cstdint>
extern "C" {
unsigned sm64ds_reset_level_data(unsigned);
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
extern unsigned char sm64ds_overlay84_bss_begin[],sm64ds_overlay84_bss_end[];
extern unsigned char g_profile_KURIBO[];
extern unsigned char g_profile_KURIBO_S[];
extern unsigned char g_profile_KURIBO_L[];
extern unsigned char g_profile_RED_BOMBHEI[];
extern unsigned char g_profile_PAKUN2[];
extern unsigned char g_profile_FIREPAKUN[];
extern unsigned char g_profile_FIREPAKUN_S[];
extern unsigned char g_profile_PAKUN[];
void __sinit_ov084_0213035c();
void __sinit_ov084_02130558();
void __sinit_ov084_02130654();
}
namespace {
bool loaded=false;
const unsigned ids[]={200,201,202,181,253,251,252,250};
void*const profiles[]={g_profile_KURIBO,g_profile_KURIBO_S,g_profile_KURIBO_L,g_profile_RED_BOMBHEI,g_profile_PAKUN2,g_profile_FIREPAKUN,g_profile_FIREPAKUN_S,g_profile_PAKUN};
void* previous_profiles[8];
}
extern "C" bool sm64ds_load_overlay84(){
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay84_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay84_bss_begin);
    if(bytes!=544)return false;
    if(sm64ds_reset_level_data(84)!=0)return false;
    std::memset(sm64ds_overlay84_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(84);
    __sinit_ov084_0213035c();
    __sinit_ov084_02130558();
    __sinit_ov084_02130654();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<8;++i){previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay84(){
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(84);
    for(unsigned i=0;i<8;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
