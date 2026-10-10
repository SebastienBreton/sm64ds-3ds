// Retail overlay5: all mutable table bytes are reset only after scene teardown.
#include "unmatched/actor_registry.h"
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_minigame_data_0_begin[],sm64ds_minigame_data_0_end[];
extern unsigned char sm64ds_minigame_data_1_begin[],sm64ds_minigame_data_1_end[];
extern unsigned char sm64ds_minigame_data_2_begin[],sm64ds_minigame_data_2_end[];
extern unsigned char sm64ds_minigame_profile[];
unsigned sm64ds_destroy_overlay_resources(int);
}
namespace {
bool loaded,captured;
void* previous;
unsigned char initial0[0x1f0],initial1[0x24],initial2[0xb48];
struct Span{unsigned char* begin;unsigned char* end;unsigned char* initial;unsigned size;};
Span spans[]={
 {sm64ds_minigame_data_0_begin,sm64ds_minigame_data_0_end,initial0,sizeof initial0},
 {sm64ds_minigame_data_1_begin,sm64ds_minigame_data_1_end,initial1,sizeof initial1},
 {sm64ds_minigame_data_2_begin,sm64ds_minigame_data_2_end,initial2,sizeof initial2}};
}
extern "C" bool sm64ds_load_minigame_menu_overlay(){
 if(loaded)return false;
 for(const auto&s:spans)if(std::uintptr_t(s.end)-std::uintptr_t(s.begin)!=s.size)return false;
 for(const auto&s:spans){
  if(!captured)std::memcpy(s.initial,s.begin,s.size);
  std::memcpy(s.begin,s.initial,s.size);
 }
 captured=true;previous=data_020a4bb8[5];data_020a4bb8[5]=sm64ds_minigame_profile;
 loaded=true;return true;
}
extern "C" bool sm64ds_unload_minigame_menu_overlay(){
 if(!loaded)return false;
 sm64ds_destroy_overlay_resources(5);
 data_020a4bb8[5]=previous;loaded=false;return true;
}
