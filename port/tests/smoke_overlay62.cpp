#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_overlay62_bss_begin[],sm64ds_overlay62_bss_end[];
void __sinit_ov062_0211cf30();void __sinit_ov062_0211d2d8();
void __sinit_ov062_0211d4a0();void __sinit_ov062_0211d690();void __sinit_ov062_0211d6fc();
std::uintptr_t func_020462bc(const int*);
int sm64ds_set_overlay_scope(int);unsigned sm64ds_destroy_overlay_resources(int);
struct Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int range,height,draw,secondary;};
extern Profile g_profile_HOLHEI,g_profile_NOKONOKO,g_profile_NOKONOKO_S,g_profile_RACE_NOKO,g_profile_RACE_FLAG,g_profile_JANGO;
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 const auto before=root->VMemoryLeft();
 int modelCounts[2]={0,7};if(func_020462bc(modelCounts)!=7*0x30)return 6;
 const Profile* profiles[]={&g_profile_HOLHEI,&g_profile_NOKONOKO,&g_profile_NOKONOKO_S,&g_profile_RACE_NOKO,&g_profile_RACE_FLAG,&g_profile_JANGO};
 for(auto* profile:profiles)if(!profile->create)return 3;
 if(sm64ds_overlay62_bss_end-sm64ds_overlay62_bss_begin!=960)return 4;
 for(unsigned repeat=0;repeat<2;++repeat){
  std::memset(sm64ds_overlay62_bss_begin,0,960);
  auto old=sm64ds_set_overlay_scope(62);
  __sinit_ov062_0211cf30();__sinit_ov062_0211d2d8();__sinit_ov062_0211d4a0();__sinit_ov062_0211d690();__sinit_ov062_0211d6fc();
  sm64ds_set_overlay_scope(old);
  unsigned destroyed=sm64ds_destroy_overlay_resources(62);
  std::printf("overlay62 initializer pass=%u destructors=%u\n",repeat,destroyed);
  if(destroyed!=33||sm64ds_destroy_overlay_resources(62)!=0||root->VMemoryLeft()!=before)return 5;
 }
 std::puts("smoke_overlay62: PASS initialization and resource destructor cycles");return 0;
}
