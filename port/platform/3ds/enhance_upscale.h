#pragma once
// visual-enhancements: CPU pixel-art upscalers for DS 2D art (title logo,
// menu BG/OBJ layers). Pure C++ (no libctru/citro3d): built into the 3DS game
// AND into the host-side tools (build3ds/validation/visual-enhancements/tools).
//
// Pixels are 0xAARRGGBB, straight alpha. DS 2D layers have binary alpha
// (palette index 0 = transparent); results have partial alpha on edges.
// All functions are one-shot (load-time) work: nothing here runs per frame.
//
// Licences: MMPX (McGuire & Gagiu 2021), Super-xBR and 4xBR (Hyllian) are
// MIT-licensed; re-implemented here from MIT reference code (mmpx-rs,
// super-xbr npm, xbr-js npm).
// xBRZ/hqx (GPL/LGPL) are deliberately NOT included.
#include <cstdint>
#include <vector>

namespace enhance {

struct Image {
    int width = 0, height = 0;
    std::vector<uint32_t> px;   // row-major, 0xAARRGGBB
    Image() = default;
    Image(int w, int h, uint32_t fill = 0) : width(w), height(h), px(size_t(w) * size_t(h), fill) {}
    uint32_t at(int x, int y) const {   // clamped (edge duplicate)
        x = x < 0 ? 0 : (x >= width ? width - 1 : x);
        y = y < 0 ? 0 : (y >= height ? height - 1 : y);
        return px[size_t(y) * size_t(width) + size_t(x)];
    }
};

enum class Method : uint8_t {
    Nearest = 0,     // DS look, integer-ish replication (reference)
    Sharp,           // current port look: nearest x2 then area/bilinear down
    Bilinear,        // plain bilinear (blurry reference)
    Epx,             // Scale2x/EPX (exact palette)
    Mmpx,            // MMPX x2 (exact palette, curve/slope aware)
    SuperXbr,        // Super-xBR x2 (edge-directed, gradients; new colours)
    MmpxSuperXbr,    // MMPX x2 then Super-xBR x2
    Xbr,             // Hyllian 4xBR-lv1 (xbr-js port, MIT): xBRZ-like clean outlines
    Count
};
const char* method_name(Method m);
bool method_from_name(const char* name, Method& out);

// Single 2x passes.
Image epx2x(const Image& src);
Image mmpx2x(const Image& src);
Image superxbr2x(const Image& src);     // premultiplied internally
Image xbr4x(const Image& src);          // binary-alpha friendly

// Area (box) resample to an arbitrary size, premultiplied-alpha correct.
Image resize_area(const Image& src, int out_w, int out_h);
Image resize_bilinear(const Image& src, int out_w, int out_h);
Image resize_nearest(const Image& src, int out_w, int out_h);

// Full pipeline: magnify `src` with `method` to (out_w,out_h) (any size;
// typically src*1.25 for the 320x240 DS area of the 400x240 top LCD, or
// src*2.5 for a supersampled texture). Picks the number of 2x passes so the
// final step is always a downscale (area filter), never a blurry upscale.
Image upscale(const Image& src, Method method, int out_w, int out_h);

// Alpha cleanup for DS layers drawn over other layers: alpha below
// `threshold` -> 0 (keeps the cut-out edge crisp against the sky).
void alpha_floor(Image& img, uint8_t threshold);

// 64-bit FNV-1a over pixels: key for a load-time cache of upscaled layers.
uint64_t content_hash(const Image& img);

}  // namespace enhance
