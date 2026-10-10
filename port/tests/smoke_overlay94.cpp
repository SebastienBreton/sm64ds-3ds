#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_overlay94_bss_begin[],sm64ds_overlay94_bss_end[];
void __sinit_ov094_021367e8();
int sm64ds_set_overlay_scope(int);unsigned sm64ds_destroy_overlay_resources(int);
struct Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int range,height,draw,secondary;};
extern Profile g_profile_OWL;
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 const auto before=root->VMemoryLeft();
 const Profile* profiles[]={&g_profile_OWL};
 for(auto* profile:profiles)if(!profile->create)return 3;
 if(sm64ds_overlay94_bss_end-sm64ds_overlay94_bss_begin!=160)return 4;
 for(unsigned repeat=0;repeat<2;++repeat){
  std::memset(sm64ds_overlay94_bss_begin,0,160);
  auto old=sm64ds_set_overlay_scope(94);
  __sinit_ov094_021367e8();
  sm64ds_set_overlay_scope(old);
  unsigned destroyed=sm64ds_destroy_overlay_resources(94);
  std::printf("overlay94 initializer pass=%u destructors=%u\n",repeat,destroyed);
  if(destroyed!=4||sm64ds_destroy_overlay_resources(94)!=0||root->VMemoryLeft()!=before)return 5;
 }
 std::puts("smoke_overlay94: PASS initialization and resource destructor cycles");return 0;
}
