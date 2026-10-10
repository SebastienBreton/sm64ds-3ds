// visual-enhancements: see enhance_overlay.h.
#include "enhance_overlay.h"
#include <cstdio>
#include <cstring>

namespace enhance {

namespace {
struct Item {
    const char* label;
    const char* help;        // shown in the info box
    bool restart;            // boot option ("after restart")
    int count;
    const char* values[6];
};
// Order = display order. Values are indices in the matching Settings field.
const Item kItems[] = {
    {"HD title logo", "Logo redrawn in HD (computed once in the background, cached on SD).", false, 2, {"No", "Yes"}},
    {"Logo method", "xBR: sharp edges. MMPX: exact colours. Super-xBR: softer.", false, 4, {"xBR", "MMPX", "Super-xBR", "EPX"}},
    {"In-game top screen", "Sharp smooth: even pixels (GPU cost: one extra pass).", false, 2, {"Raw pixels", "Sharp smooth"}},
    {"Texture filtering", "Auto: bilinear except cut-out textures (leaves, grids).", false, 3, {"Off", "Auto", "Always"}},
    {"Menu layout", "DS 4:3 = original proportions. Stretched = full screen.", true, 3, {"Auto", "DS 4:3", "Stretched"}},
    {"Menu filter", "Sharp smooth = current look; Pixels = nearest neighbour.", true, 2, {"Sharp smooth", "Pixels"}},
    {"Full-width sky", "Extends the title screen sky over the side bars.", true, 2, {"Yes", "No"}},
    {"FPS counter", "Updates and frames per second (+ GPU time), top left.", false, 3, {"Off", "FPS", "FPS + GPU"}},
    {"Circle Pad", "Analog: speed follows the tilt, no Y needed. DS: D-pad + Y to run.", false, 2, {"DS (digital)", "Analog"}},
    {"Camera (C-Stick)", "DS arrows: like the touch arrows, down=L up=X. Free: analog + tilt.", false, 3, {"Off", "DS arrows", "Free"}},
};
constexpr int kCount = int(sizeof kItems / sizeof kItems[0]);
// Method menu index <-> enhance::Method value (enhance_upscale.h order).
constexpr uint8_t kMethodOf[4] = {7 /*xbr*/, 4 /*mmpx*/, 5 /*superxbr*/, 3 /*epx*/};

int get(const Settings& s, int item) {
    switch (item) {
    case 0: return s.logo_hd;
    case 1: for (int i = 0; i < 4; ++i) if (kMethodOf[i] == s.logo_method) return i; return 0;
    case 2: return int(s.gameplay_filter);
    case 3: return int(s.tex_filter);
    case 4: return int(s.top_layout);
    case 5: return s.screen_filter == ScreenFilter::Sharp ? 0 : 1;
    case 6: return s.full_width_sky ? 0 : 1;
    case 7: return s.show_fps ? (s.gpu_timing ? 2 : 1) : 0;
    case 8: return s.circle_pad_analog;
    case 9: return s.camera_cstick <= 2 ? s.camera_cstick : 1;
    }
    return 0;
}
void set(Settings& s, int item, int v) {
    switch (item) {
    case 0: s.logo_hd = v != 0; break;
    case 1: s.logo_method = kMethodOf[v]; break;
    case 2: s.gameplay_filter = ScreenFilter(v); break;
    case 3: s.tex_filter = TexFilter(v); break;
    case 4: s.top_layout = TopLayout(v); break;
    case 5: s.screen_filter = v == 0 ? ScreenFilter::Sharp : ScreenFilter::Nearest; break;
    case 6: s.full_width_sky = v == 0; break;
    case 7: s.show_fps = v != 0; s.gpu_timing = v == 2; break;
    case 8: s.circle_pad_analog = v != 0; break;
    case 9: s.camera_cstick = uint8_t(v); break;
    }
}

// UTF-8 -> CP437 for the few accented letters French needs.
int next_glyph(const char*& p) {
    const unsigned char c = (unsigned char)*p++;
    if (c < 0x80) return c;
    if ((c & 0xe0) == 0xc0 && *p) {
        const unsigned cp = ((c & 0x1fu) << 6) | ((unsigned char)*p++ & 0x3fu);
        switch (cp) {
        case 0xe9: return 0x82;  // é
        case 0xe8: return 0x8a;  // è
        case 0xea: return 0x88;  // ê
        case 0xe0: return 0x85;  // à
        case 0xe2: return 0x83;  // â
        case 0xe7: return 0x87;  // ç
        case 0xf4: return 0x93;  // ô
        case 0xfb: return 0x96;  // û
        case 0xee: return 0x8c;  // î
        case 0xc9: return 0x90;  // É
        default: return '?';
        }
    }
    while ((unsigned char)*p >= 0x80 && ((unsigned char)*p & 0xc0) == 0x80) ++p;
    return '?';
}
int text_width(const char* s, int scale) {
    int n = 0;
    while (*s) { next_glyph(s); ++n; }
    return n * 8 * scale;
}

inline uint32_t blend(uint32_t dst, uint32_t src) {   // src alpha over dst (both 0xAARRGGBB)
    const unsigned a = src >> 24, ia = 255 - a;
    auto ch = [&](int sh) { return ((((src >> sh) & 255) * a + ((dst >> sh) & 255) * ia + 127) / 255) << sh; };
    const unsigned da = dst >> 24, oa = a + da * ia / 255;
    return (oa << 24) | ch(16) | ch(8) | ch(0);
}
void fill(uint32_t* img, int x0, int y0, int x1, int y1, uint32_t c) {
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < SettingsOverlay::kH; ++y)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < SettingsOverlay::kW; ++x)
            img[y * SettingsOverlay::kW + x] = blend(img[y * SettingsOverlay::kW + x], c);
}
}  // namespace

