#include "unmatched/actor_registry.h"
#include "ntr/mmio.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
#include <cstring>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
extern void* data_020aa3f0;
void* sm64ds_resolve_level_address(unsigned,unsigned,unsigned);
void* sm64ds_simple_bg_clps(unsigned);
int sm64ds_current_level_overlay();
extern unsigned char sm64ds_overlay102_bss_begin[];
extern unsigned char data_02099e94[8];
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    if(data_020a4bb8!=data_020a4bb8_storage)return 10;
    unsigned char initial_after[8];std::memcpy(initial_after,data_02099e94,8);
    const unsigned ids[]={61,59,49,298,340,341,22,20,21,23,25,24,206,285};
    void* old_profiles[14];
    for(unsigned i=0;i<14;++i)old_profiles[i]=data_020a4bb8[ids[i]];
    void* main_head=data_020aa3f0;
    if(sm64ds_current_level_overlay()!=-1)return 4;
    // Same original address must retain the owning level's native identity.
    auto* castle=sm64ds_resolve_level_address(9,0x02112c18,16);
    auto* other=sm64ds_resolve_level_address(16,0x02113550,16);
    if(!castle || !other || other==sm64ds_resolve_level_address(9,0x02113550,16) || !sm64ds_resolve_level_address(9,0x02113550,16))return 5;
    if(sm64ds_resolve_level_address(9,0xffffffff,16) || sm64ds_resolve_level_address(9,0x02112c18,0xffffffff))return 6;
    const unsigned levels[]={16,16,15,9,9};
    const unsigned addresses[]={0x02113ccc,0x02113cec,0x021136f4,0x02112c18,0x02112c58};
    for(unsigned i=0;i<5;++i){
        auto* p=static_cast<unsigned char*>(sm64ds_resolve_level_address(levels[i],addresses[i],8));
        if(!p || std::memcmp(p,"CLPS",4))return 7;
        unsigned size=8+8*(unsigned(p[6])+(unsigned(p[7])<<8));
        if(!sm64ds_resolve_level_address(levels[i],addresses[i],size))return 8;
    }
    LoadOverlay(9);void* castle_head=data_020aa3f0;
    if(sm64ds_current_level_overlay()!=9 || sm64ds_simple_bg_clps(4)!=castle ||
       sm64ds_simple_bg_clps(5)!=sm64ds_resolve_level_address(9,0x02112c58,16) ||
       std::memcmp(sm64ds_simple_bg_clps(3),"CLPS",4))return 9;
    unsigned char pristine[960];unsigned failures=0;
    for(unsigned cycle=0;cycle<3;++cycle) {
        LoadOverlay(102);
        if(sm64ds_try_load_overlay(102)!=OVERLAY_OVERLAP)++failures;
        unsigned count=0;
        for(auto* n=static_cast<void**>(data_020aa3f0);n!=castle_head;n=static_cast<void**>(n[0])) {
            if(!n || ++count>100)return 3;
        }
        if(count!=26)++failures;
        if(std::memcmp(initial_after,data_02099e94,8))++failures;
        for(unsigned i=0;i<14;++i) {
            auto* profile=static_cast<unsigned char*>(data_020a4bb8_storage[ids[i]]);
            if(!profile || !*reinterpret_cast<void**>(profile) || *reinterpret_cast<unsigned short*>(profile+4)!=ids[i])++failures;
        }
        if(cycle==0) {
            std::memcpy(pristine,sm64ds_overlay102_bss_begin,960);
        } else {
            if(std::memcmp(pristine,sm64ds_overlay102_bss_begin,960))++failures;
        }
        // Poison padding after destructor records have been consumed, so the
        // next load must restore every byte of the contiguous overlay BSS.
        UnloadOverlay(102);
        std::memset(sm64ds_overlay102_bss_begin,0xa5,960);
        if(data_020aa3f0!=castle_head)++failures;
        for(unsigned i=0;i<14;++i)if(data_020a4bb8[ids[i]]!=old_profiles[i])++failures;
        if(sm64ds_try_unload_overlay(102)!=OVERLAY_NOT_LOADED)++failures;
        std::printf("overlay102 reload: cycle=%u registrations=%u failures=%u\n",cycle,count,failures);
    }
    UnloadOverlay(9);if(data_020aa3f0!=main_head)++failures;
    std::printf("smoke_overlay102_reload: %s profiles=14 bss=960\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
