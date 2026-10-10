#include "unmatched/actor_registry.h"
#include "ntr/mmio.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
#include <cstring>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
extern void* data_020aa3f0;
extern unsigned char sm64ds_overlay85_bss_begin[],data_ov085_0212fe88[];
extern unsigned data_ov085_021306ec[];
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    const unsigned ids[]={185,186,187,229,235,183};
    void* old_profiles[6];
    for(unsigned i=0;i<6;++i)old_profiles[i]=data_020a4bb8[ids[i]];
    void* main_head=data_020aa3f0;
    LoadOverlay(9);void* castle_head=data_020aa3f0;
    unsigned char pristine[1024],toad_states[40];unsigned failures=0;
    for(unsigned cycle=0;cycle<3;++cycle) {
        LoadOverlay(85);
        if(sm64ds_try_load_overlay(85)!=OVERLAY_OVERLAP)++failures;
        unsigned count=0;
        for(auto* n=static_cast<void**>(data_020aa3f0);n!=castle_head;n=static_cast<void**>(n[0])) {
            if(!n || ++count>100)return 3;
        }
        if(count!=28)++failures;
        for(unsigned i=0;i<6;++i) {
            auto* profile=static_cast<unsigned char*>(data_020a4bb8[ids[i]]);
            if(!profile || !*reinterpret_cast<void**>(profile) || *reinterpret_cast<unsigned short*>(profile+4)!=ids[i])++failures;
        }
        if(cycle==0) {
            std::memcpy(pristine,sm64ds_overlay85_bss_begin,1024);
            std::memcpy(toad_states,data_ov085_0212fe88,40);
        } else {
            if(std::memcmp(pristine,sm64ds_overlay85_bss_begin,1024))++failures;
            if(std::memcmp(toad_states,data_ov085_0212fe88,40))++failures;
        }
        data_ov085_021306ec[0]^=0x12345678;
        std::memset(data_ov085_0212fe88,0,16);
        UnloadOverlay(85);
        if(data_020aa3f0!=castle_head)++failures;
        for(unsigned i=0;i<6;++i)if(data_020a4bb8[ids[i]]!=old_profiles[i])++failures;
        if(sm64ds_try_unload_overlay(85)!=OVERLAY_NOT_LOADED)++failures;
        std::printf("overlay85 reload: cycle=%u registrations=%u failures=%u\n",cycle,count,failures);
    }
    UnloadOverlay(9);if(data_020aa3f0!=main_head)++failures;
    std::printf("smoke_overlay85_reload: %s profiles=6 bss=1024\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
