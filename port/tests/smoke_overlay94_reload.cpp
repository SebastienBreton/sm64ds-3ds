#include "unmatched/actor_registry.h"
#include "ntr/mmio.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
#include <cstring>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
extern void* data_020aa3f0;
extern unsigned char sm64ds_overlay94_bss_begin[];
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    const unsigned ids[]={234};
    void* old_profiles[1];
    for(unsigned i=0;i<1;++i)old_profiles[i]=data_020a4bb8[ids[i]];
    void* main_head=data_020aa3f0;
    LoadOverlay(9);void* castle_head=data_020aa3f0;
    unsigned char pristine[160];unsigned failures=0;
    for(unsigned cycle=0;cycle<3;++cycle) {
        LoadOverlay(94);
        if(sm64ds_try_load_overlay(94)!=OVERLAY_OVERLAP)++failures;
        unsigned count=0;
        for(auto* n=static_cast<void**>(data_020aa3f0);n!=castle_head;n=static_cast<void**>(n[0])) {
            if(!n || ++count>100)return 3;
        }
        if(count!=4)++failures;
        for(unsigned i=0;i<1;++i) {
            auto* profile=static_cast<unsigned char*>(data_020a4bb8[ids[i]]);
            if(!profile || !*reinterpret_cast<void**>(profile) || *reinterpret_cast<unsigned short*>(profile+4)!=ids[i])++failures;
        }
        if(cycle==0) {
            std::memcpy(pristine,sm64ds_overlay94_bss_begin,160);
        } else {
            if(std::memcmp(pristine,sm64ds_overlay94_bss_begin,160))++failures;
        }
        // Poison padding after destructor records have been consumed, so the
        // next load must restore every byte of the contiguous overlay BSS.
        UnloadOverlay(94);
        std::memset(sm64ds_overlay94_bss_begin,0xa5,160);
        if(data_020aa3f0!=castle_head)++failures;
        for(unsigned i=0;i<1;++i)if(data_020a4bb8[ids[i]]!=old_profiles[i])++failures;
        if(sm64ds_try_unload_overlay(94)!=OVERLAY_NOT_LOADED)++failures;
        std::printf("overlay94 reload: cycle=%u registrations=%u failures=%u\n",cycle,count,failures);
    }
    UnloadOverlay(9);if(data_020aa3f0!=main_head)++failures;
    std::printf("smoke_overlay94_reload: %s profiles=1 bss=160\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
