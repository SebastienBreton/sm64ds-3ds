#pragma once
#include <stdint.h>

namespace ntr {

// A coherent engine BG/OBJ view. Its size is a power of two: 512 KiB for
// engine A BG, 256 KiB for A OBJ, and 128 KiB for each engine B view.
// Physical bank routing and overlap are resolved by the memory owner.
struct BitmapMemory {
    const uint8_t* bytes;
    unsigned mask;
};
struct BitmapPixel {
    uint16_t color = 0; // RGB555, independent of bit15 transparency
    uint8_t alpha = 0; // OBJ: attribute alpha+1 in 2..16; BG:16
    explicit operator bool() const { return alpha != 0; }
};

// BG2/3 extended bitmap format. Coordinates are signed 24.8 after affine
// transformation and mosaic. Index zero/direct-color bit15 clear is transparent.
// DISPCNT text-map/character offsets and extended palettes do not apply.
BitmapPixel sample_extended_bitmap(BitmapMemory memory, const uint16_t* palette,
                                   uint16_t bg_control, int32_t x, int32_t y);

// OBJ mode3. Coordinates are integer texels after normal flips/affine transform
// and clipping by the caller. The bitmap mapping uses DISPCNT bits5/6/22, not
// the tile OBJ bits4/20/21. Reserved bits5=1,6=1 draws nothing.
BitmapPixel sample_bitmap_obj(BitmapMemory memory, uint32_t display_control,
                             uint16_t attribute2, unsigned width,
                             unsigned height, int x, int y);
}
