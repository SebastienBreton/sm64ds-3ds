#include "common.h"
#include "daObjBlockS_c.h"
#include "ntr/mmio.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
void func_02059bc0();
void __sinit_02075154();void __sinit_0207501c();void __sinit_02073a24();void __sinit_02074da8();
void __sinit_ov098_0213c058();
int sm64ds_set_overlay_scope(int);unsigned sm64ds_destroy_overlay_resources(int);
daObjBlockS_c* daObjBlockS_c_classInit();
extern unsigned char data_ov098_0213c850[],data_ov098_0213c858[];
using State=void(daObjBlockS_c::*)();
extern State data_ov098_0213c878[];
}
static bool handle(unsigned char* h,unsigned count) {return h[2]==count && bool(*reinterpret_cast<void**>(h+4))==bool(count);}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    func_02059bc0();__sinit_02075154();__sinit_0207501c();__sinit_02073a24();__sinit_02074da8();
    LoadOverlay(98);
    auto* block=daObjBlockS_c_classInit();if(!block)return 3;
    block->actorID=194;block->mPosX=0;block->mPosY=0x400000;block->mPosZ=0;
    block->mAngleX=0;block->mAngleY=0;block->mAngleZ=0;
    std::printf("Small block factory size=%u\n",unsigned(sizeof(*block)));std::fflush(stdout);
    if(block->InitResources()!=1)return 4;
    if(block->mState!=0 || block->mHomePosY!=0x400000 || block->mVertAccel!=-0x2000 || block->mTerminalVelocity!=-0x3c000)return 5;
    if(!handle(data_ov098_0213c850,1) || !handle(data_ov098_0213c858,1))return 6;
    if(data_ov098_0213c878[0]!=&daObjBlockS_c::func_ov098_02138b18 || data_ov098_0213c878[1]!=&daObjBlockS_c::func_ov098_021389f8)return 7;
    block->mFlags &= ~8u;
    for(unsigned i=0;i<10;++i)if(block->Behavior()!=1)return 8;
    if(block->mPosY>=0x400000 || block->mVertSpeed>=0)return 9;
    std::printf("Small block free fall: y=%d vertical=%d state=%d\n",block->mPosY,block->mVertSpeed,block->mState);std::fflush(stdout);
    if(block->CleanupResources()!=1 || block->mMeshCollider.IsEnabled())return 10;
    if(!handle(data_ov098_0213c850,0) || !handle(data_ov098_0213c858,0))return 11;
    delete block;UnloadOverlay(98);
    if(sm64ds_try_unload_overlay(98)!=OVERLAY_NOT_LOADED)return 12;
    std::puts("smoke_smallblock: PASS resources, idle dispatch/free fall, cleanup");return 0;
}
