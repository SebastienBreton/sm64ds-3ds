#include "common.h"
#include "daPeach_c.h"
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
void __sinit_ov085_0212f3a0();
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
daPeach_c* daPeach_c_classInit();
extern daPeach_c::StateFunc data_ov085_0213055c[];
extern unsigned char data_ov085_021304f4[];
extern unsigned char* data_ov085_0212f280[];
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    func_02059bc0();__sinit_02075154();__sinit_0207501c();
    __sinit_02073a24();__sinit_02074da8();
    const int previous=sm64ds_set_overlay_scope(85);
    __sinit_ov085_0212f3a0();sm64ds_set_overlay_scope(previous);
    const daPeach_c::StateFunc expected[]={&daPeach_c::State4,&daPeach_c::InitState3,
        &daPeach_c::InitState1,&daPeach_c::State3,&daPeach_c::State1,&daPeach_c::InitState4,
        &daPeach_c::InitState2,&daPeach_c::State2,&daPeach_c::State0,&daPeach_c::InitState0};
    for(unsigned i=0;i<10;++i)if(data_ov085_0213055c[i]!=expected[i])return 3;
    auto* peach=daPeach_c_classInit();
    if(!peach)return 4;
    std::printf("Peach factory: %p size=%u\n",static_cast<void*>(peach),unsigned(sizeof(*peach)));std::fflush(stdout);
    if(peach->InitResources()!=1)return 5;
    unsigned char* handles[8]={data_ov085_021304f4};
    for(unsigned i=0;i<7;++i)handles[i+1]=data_ov085_0212f280[i];
    for(auto* h:handles)if(!h[2] || !*reinterpret_cast<void**>(h+4))return 6;
    for(unsigned frame=0;frame<60;++frame)if(peach->Behavior()!=1)return 7;
    if(peach->mStateValue!=0 || peach->mStateFuncs!=data_ov085_0213055c)return 8;
    std::printf("Peach behavior: calls=60 state=%d resources=8\n",peach->mStateValue);std::fflush(stdout);
    peach->CleanupResources();
    for(auto* h:handles)if(h[2] || *reinterpret_cast<void**>(h+4))return 9;
    delete peach;
    if(sm64ds_destroy_overlay_resources(85)!=8)return 10;
    std::puts("smoke_peach: PASS factory, assets, idle behavior, resource release");
    return 0;
}
