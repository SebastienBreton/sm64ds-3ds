#pragma once
// visual-enhancements: HD replacement of one static DS BG layer (the title
// logo "SUPER MARIO 64 DS" = Engine B BG3, 8bpp, priority 2, over the BG2 sky,
// under the OBJ sparkles/star/"TOUCH TO START").
//
// CPU side only (no citro3d): layer extraction from the PPU candidate planes,
// one-shot upscale (enhance_upscale.h) cached by content hash, conversion to
// a PICA RGBA8 tiled texture image, and the per-frame "occluder" image (DS
// pixels that are in front of the layer, redrawn over the HD layer).
// The GPU part (texture upload + 2 quads in screen_scale::draw) is Phase 2.
//
// Panel geometry: the DS 256x192 screen is shown at 1.25x in a 320x240 area
// at LCD x=40 (menu DS layout). A DS rect whose origin is a multiple of 4 maps
// to whole LCD pixels; the HD image is produced at exactly 1.25x of the rect.
#include "enhance_upscale.h"
#include <cstdint>

namespace enhance {

constexpr int kDsW = 256, kDsH = 192;

// Raw views of ntr::PpuPreparedCandidates (kept free of ntr headers so the
// host tools can feed dumps). Colours 0xAARRGGBB, layer ids 0..3 BG, 4 OBJ,
// 5 backdrop (low 3 bits; bit7 = semi-transparent OBJ).
struct PlaneView {
    const uint32_t* front;      // 256*192
    const uint32_t* back;
    const uint8_t* front_layer;
    const uint8_t* front_priority;
    const uint8_t* back_layer;
    const uint8_t* back_priority;
};

struct Rect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; int w() const { return x1 - x0; } int h() const { return y1 - y0; } bool empty() const { return x1 <= x0 || y1 <= y0; } };

// Pixels of `layer` (front, else back candidate). Returns the bounding box
// (empty if absent). `out` is the full 256x192 image (alpha 0 elsewhere).
// `hidden` (optional) counts pixels where the layer is neither front nor
// back but would be expected (cannot be known: returns only coverage).
Rect extract_layer(const PlaneView& v, unsigned layer, Image& out);

// Grow `r` to multiples of 4 (whole LCD pixels at 1.25x), clamped to the screen.
Rect align4(Rect r);

// Merge `more` into `acc` where acc is transparent (union over frames).
void merge_layer(Image& acc, const Image& more);

// HD image for a layer crop: upscale(crop, method, 1.25*w, 1.25*h).
// Cached: same crop content + method -> same result, no recomputation.
struct HdLayer {
    uint64_t key = 0;           // content_hash(crop) ^ method
    Rect ds;                    // DS rect of the crop (aligned to 4)
    Image hd;                   // 1.25x RGBA (straight alpha)
    Method method = Method::Xbr;
    double build_ms = 0;        // measured by the caller (diagnostics)
};
bool build_hd_layer(const Image& full_layer, Rect aligned, Method m, HdLayer& out);

// Occluder: DS pixels inside `r` whose front candidate is drawn in front of
// `layer` (any OBJ, or a BG with a smaller priority value / equal priority and
// smaller BG index). Output: r.w() x r.h(), alpha 255 where occluding.
// Returns the number of occluding pixels (0 -> no occluder draw needed).
int build_occluder(const PlaneView& v, unsigned layer, unsigned layer_priority, Rect r, Image& out);

// PICA RGBA8 tiled texture image (8x8 Morton tiles, texel u32 = 0xRRGGBBAA,
// rows stored bottom-up like the port's other uploads). Verified in Azahar
// (ve_bench): image pixel (x,y) is sampled at (u,v) = ((x+.5)/tex_w,
// (y+.5)/tex_h), i.e. v grows downwards with the image; tex_w/tex_h powers of two >= 8
// and >= src size. Unused texels are transparent.
void to_pica_rgba8(const Image& src, uint32_t* dst, int tex_w, int tex_h);
// Inverse (host tests / readback checks).
Image from_pica_rgba8(const uint32_t* src, int tex_w, int tex_h, int w, int h);

// Smallest power of two >= n (>= 8, <= 1024).
int pot(int n);

}  // namespace enhance
