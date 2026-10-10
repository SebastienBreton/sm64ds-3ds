#include "unmatched/actor_registry.h"
#include "ntr/mmio.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
#include <cstring>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
extern void* data_020aa3f0;
extern unsigned char sm64ds_overlay89_bss_begin[];
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    const unsigned ids[]={282,283};
    void* old_profiles[2];
    for(unsigned i=0;i<2;++i)old_profiles[i]=data_020a4bb8[ids[i]];
    void* main_head=data_020aa3f0;
    LoadOverlay(9);void* castle_head=data_020aa3f0;
    unsigned char pristine[256];unsigned failures=0;
    for(unsigned cycle=0;cycle<3;++cycle) {
        LoadOverlay(89);
        if(sm64ds_try_load_overlay(89)!=OVERLAY_OVERLAP)++failures;
        unsigned count=0;
        for(auto* n=static_cast<void**>(data_020aa3f0);n!=castle_head;n=static_cast<void**>(n[0])) {
            if(!n || ++count>100)return 3;
        }
        if(count!=8)++failures;
        for(unsigned i=0;i<2;++i) {
            auto* profile=static_cast<unsigned char*>(data_020a4bb8[ids[i]]);
            if(!profile || !*reinterpret_cast<void**>(profile) || *reinterpret_cast<unsigned short*>(profile+4)!=ids[i])++failures;
        }
        if(cycle==0) {
            std::memcpy(pristine,sm64ds_overlay89_bss_begin,256);
        } else {
            if(std::memcmp(pristine,sm64ds_overlay89_bss_begin,256))++failures;
        }
        // Poison padding after destructor records have been consumed, so the
        // next load must restore every byte of the contiguous overlay BSS.
        UnloadOverlay(89);
        std::memset(sm64ds_overlay89_bss_begin,0xa5,256);
        if(data_020aa3f0!=castle_head)++failures;
        for(unsigned i=0;i<2;++i)if(data_020a4bb8[ids[i]]!=old_profiles[i])++failures;
        if(sm64ds_try_unload_overlay(89)!=OVERLAY_NOT_LOADED)++failures;
        std::printf("overlay89 reload: cycle=%u registrations=%u failures=%u\n",cycle,count,failures);
    }
    UnloadOverlay(9);if(data_020aa3f0!=main_head)++failures;
    std::printf("smoke_overlay89_reload: %s profiles=2 bss=256\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
