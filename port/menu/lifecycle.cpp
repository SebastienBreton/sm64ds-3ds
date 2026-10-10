// Overlay7 data lifetime. Destroy its active scene before unloading this scope.
#include "unmatched/actor_registry.h"
#include <cstdint>
#include <cstring>
extern "C" {
extern unsigned char sm64ds_menu_bss_begin[],sm64ds_menu_bss_end[];
extern unsigned char sm64ds_menu_data_0_begin[],sm64ds_menu_data_0_end[];
extern unsigned char sm64ds_menu_data_1_begin[],sm64ds_menu_data_1_end[];
extern unsigned char g_profile_DSMT[];
unsigned sm64ds_destroy_overlay_resources(int);
}
namespace {
bool loaded, captured;
void* previous;
unsigned char initial0[0x02103264-0x020ccb54];
unsigned char initial1[24];
std::size_t span(unsigned char* a,unsigned char* b) {
 return reinterpret_cast<std::uintptr_t>(b)-reinterpret_cast<std::uintptr_t>(a);
}
}
extern "C" bool sm64ds_load_menu_overlay() {
 if(loaded || span(sm64ds_menu_bss_begin,sm64ds_menu_bss_end)!=0x1900 ||
    span(sm64ds_menu_data_0_begin,sm64ds_menu_data_0_end)!=sizeof(initial0) ||
    span(sm64ds_menu_data_1_begin,sm64ds_menu_data_1_end)!=sizeof(initial1)) return false;
 if(!captured) {
  std::memcpy(initial0,sm64ds_menu_data_0_begin,sizeof(initial0));
  std::memcpy(initial1,sm64ds_menu_data_1_begin,sizeof(initial1));
  captured=true;
 }
 std::memcpy(sm64ds_menu_data_0_begin,initial0,sizeof(initial0));
 std::memcpy(sm64ds_menu_data_1_begin,initial1,sizeof(initial1));
 std::memset(sm64ds_menu_bss_begin,0,0x1900);
 previous=data_020a4bb8[1];data_020a4bb8[1]=g_profile_DSMT;
 loaded=true;return true;
}
extern "C" bool sm64ds_unload_menu_overlay() {
 if(!loaded)return false;
 sm64ds_destroy_overlay_resources(7);
 data_020a4bb8[1]=previous;
 loaded=false;return true;
}
