#include "ntr/bitmap.h"

namespace ntr {
namespace {
uint16_t word(BitmapMemory memory, unsigned offset) {
    return uint16_t(memory.bytes[offset & memory.mask]) |
           uint16_t(memory.bytes[(offset + 1) & memory.mask]) << 8;
}
}

BitmapPixel sample_extended_bitmap(BitmapMemory memory, const uint16_t* palette,
                                   uint16_t control, int32_t x, int32_t y) {
    if (!memory.bytes || !(control & 0x80)) return {};
    const unsigned size = control >> 14;
    const unsigned width = size < 2 ? 128u << size : 512u;
    const unsigned height = size == 0 ? 128u : size == 3 ? 512u : 256u;
    // Unsigned masks preserve the hardware's fractional-coordinate wrap and
    // also reject negative coordinates when overflow/wrap is disabled.
    const unsigned mx = width * 256 - 1, my = height * 256 - 1;
    const unsigned ux = uint32_t(x), uy = uint32_t(y);
    if (!(control & 0x2000) && ((ux & ~mx) || (uy & ~my))) return {};
    const unsigned texel = ((uy & my) >> 8) * width + ((ux & mx) >> 8);
    const unsigned base = (control & 0x1f00) << 6;
    if (control & 4) {
        const uint16_t color = word(memory, base + texel * 2);
        return color & 0x8000 ? BitmapPixel{uint16_t(color & 0x7fff),16} : BitmapPixel{};
    }
    const unsigned index = memory.bytes[(base + texel) & memory.mask];
    return index && palette ? BitmapPixel{uint16_t(palette[index] & 0x7fff),16} : BitmapPixel{};
}

BitmapPixel sample_bitmap_obj(BitmapMemory memory, uint32_t display,
                             uint16_t attr2, unsigned width,
                             unsigned height, int x, int y) {
    const unsigned alpha = attr2 >> 12;
    if (!memory.bytes || !alpha || unsigned(x) >= width || unsigned(y) >= height)
        return {};
    const unsigned tile = attr2 & 0x3ff;
    unsigned base, stride;
    if (display & 0x40) {
        if (display & 0x20) return {};
        base = tile << (7 + ((display >> 22) & 1));
        stride = width * 2;
    } else if (display & 0x20) {
        base = (tile & 31) * 16 + (tile & 0x3e0) * 128;
        stride = 512;
    } else {
        base = (tile & 15) * 16 + (tile & 0x3f0) * 128;
        stride = 256;
    }
    const uint16_t color = word(memory, base + unsigned(y) * stride + unsigned(x) * 2);
    return color & 0x8000 ? BitmapPixel{uint16_t(color & 0x7fff),uint8_t(alpha+1)} : BitmapPixel{};
}
}
