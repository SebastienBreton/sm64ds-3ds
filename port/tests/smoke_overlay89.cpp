#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_overlay89_bss_begin[],sm64ds_overlay89_bss_end[];
void __sinit_ov089_021328d4();
int sm64ds_set_overlay_scope(int);unsigned sm64ds_destroy_overlay_resources(int);
struct Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int range,height,draw,secondary;};
extern Profile g_profile_OBJ_KEY,g_profile_LAST_STAR;
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 const auto before=root->VMemoryLeft();
 const Profile* profiles[]={&g_profile_OBJ_KEY,&g_profile_LAST_STAR};
 for(auto* profile:profiles)if(!profile->create)return 3;
 if(sm64ds_overlay89_bss_end-sm64ds_overlay89_bss_begin!=256)return 4;
 for(unsigned repeat=0;repeat<2;++repeat){
  std::memset(sm64ds_overlay89_bss_begin,0,256);
  auto old=sm64ds_set_overlay_scope(89);
  __sinit_ov089_021328d4();
  sm64ds_set_overlay_scope(old);
  unsigned destroyed=sm64ds_destroy_overlay_resources(89);
  std::printf("overlay89 initializer pass=%u destructors=%u\n",repeat,destroyed);
  if(destroyed!=8||sm64ds_destroy_overlay_resources(89)!=0||root->VMemoryLeft()!=before)return 5;
 }
 std::puts("smoke_overlay89: PASS initialization and resource destructor cycles");return 0;
}
