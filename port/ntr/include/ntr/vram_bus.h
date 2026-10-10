#pragma once
#include "ntr/vram_banks.h"

namespace ntr {
// IO attaches only after physical backing, VRAMCNT dispatch and render views
// are coherent. A null accessor keeps SDK/GX callers on their existing path.
VramBanks* io_vram_banks();
void vram_attach_cpu_bus(VramBanks* bus);
inline bool vram_cpu_address(uintptr_t address){return (address&0xff000000u)==0x06000000u && address<=0x06ffffffu;}
uint32_t vram_cpu_read(uint32_t address,unsigned width);
void vram_cpu_write(uint32_t address,uint32_t value,unsigned width);
VramSpan vram_cpu_span(uint32_t address);
void vram_physical_changed(unsigned bank,unsigned offset,unsigned bytes);

// Caller-owned read-only raster view. Publishing only a changed domain avoids
// unrelated copies; exact byte snapshots still decide render-cache equality.
struct VramPublishedView {
    VramView view;
    uint8_t* output;
    unsigned bytes;
    uint64_t generation = UINT64_MAX;
    bool publish(const VramBanks& bus){
        const auto next=bus.view_generation(view);
        if(generation==next)return false;
        bus.copy_view(view,0,output,bytes);generation=next;return true;
    }
    void invalidate(){generation=UINT64_MAX;}
};
}