int SettingsOverlay::item_count() { return kCount; }

int draw_text(uint32_t* rgba, int w, int h, int x, int y, const char* s, uint32_t color, const uint8_t* font, int scale) {
    const int start = x;
    while (*s) {
        const int g = next_glyph(s);
        const uint8_t* rows = font + g * 8;
        for (int ry = 0; ry < 8; ++ry)
            for (int rx = 0; rx < 8; ++rx)
                if (rows[ry] & (0x80 >> rx))
                    for (int sy = 0; sy < scale; ++sy)
                        for (int sx = 0; sx < scale; ++sx) {
                            const int px = x + rx * scale + sx, py = y + ry * scale + sy;
                            if (px >= 0 && py >= 0 && px < w && py < h) rgba[py * w + px] = blend(rgba[py * w + px], color);
                        }
        x += 8 * scale;
    }
    return x - start;
}

void SettingsOverlay::set_cost_note(int item, const char* note) {
    if (item < 0 || item >= kCount || item >= 16) return;
    std::snprintf(notes_[item], sizeof notes_[item], "%s", note ? note : "");
    ++generation_;
}

void SettingsOverlay::change(int item, int dir) {
    Settings s = snapshot();
    const int n = kItems[item].count;
    set(s, item, (get(s, item) + dir + n) % n);
    publish(s);
    dirty_ = true;
}

bool SettingsOverlay::update(uint32_t held, uint32_t down) {
    const bool chord = (held & (kKeySelect | kKeyStart)) == (kKeySelect | kKeyStart);
    if (!chord) { chord_ = 0; chord_latched_ = false; }
    else if (!chord_latched_ && ++chord_ >= kChordFrames) {
        chord_latched_ = true;
        open_ = !open_;
        if (!open_ && dirty_) { save_requested_ = true; dirty_ = false; }
        ++generation_;
        return true;
    }
    if (!open_) return false;
    const int before = selected_;
    const uint32_t gen = generation_;
    if (down & kKeyUp) selected_ = (selected_ + kCount - 1) % kCount;
    if (down & kKeyDown) selected_ = (selected_ + 1) % kCount;
    if (down & (kKeyRight | kKeyA)) change(selected_, +1);
    if (down & kKeyLeft) change(selected_, -1);
    if (down & kKeyX) { Settings r = Settings::recommended(); Settings cur = snapshot();
        r.top_layout = cur.top_layout; r.screen_filter = cur.screen_filter; r.full_width_sky = cur.full_width_sky;
        r.circle_pad_analog = cur.circle_pad_analog;   // input choice is not a display preset
        r.camera_cstick = cur.camera_cstick;
        publish(r); dirty_ = true; }
    if (down & kKeyY) { Settings r = Settings::safe(); r.circle_pad_analog = snapshot().circle_pad_analog;
        r.camera_cstick = snapshot().camera_cstick;
        publish(r); dirty_ = true; }
    if (down & kKeyB) {
        open_ = false;
        if (dirty_) { save_requested_ = true; dirty_ = false; }
    }
    if (selected_ != before || (down & (kKeyRight | kKeyA | kKeyLeft | kKeyX | kKeyY | kKeyB)) || gen != generation_) ++generation_;
    return true;   // swallow everything while open
}

