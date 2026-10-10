// Native Mole/painting actor overlay with original constructor/resource lifetime.
#include "unmatched/actor_registry.h"
#include <cstring>
#include <cstdint>
extern "C" {
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
extern unsigned char sm64ds_overlay80_bss_begin[],sm64ds_overlay80_bss_end[];
extern unsigned sm64ds_overlay80_ids[];
extern void* sm64ds_overlay80_profiles[];
void __sinit_ov080_021278c0();void __sinit_ov080_02127a60();void __sinit_ov080_02127b2c();
}
namespace {bool loaded=false;void* previous[4];}
extern "C" bool sm64ds_load_overlay80(){
 if(loaded)return false;
 if(reinterpret_cast<uintptr_t>(sm64ds_overlay80_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay80_bss_begin)!=1184)return false;
 std::memset(sm64ds_overlay80_bss_begin,0,1184);
 const int scope=sm64ds_set_overlay_scope(80);
 __sinit_ov080_021278c0();__sinit_ov080_02127a60();__sinit_ov080_02127b2c();
 sm64ds_set_overlay_scope(scope);
 for(unsigned i=0;i<4;++i){previous[i]=data_020a4bb8[sm64ds_overlay80_ids[i]];data_020a4bb8[sm64ds_overlay80_ids[i]]=sm64ds_overlay80_profiles[i];}
 loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay80(){
 if(!loaded)return false;
 sm64ds_destroy_overlay_resources(80);
 for(unsigned i=0;i<4;++i)data_020a4bb8[sm64ds_overlay80_ids[i]]=previous[i];
 loaded=false;return true;
}

