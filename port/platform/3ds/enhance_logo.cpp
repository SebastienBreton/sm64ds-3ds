// visual-enhancements: see enhance_logo.h.
#include "enhance_logo.h"
#include <algorithm>

namespace enhance {

Rect extract_layer(const PlaneView& v, unsigned layer, Image& out) {
    out = Image(kDsW, kDsH, 0);
    Rect r{kDsW, kDsH, 0, 0};
    for (int y = 0; y < kDsH; ++y)
        for (int x = 0; x < kDsW; ++x) {
            const int i = y * kDsW + x;
            uint32_t c;
            if ((v.front_layer[i] & 7u) == layer) c = v.front[i];
            else if ((v.back_layer[i] & 7u) == layer) c = v.back[i];
            else continue;
            out.px[size_t(i)] = c | 0xff000000u;
            r.x0 = std::min(r.x0, x); r.y0 = std::min(r.y0, y);
            r.x1 = std::max(r.x1, x + 1); r.y1 = std::max(r.y1, y + 1);
        }
    if (r.x1 <= r.x0) r = Rect{};
    return r;
}

Rect align4(Rect r) {
    r.x0 &= ~3; r.y0 &= ~3;
    r.x1 = std::min(kDsW, (r.x1 + 3) & ~3);
    r.y1 = std::min(kDsH, (r.y1 + 3) & ~3);
    return r;
}

void merge_layer(Image& acc, const Image& more) {
    if (acc.px.size() != more.px.size()) { acc = more; return; }
    for (size_t i = 0; i < acc.px.size(); ++i)
        if (!(acc.px[i] >> 24) && (more.px[i] >> 24)) acc.px[i] = more.px[i];
}

bool build_hd_layer(const Image& full, Rect r, Method m, HdLayer& out) {
    if (r.empty()) return false;
    Image crop(r.w(), r.h());
    for (int y = 0; y < r.h(); ++y)
        for (int x = 0; x < r.w(); ++x)
            crop.px[size_t(y) * r.w() + x] = full.px[size_t(r.y0 + y) * kDsW + r.x0 + x];
    const uint64_t key = content_hash(crop) ^ (uint64_t(m) * 0x9e3779b97f4a7c15ull);
    if (out.key == key && out.ds.x0 == r.x0 && out.ds.y0 == r.y0 && !out.hd.px.empty()) return true;
    out.key = key; out.ds = r; out.method = m;
    out.hd = upscale(crop, m, r.w() * 5 / 4, r.h() * 5 / 4);
    return true;
}

int build_occluder(const PlaneView& v, unsigned layer, unsigned prio, Rect r, Image& out) {
    out = Image(r.w(), r.h(), 0);
    int n = 0;
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x) {
            const int i = y * kDsW + x;
            const unsigned fl = v.front_layer[i] & 7u, fp = v.front_priority[i];
            if (fl == layer || fl == 5) continue;   // the layer itself / backdrop
            bool front;
            if (fl == 4) front = fp <= prio;        // OBJ wins ties against BGs
            else front = fp < prio || (fp == prio && fl < layer);
            if (!front) continue;
            out.px[size_t(y - r.y0) * r.w() + (x - r.x0)] = v.front[i] | 0xff000000u;
            ++n;
        }
    return n;
}

namespace {
inline unsigned morton8(unsigned x, unsigned y) {
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}
}

void to_pica_rgba8(const Image& src, uint32_t* dst, int tw, int th) {
    std::fill_n(dst, size_t(tw) * th, 0u);
    for (int y = 0; y < src.height && y < th; ++y) {
        const unsigned py = unsigned(th - 1 - y);
        for (int x = 0; x < src.width && x < tw; ++x) {
            const uint32_t c = src.px[size_t(y) * src.width + x];
            const unsigned idx = ((py >> 3) * unsigned(tw / 8) + unsigned(x >> 3)) * 64u + morton8(unsigned(x) & 7u, py & 7u);
            dst[idx] = (c << 8) | (c >> 24);   // 0xAARRGGBB -> 0xRRGGBBAA
        }
    }
}

Image from_pica_rgba8(const uint32_t* s, int tw, int th, int w, int h) {
    Image o(w, h);
    for (int y = 0; y < h; ++y) {
        const unsigned py = unsigned(th - 1 - y);
        for (int x = 0; x < w; ++x) {
            const uint32_t c = s[((py >> 3) * unsigned(tw / 8) + unsigned(x >> 3)) * 64u + morton8(unsigned(x) & 7u, py & 7u)];
            o.px[size_t(y) * w + x] = (c >> 8) | (c << 24);
        }
    }
    return o;
}

int pot(int n) {
    int p = 8;
    while (p < n && p < 1024) p <<= 1;
    return p;
}

}  // namespace enhance