void SettingsOverlay::render(uint32_t* img, const uint8_t* font) const {
    // Opaque dark panel (the bottom screen game image stays hidden: no
    // alpha blending needed on the GPU side, simplest and cheapest draw).
    for (int y = 0; y < kH; ++y) {
        const unsigned t = unsigned(y) * 40u / kH;
        const uint32_t c = 0xff000000u | ((10u + t / 2) << 16) | ((18u + t / 2) << 8) | (40u + t);
        for (int x = 0; x < kW; ++x) img[y * kW + x] = c;
    }
    const uint32_t white = 0xffffffffu, grey = 0xffa8b0c8u, gold = 0xffffd23cu, dim = 0xff7f879fu;
    const char* title = "OPTIONS";
    draw_text(img, kW, kH, (kW - text_width(title, 2)) / 2, 6, title, gold, font, 2);
    fill(img, 8, 25, kW - 8, 26, 0x80ffffffu);
    const Settings s = snapshot();
    const int row_h = 15, top = 31;
    for (int i = 0; i < kCount; ++i) {
        const int y = top + i * row_h;
        const bool sel = i == selected_;
        if (sel) fill(img, 4, y - 2, kW - 4, y + 10, 0x603c78ffu);
        char label[64];
        std::snprintf(label, sizeof label, "%s%s", kItems[i].label, kItems[i].restart ? "*" : "");
        draw_text(img, kW, kH, 10, y, label, sel ? white : grey, font);
        const char* value = kItems[i].values[get(s, i)];
        const int vw = text_width(value, 1);
        int vx = kW - 12 - vw;
        if (sel) {
            draw_text(img, kW, kH, vx - 12, y, "<", gold, font);
            draw_text(img, kW, kH, kW - 12, y, ">", gold, font);
        }
        draw_text(img, kW, kH, vx, y, value, sel ? gold : white, font);
    }
    // Info box: help text of the selected option (wrapped on 2 lines) + cost.
    const int by = top + kCount * row_h + 2;
    fill(img, 4, by, kW - 4, by + 34, 0x50000000u);
    // word wrap at 37 glyphs, 2 lines
    {
        const char* p = kItems[selected_].help;
        char line[128]; int len = 0, glyphs = 0, row = 0;
        while (*p && row < 2) {
            const char* w = p; int wg = 0;                 // next word (+ trailing space)
            while (*p && *p != ' ') { next_glyph(p); ++wg; }
            if (*p == ' ') { ++p; ++wg; }
            if (glyphs + wg > 38 && glyphs) {
                line[len] = 0; draw_text(img, kW, kH, 10, by + 3 + row * 10, line, white, font);
                ++row; len = 0; glyphs = 0;
                if (row >= 2) break;
            }
            const int bytes = int(p - w);
            if (len + bytes < int(sizeof line) - 1) { std::memcpy(line + len, w, size_t(bytes)); len += bytes; glyphs += wg; }
        }
        if (row < 2 && len) { line[len] = 0; draw_text(img, kW, kH, 10, by + 3 + row * 10, line, white, font); }
    }
    if (notes_[selected_][0]) draw_text(img, kW, kH, 10, by + 24, notes_[selected_], gold, font);
    draw_text(img, kW, kH, 4, kH - 10, "A:set B:close X:recommended Y:original", dim, font);
    // "*" legend just above the help line
    if (!notes_[selected_][0]) { const char* legend = "* after restart"; draw_text(img, kW, kH, kW - 8 - text_width(legend, 1), by + 24, legend, dim, font); }
}

}  // namespace enhance
