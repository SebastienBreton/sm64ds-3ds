// Real retail menu resource initialization, with original sound boot active.
#include "ntr/mmio.h"
#include "Heap.h"
#include "dScDSMT_c.h"
#include "menu/menu.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
#include <cstdlib>
namespace Sound { void LoadInitialGroup(int); }
extern "C" {
void func_02059bc0();
void __sinit_02075154();
void __sinit_0207501c();
void __sinit_02073a24();
void __sinit_02074da8();
void func_0201fe08();
void func_020133bc();
dScDSMT_c* dScDSMT_c_classInit();
extern unsigned short data_020a4b54;
extern void* data_020a0ea0;
extern void* data_020a0eac;
}
int main() {
 setvbuf(stdout,nullptr,_IONBF,0);
 // A dedicated cart path prevents testing from modifying the user's gameplay save.
 setenv("SM64DS_SAVE_PATH","sdmc:/sm64ds/menu-validation.sav",1);
 if(!ntr::io_init() || !Heap::SetupRootHeap())return 1;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();
 data_020a0eac=data_020a0ea0;
 __sinit_02073a24();__sinit_02074da8();
 std::puts("menu boot: original sound initialization");func_020133bc();
 Sound::LoadInitialGroup(0);
 std::puts("menu boot: original message bank");func_0201fe08();
 if(!(sm64ds_try_load_overlay(7)==OVERLAY_OK))return 2;
 data_020a4b54=1;
 auto* scene=dScDSMT_c_classInit();if(!scene)return 3;
 std::puts("menu boot: original InitResources");
 const int result=scene->InitResources();
 std::printf("menu InitResources returned=%d scene=%p\n",result,scene);
 if(result!=1)return 4;
 std::puts("menu boot: cleanup");scene->CleanupResources();delete scene;
 if(!(sm64ds_try_unload_overlay(7)==OVERLAY_OK))return 5;
 std::puts("smoke_menu_init: PASS resource initialization and cleanup");return 0;
}
