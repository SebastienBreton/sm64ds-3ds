#include "unmatched/actor_registry.h"
#include "ntr/mmio.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
#include <cstring>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
extern void* data_020aa3f0;
extern unsigned char sm64ds_overlay98_bss_begin[],data_ov098_0213c878[],data_ov098_0213c930[];
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    const unsigned ids[]={299,300,194,207,208};
    void* old_profiles[5];
    for(unsigned i=0;i<5;++i)old_profiles[i]=data_020a4bb8[ids[i]];
    void* main_head=data_020aa3f0;
    LoadOverlay(9);void* castle_head=data_020aa3f0;
    unsigned char pristine[352];unsigned failures=0;
    for(unsigned cycle=0;cycle<3;++cycle) {
        LoadOverlay(98);
        if(sm64ds_try_load_overlay(98)!=OVERLAY_OVERLAP)++failures;
        unsigned count=0;
        for(auto* n=static_cast<void**>(data_020aa3f0);n!=castle_head;n=static_cast<void**>(n[0])) {
            if(!n || ++count>100)return 3;
        }
        if(count!=8)++failures;
        for(unsigned i=0;i<5;++i) {
            auto* profile=static_cast<unsigned char*>(data_020a4bb8[ids[i]]);
            if(!profile || !*reinterpret_cast<void**>(profile) || *reinterpret_cast<unsigned short*>(profile+4)!=ids[i])++failures;
        }
        if(cycle==0) {
            std::memcpy(pristine,sm64ds_overlay98_bss_begin,352);
        } else {
            if(std::memcmp(pristine,sm64ds_overlay98_bss_begin,352))++failures;
        }
        std::memset(data_ov098_0213c878,0,112);
        std::memset(data_ov098_0213c930,0,24);
        UnloadOverlay(98);
        if(data_020aa3f0!=castle_head)++failures;
        for(unsigned i=0;i<5;++i)if(data_020a4bb8[ids[i]]!=old_profiles[i])++failures;
        if(sm64ds_try_unload_overlay(98)!=OVERLAY_NOT_LOADED)++failures;
        std::printf("overlay98 reload: cycle=%u registrations=%u failures=%u\n",cycle,count,failures);
    }
    UnloadOverlay(9);if(data_020aa3f0!=main_head)++failures;
    std::printf("smoke_overlay98_reload: %s profiles=5 bss=352\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
