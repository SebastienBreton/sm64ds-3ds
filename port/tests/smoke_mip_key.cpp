#include "common.h"
#include "daObj_Mip_Key_c.h"
#include "ntr/mmio.h"
#include <cstdio>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
void func_02059bc0();
void __sinit_02075154();
void __sinit_0207501c();
void __sinit_02073a24();
void __sinit_02074da8();
void __sinit_ov085_0212f5ec();
void __sinit_ov085_0212f9bc();
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
daObj_Mip_Key_c* daObj_Mip_Key_c_classInit();
extern unsigned char data_ov085_021305d8[];
extern MipKeyState data_ov085_0213071c;
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    func_02059bc0();__sinit_02075154();__sinit_0207501c();
    __sinit_02073a24();__sinit_02074da8();
    const int previous=sm64ds_set_overlay_scope(85);
    __sinit_ov085_0212f5ec();__sinit_ov085_0212f9bc();sm64ds_set_overlay_scope(previous);
    auto* key=daObj_Mip_Key_c_classInit();
    if(!key)return 3;
    std::printf("Rabbit key factory: %p size=%u\n",static_cast<void*>(key),unsigned(sizeof(*key)));std::fflush(stdout);
    if(key->InitResources()!=1)return 4;
    if(key->mStateTimer!=10 || key->mState!=&data_ov085_0213071c)return 5;
    if(!data_ov085_021305d8[2] || !*reinterpret_cast<void**>(data_ov085_021305d8+4))return 6;
    const unsigned initial_angle=static_cast<unsigned short>(key->mAngleX);
    for(unsigned i=0;i<60;++i)if(key->Behavior()!=1)return 7;
    if(key->mStateTimer || key->mState!=&data_ov085_0213071c || key->mPlayer)return 8;
    if(static_cast<unsigned short>(key->mAngleX)!=static_cast<unsigned short>(initial_angle+60*0x1000))return 9;
    std::printf("Rabbit key behavior: calls=60 wait=%u angle=%u\n",unsigned(key->mStateTimer),unsigned(static_cast<unsigned short>(key->mAngleX)));
    key->CleanupResources();
    if(data_ov085_021305d8[2] || *reinterpret_cast<void**>(data_ov085_021305d8+4))return 10;
    delete key;
    if(sm64ds_destroy_overlay_resources(85)!=11)return 11;
    std::puts("smoke_mip_key: PASS model, loose state, timer, rotation, cleanup");
    return 0;
}
