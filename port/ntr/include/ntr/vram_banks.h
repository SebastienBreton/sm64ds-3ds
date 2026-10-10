#pragma once
#include <stdint.h>

namespace ntr {
enum class VramView : unsigned {
    ABg, AObj, BBg, BObj, Texture, TexturePalette,
    ABgPalette, AObjPalette, BBgPalette, BObjPalette, Arm7, Count
};
struct VramSpan { uint8_t* data = nullptr; unsigned bytes = 0; };

// Physical bank ownership stays with IO. No allocation, global state or
// platform dependency. PPU/GX views and CPU aliases all read the same banks.
class VramBanks {
public:
    static unsigned bank_size(unsigned bank);
    void reset(uint8_t* const banks[9]); // resets controls, preserves bytes
    uint8_t map(unsigned bank,uint8_t control); // returns masked latch value
    uint8_t control(unsigned bank) const {return controls_[bank];}
    uint16_t lcdc_mask() const {return lcdc_;}
    uint8_t arm7_status() const;
    uint64_t generation() const {return generation_;}
    uint64_t view_generation(VramView view) const {return view_generation_[unsigned(view)];}
    uint64_t bank_generation(unsigned bank) const {return bank_generation_[bank];}
    uint8_t* physical(unsigned bank) const {return banks_[bank];}
    void changed(unsigned bank,unsigned offset,unsigned bytes);
    void* context = nullptr;
    void (*written)(void*,unsigned bank,unsigned offset,unsigned bytes) = nullptr;
    uint32_t read_cpu(uint32_t address,unsigned width) const;
    void write_cpu(uint32_t address,uint32_t value,unsigned width);
    uint32_t read(VramView view,unsigned offset,unsigned width) const;
    void write_arm7(unsigned offset,uint32_t value,unsigned width);
    void copy_view(VramView view,unsigned offset,uint8_t* output,unsigned bytes) const;
    // Raw pointers are safe only for one mapped bank and this bounded span.
    // Overlap requires explicit read_cpu/write_cpu (OR / broadcast semantics).
    VramSpan unique_cpu_span(uint32_t address) const;
private:
    uint8_t* banks_[9]{};
    uint8_t controls_[9]{};
    uint16_t pages_[unsigned(VramView::Count)][32]{};
    uint16_t lcdc_ = 0;
    uint64_t generation_ = 0;
    uint64_t view_generation_[unsigned(VramView::Count)]{};
    uint16_t bank_views_[9]{};
    uint64_t bank_generation_[9]{};
    void rebuild();
    uint16_t resolve_cpu(uint32_t address,unsigned& offset) const;
    uint16_t resolve(VramView view,unsigned& offset) const;
    uint32_t read_mask(uint16_t mask,unsigned offset,unsigned width) const;
    void write_mask(uint16_t mask,unsigned offset,uint32_t value,unsigned width);
};
}
