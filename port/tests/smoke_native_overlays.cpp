#include "ntr/mmio.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
extern unsigned* data_02092208[];
extern void* data_020aa3f0;
}
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    unsigned failures=0,tested=0;
    auto* original_head=data_020aa3f0;
    for(unsigned i=0;i<sm64ds_native_overlay_count;++i) {
        const auto& ov=sm64ds_native_overlays[i];
        if(!ov.kind || ov.kind>=3)continue; // Actor overlays have dedicated lifecycle probes.
        const unsigned root=data_02092208[ov.id-8][0];
        for(unsigned cycle=0;cycle<2;++cycle) {
            LoadOverlay(ov.id);
            if(data_02092208[ov.id-8][0]!=root)++failures;
            if(sm64ds_try_load_overlay(ov.id)!=OVERLAY_OVERLAP)++failures;
            const unsigned conflict=ov.id==8?9:8;
            if(sm64ds_try_load_overlay(conflict)!=OVERLAY_OVERLAP)++failures;
            data_02092208[ov.id-8][0]=0;
            UnloadOverlay(ov.id);
            if(data_020aa3f0!=original_head)++failures;
            if(sm64ds_try_unload_overlay(ov.id)!=OVERLAY_NOT_LOADED)++failures;
        }
        ++tested;
    }
    if(tested!=18)++failures;
    unsigned placeholders=0;
    for(unsigned i=0;i<sm64ds_native_overlay_count;++i){
        const auto& ov=sm64ds_native_overlays[i];if(ov.kind!=6)continue;
        for(unsigned cycle=0;cycle<2;++cycle){
            LoadOverlay(ov.id);
            if(sm64ds_try_load_overlay(ov.id)!=OVERLAY_OVERLAP)++failures;
            UnloadOverlay(ov.id);
            if(data_020aa3f0!=original_head)++failures;
            if(sm64ds_try_unload_overlay(ov.id)!=OVERLAY_NOT_LOADED)++failures;
        }
        ++placeholders;
    }
    if(placeholders!=13)++failures;
    std::printf("empty placeholders=%u cycles=%u\n",placeholders,2*placeholders);
    // Overlay58 has actor code despite having neither BSS nor constructors.
    if(sm64ds_try_load_overlay(58)!=OVERLAY_UNSUPPORTED)++failures;
    if(sm64ds_try_load_overlay(10)!=OVERLAY_UNSUPPORTED)++failures;
    if(sm64ds_try_load_overlay(55)!=OVERLAY_UNSUPPORTED)++failures;
    if(sm64ds_try_load_overlay(999)!=OVERLAY_UNKNOWN)++failures;
    std::printf("smoke_native_overlays: %s supported=%u cycles=%u failures=%u\n",failures?"FAIL":"PASS",tested,tested*2,failures);
    return failures?1:0;
}
