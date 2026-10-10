// First native-menu integration gate: real factory/destructor and data reload.
// This deliberately does not claim that UI drawing or a save journey has run.
#include "ntr/mmio.h"
#include "Heap.h"
#include "dScDSMT_c.h"
#include "menu/menu.h"
#include "unmatched/native_overlay.h"
#include "unmatched/actor_registry.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
extern "C" {
dScDSMT_c* dScDSMT_c_classInit();
extern unsigned short data_020a4b54;
extern void* _ZTV9dScDSMT_c[];
extern void* _ZTVN9dScDSMT_c15graphCallback_cE[];
extern void* _ZTV10dFdDummy_c[];
extern unsigned char sm64ds_menu_bss_begin[],sm64ds_menu_data_0_begin[];
extern unsigned char g_profile_DSMT[];
}
#define CHECK(c) do {if(!(c)){std::printf("menu factory FAILED line=%d: %s\n",__LINE__,#c);return 1;}} while(0)
int main() {
 CHECK(ntr::io_init());auto* heap=Heap::SetupRootHeap();CHECK(heap);
 CHECK(sizeof(dScDSMT_c)==0x64);
 const auto initialFree=heap->MaxAllocationUnitSize();
 void* previous=data_020a4bb8_storage[1];
 const unsigned char initialByte=sm64ds_menu_data_0_begin[0];
 for(unsigned cycle=0;cycle<3;++cycle) {
  std::memset(sm64ds_menu_bss_begin,0xa5,0x1900);
  CHECK((sm64ds_try_load_overlay(7)==OVERLAY_OK));
  CHECK(sm64ds_try_load_overlay(7)==OVERLAY_OVERLAP);
  CHECK(data_020a4bb8_storage[1]==g_profile_DSMT);
  CHECK(sm64ds_menu_data_0_begin[0]==initialByte);
  for(unsigned i=0;i<0x1900;++i)CHECK(sm64ds_menu_bss_begin[i]==0);
  data_020a4b54=1;
 auto* scene=dScDSMT_c_classInit();CHECK(scene);
  CHECK(*reinterpret_cast<void***>(scene)==_ZTV9dScDSMT_c+2);
  CHECK(*reinterpret_cast<void***>(scene->unk_050)==_ZTVN9dScDSMT_c15graphCallback_cE+2);
  CHECK(*reinterpret_cast<void***>(&scene->fader)==_ZTV10dFdDummy_c+2);
  delete scene;
  CHECK(heap->MaxAllocationUnitSize()==initialFree);
  sm64ds_menu_data_0_begin[0]^=255;
  CHECK((sm64ds_try_unload_overlay(7)==OVERLAY_OK));
  CHECK(sm64ds_try_unload_overlay(7)==OVERLAY_NOT_LOADED);
  CHECK(data_020a4bb8_storage[1]==previous);
  std::printf("menu factory cycle=%u size=100 vtables=3 memory-recovered\n",cycle);
 }
 std::puts("smoke_menu_factory: PASS");return 0;
}
