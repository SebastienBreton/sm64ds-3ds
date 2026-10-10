#include <cstdio>
#include <cstring>
#include <cstdint>
#include "ntr/mmio.h"
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
bool sm64ds_load_castle_overlay();
bool sm64ds_unload_castle_overlay();
extern void* data_020aa3f0;
extern unsigned char sm64ds_castle_bss_begin[];
extern unsigned data_ov009_02113d8c[],data_ov009_02112bdc[];
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    unsigned failures=0;
    unsigned char expected[704];
    auto* original_head=data_020aa3f0;
    unsigned root=0;
    for(unsigned cycle=0;cycle<3;++cycle) {
        if(!sm64ds_load_castle_overlay())return 3;
        if(sm64ds_load_castle_overlay())++failures;
        unsigned registrations=0;
        for(auto *node=static_cast<void**>(data_020aa3f0);node!=original_head;node=static_cast<void**>(node[0])) {
            if(!node || ++registrations>100)return 4;
        }
        if(registrations!=29)++failures;
        if(cycle==0) {std::memcpy(expected,sm64ds_castle_bss_begin,704);root=data_ov009_02112bdc[0];}
        else {
            if(std::memcmp(expected,sm64ds_castle_bss_begin,704))++failures;
            if(data_ov009_02112bdc[0]!=root)++failures;
        }
        // Mutate a spawn vector and a numeric root between successive loads.
        data_ov009_02113d8c[0]^=0x12345678;
        data_ov009_02112bdc[0]=0;
        if(!sm64ds_unload_castle_overlay())++failures;
        if(data_020aa3f0!=original_head)++failures;
        if(sm64ds_unload_castle_overlay())++failures;
        std::printf("castle reload: cycle=%u registrations=%u failures=%u\n",cycle,registrations,failures);
    }
    std::printf("smoke_castle_reload: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
