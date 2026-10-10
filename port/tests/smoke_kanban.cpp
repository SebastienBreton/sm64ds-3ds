#include "common.h"
#include "daObjKanban_c.h"
#include "ntr/mmio.h"
#include <cstdio>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
void func_02059bc0();
void __sinit_02075154();
void __sinit_0207501c();
void __sinit_02073a24();
void __sinit_02074da8();
void __sinit_ov085_0212fdb0();
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
daObjKanban_c* daObjKanban_c_classInit();
extern unsigned char data_ov085_02130858[];
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    func_02059bc0();__sinit_02075154();__sinit_0207501c();
    __sinit_02073a24();__sinit_02074da8();
    const int previous=sm64ds_set_overlay_scope(85);
    __sinit_ov085_0212fdb0();sm64ds_set_overlay_scope(previous);
    auto* sign=daObjKanban_c_classInit();
    if(!sign)return 3;
    std::printf("Kanban factory: %p size=%u\n",static_cast<void*>(sign),unsigned(sizeof(*sign)));std::fflush(stdout);
    if(sign->InitResources()!=1)return 4;
    if(!data_ov085_02130858[2] || !*reinterpret_cast<void**>(data_ov085_02130858+4))return 5;
    for(unsigned i=0;i<60;++i)if(sign->Behavior()!=1)return 6;
    if(sign->mTalkingPlayer || sign->mState)return 7;
    std::printf("Kanban behavior: calls=60 state=%u resource=1\n",sign->mState);
    if(sign->CleanupResources()!=1)return 8;
    if(data_ov085_02130858[2] || *reinterpret_cast<void**>(data_ov085_02130858+4))return 9;
    delete sign;
    if(sm64ds_destroy_overlay_resources(85)!=1)return 10;
    std::puts("smoke_kanban: PASS factory, model, idle behavior, cleanup");
    return 0;
}
