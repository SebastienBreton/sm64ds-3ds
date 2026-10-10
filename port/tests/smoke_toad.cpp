#include "common.h"
#include "daKinopio_c.h"
#include "ntr/mmio.h"
#include <cstdio>
#include <cstring>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
void func_02059bc0();
void __sinit_02075154();
void __sinit_0207501c();
void __sinit_02073a24();
void __sinit_02074da8();
void __sinit_ov085_0212f2a8();
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
daKinopio_c* daKinopio_c_classInit();
extern daKinopio_c::State data_ov085_0212fe88[];
extern unsigned char data_ov085_0212f27c[];
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    func_02059bc0();__sinit_02075154();__sinit_0207501c();
    __sinit_02073a24();__sinit_02074da8();
    const int previous=sm64ds_set_overlay_scope(85);
    __sinit_ov085_0212f2a8();sm64ds_set_overlay_scope(previous);
    if(data_ov085_0212fe88[0].init!=&daKinopio_c::St_Idle_Init ||
       data_ov085_0212fe88[0].main!=&daKinopio_c::St_Idle_Main ||
       data_ov085_0212fe88[1].init!=&daKinopio_c::St_Talk_Init ||
       data_ov085_0212fe88[1].main!=&daKinopio_c::St_Talk_Main)return 3;
    if(std::memcmp(data_ov085_0212f27c,"\x14\x28\x3c\x00",4))return 4;
    auto* toad=daKinopio_c_classInit();
    if(!toad)return 5;
    toad->param1=0xffff;toad->mAngleX=0xff; // Ordinary Toad without star or companion.
    std::printf("Toad factory: %p size=%u\n",static_cast<void*>(toad),unsigned(sizeof(*toad)));std::fflush(stdout);
    if(toad->InitResources()!=1)return 6;
    std::printf("Toad resources: state=%d message=%u opacity=%u\n",toad->mState,toad->mMessageID,toad->mOpacity);std::fflush(stdout);
    if(toad->mState!=0 || toad->mMessageID!=0 || toad->mOpacity!=255)return 7;
    for(unsigned frame=0;frame<60;++frame)if(toad->Behavior()!=1)return 9;
    if(toad->mState!=0 || toad->mOpacity!=60)return 10;
    std::printf("Toad behavior: frames=60 state=%d opacity=%u\n",toad->mState,toad->mOpacity);
    toad->CleanupResources();
    delete toad;
    if(sm64ds_destroy_overlay_resources(85)!=3)return 8;
    std::puts("smoke_toad: PASS original factory, assets, idle behavior and cleanup");
    return 0;
}
