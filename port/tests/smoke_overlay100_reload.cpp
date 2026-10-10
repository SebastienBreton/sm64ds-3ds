#include "unmatched/actor_registry.h"
#include "ntr/mmio.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
#include <cstring>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
extern void* data_020aa3f0;
extern unsigned char sm64ds_overlay100_bss_begin[];
extern unsigned char data_ov100_021487b4[],data_ov100_021487c0[];
void func_020731dc(void*,void*,void**);
void _ZN7Vector3D1Ev(void*);
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    const unsigned ids[]={336,220,337,353,354,344,31};
    void* old_profiles[7];
    for(unsigned i=0;i<7;++i)old_profiles[i]=data_020a4bb8[ids[i]];
    void* main_head=data_020aa3f0;
    LoadOverlay(9);void* castle_head=data_020aa3f0;
    unsigned char pristine[1152];unsigned failures=0;
    for(unsigned cycle=0;cycle<3;++cycle) {
        LoadOverlay(100);
        if(sm64ds_try_load_overlay(100)!=OVERLAY_OVERLAP)++failures;
        unsigned count=0;
        for(auto* n=static_cast<void**>(data_020aa3f0);n!=castle_head;n=static_cast<void**>(n[0])) {
            if(!n || ++count>100)return 3;
        }
        if(count!=28)++failures;
        for(unsigned i=0;i<7;++i) {
            auto* profile=static_cast<unsigned char*>(data_020a4bb8[ids[i]]);
            if(!profile || !*reinterpret_cast<void**>(profile) || *reinterpret_cast<unsigned short*>(profile+4)!=ids[i])++failures;
        }
        if(cycle==0) {
            std::memcpy(pristine,sm64ds_overlay100_bss_begin,1152);
        } else {
            if(std::memcmp(pristine,sm64ds_overlay100_bss_begin,1152))++failures;
        }
        // Door::InitResources lazily registers this vector outside the load
        // scope. DS unload still owns it because its object lives in ov100 BSS.
        func_020731dc(data_ov100_021487c0,(void*)_ZN7Vector3D1Ev,
                     reinterpret_cast<void**>(data_ov100_021487b4));
        if(data_020aa3f0!=data_ov100_021487b4)++failures;
        // Poison padding after destructor records have been consumed, so the
        // next load must restore every byte of the contiguous overlay BSS.
        UnloadOverlay(100);
        std::memset(sm64ds_overlay100_bss_begin,0xa5,1152);
        if(data_020aa3f0!=castle_head)++failures;
        for(unsigned i=0;i<7;++i)if(data_020a4bb8[ids[i]]!=old_profiles[i])++failures;
        if(sm64ds_try_unload_overlay(100)!=OVERLAY_NOT_LOADED)++failures;
        std::printf("overlay100 reload: cycle=%u registrations=%u failures=%u\n",cycle,count,failures);
    }
    UnloadOverlay(9);if(data_020aa3f0!=main_head)++failures;
    std::printf("smoke_overlay100_reload: %s profiles=7 bss=1152 lazy_door_registration=1\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
