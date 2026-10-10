#include "common.h"
#include "daObjYajirusi_c.h"
#include "ntr/mmio.h"
#include <cstdio>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
void func_02059bc0();
void __sinit_02075154();
void __sinit_0207501c();
void __sinit_02073a24();
void __sinit_02074da8();
void __sinit_ov098_0213bf9c();
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
int* daObjYajirusi_c_classInit_YAJIRUSI_L();
int* daObjYajirusi_c_classInit_YAJIRUSI_R();
extern unsigned char data_ov098_0213c810[],data_ov098_0213c800[],data_ov098_0213c808[],data_ov098_0213c818[];
}
static bool handle(unsigned char* h,unsigned count) {return h[2]==count && bool(*reinterpret_cast<void**>(h+4))==bool(count);}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    func_02059bc0();__sinit_02075154();__sinit_0207501c();__sinit_02073a24();__sinit_02074da8();
    const int previous=sm64ds_set_overlay_scope(98);__sinit_ov098_0213bf9c();sm64ds_set_overlay_scope(previous);
    unsigned char* models[]={data_ov098_0213c810,data_ov098_0213c808};
    unsigned char* collisions[]={data_ov098_0213c800,data_ov098_0213c818};
    for(unsigned v=0;v<2;++v) {
        auto* sign=reinterpret_cast<daObjYajirusi_c*>(v?daObjYajirusi_c_classInit_YAJIRUSI_R():daObjYajirusi_c_classInit_YAJIRUSI_L());
        if(!sign)return 3;
        sign->actorID=299+v;sign->mPosX=0;sign->mPosY=0;sign->mPosZ=0;sign->mAngleY=0;
        std::printf("Arrow factory variant=%u size=%u\n",v,unsigned(sizeof(*sign)));std::fflush(stdout);
        if(sign->InitResources()!=1 || sign->mVariant!=v)return 4;
        if(!handle(models[v],1) || !handle(collisions[v],1))return 5;
        if(sign->OnAttacked1(*sign)!=int(reinterpret_cast<unsigned long>(sign)))return 6;
        for(unsigned i=0;i<60;++i)if(sign->Behavior()!=1)return 7;
        if(sign->CleanupResources()!=1 || sign->mMeshCollider.IsEnabled())return 8;
        if(!handle(models[v],0) || !handle(collisions[v],0))return 9;
        delete sign;
        std::printf("Arrow variant=%u model/KCL, 60 idle calls, cleanup PASS\n",v);std::fflush(stdout);
    }
    if(sm64ds_destroy_overlay_resources(98)!=4)return 10;
    std::puts("smoke_arrows: PASS two factories, resources, idle behavior, cleanup");
    return 0;
}
