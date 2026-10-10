#include "common.h"
#include "daWbm_c.h"
#include "ntr/mmio.h"
#include <cstdio>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
void func_02059bc0();
void __sinit_02075154();
void __sinit_0207501c();
void __sinit_02073a24();
void __sinit_02074da8();
void __sinit_ov002_02100560();
void __sinit_ov098_0213c2b4();
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
int* daWbm_c_classInit();
extern unsigned char data_ov002_0210da38[],data_ov098_0213c91c[];
using State=void(daWbm_c::*)();
extern State data_ov098_0213c930[];
}
static bool handle(unsigned char* p,unsigned refs) {
    return p[2]==refs && bool(*reinterpret_cast<void**>(p+4))==bool(refs);
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    func_02059bc0();__sinit_02075154();__sinit_0207501c();
    __sinit_02073a24();__sinit_02074da8();
    int prev=sm64ds_set_overlay_scope(2);__sinit_ov002_02100560();
    sm64ds_set_overlay_scope(98);__sinit_ov098_0213c2b4();sm64ds_set_overlay_scope(prev);
    const State expected[]={&daWbm_c::StateDrop,&daWbm_c::StateFall,&daWbm_c::StateBounce};
    for(unsigned i=0;i<3;++i)if(data_ov098_0213c930[i]!=expected[i])return 3;
    for(unsigned variant=0;variant<3;++variant) {
        auto* bomb=reinterpret_cast<daWbm_c*>(daWbm_c_classInit());
        if(!bomb)return 4;
        bomb->param1=variant;bomb->mPosX=0;bomb->mPosY=0x400000;bomb->mPosZ=0;
        if(bomb->InitResources()!=1)return 5;
        if(bomb->mVariant!=int(variant) || bomb->mState!=int(variant!=0) || bomb->mSpawnPosY!=0x400000)return 6;
        if(!handle(data_ov002_0210da38,1) || !handle(data_ov098_0213c91c,variant==2?0:1))return 7;
        if(variant==0) {
            bomb->mStateTimer=10;
            for(unsigned i=0;i<60;++i)if(bomb->Behavior()!=1)return 8;
            if(bomb->mStateTimer || bomb->mState!=0)return 9;
        } else {
            if(bomb->mScaleX!=(variant==2?0x800:0x1000) || bomb->mVertAccel!=-0x3000 || bomb->mTerminalVelocity!=-0x50000)return 10;
            // Free-fall state without a collision scene: exercise original dispatch,
            // but leave landing, player hits and sound to the full-scene test.
            for(unsigned i=0;i<60;++i)(bomb->*data_ov098_0213c930[bomb->mState])();
            if(bomb->mState!=1)return 11;
        }
        if(bomb->CleanupResources()!=1)return 12;
        if(!handle(data_ov002_0210da38,0) || !handle(data_ov098_0213c91c,0))return 13;
        delete bomb;
        std::printf("Waterbomb variant=%u assets/release PASS, calls=60 (%s)\n",variant,variant?"fall state only":"idle behavior");std::fflush(stdout);
    }
    if(sm64ds_destroy_overlay_resources(98)!=1)return 14;
    unsigned shared=sm64ds_destroy_overlay_resources(2);
    if(shared!=24)return 15;
    std::printf("smoke_waterbomb: PASS three variants, native states, cleanup shared=%u\n",shared);
    return 0;
}
