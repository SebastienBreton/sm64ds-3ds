#pragma once
#include "ntr/host_allocator.h"
#include "ntr/vram_bus.h"
#include <cstring>

namespace ntr {
// Read-only staging is separate from CPU LCDC windows and physical banks.
// Generations only decide whether to publish; immutable texture-cache byte
// comparisons still decide whether decoded pixels can be reused.
class GxTextureViews {
public:
    bool publish(VramBanks* bus) {
        if(!bus){attached_=nullptr;return false;}
        if(attached_!=bus){
            attached_=bus;texture_.resize(0x80000);palette_.resize(0x40000);
            texture_view_.output=texture_.data();palette_view_.output=palette_.data();
            texture_view_.invalidate();palette_view_.invalidate();
        }
        texture_view_.publish(*bus);
        // Palette addresses wrap128KiB, including a palette spanning its end.
        // Mirror only after publication; no per-bind rotation/copy is needed.
        if(palette_view_.publish(*bus))
            std::memcpy(palette_.data()+0x20000,palette_.data(),0x20000);
        return true;
    }
    const uint8_t* texture()const{return texture_.data();}
    const uint8_t* palette()const{return palette_.data();}
    void invalidate(){texture_view_.invalidate();palette_view_.invalidate();}
private:
    VramBanks* attached_=nullptr;
    HostVector<uint8_t> texture_,palette_;
    VramPublishedView texture_view_{VramView::Texture,nullptr,0x80000};
    VramPublishedView palette_view_{VramView::TexturePalette,nullptr,0x20000};
};
}
