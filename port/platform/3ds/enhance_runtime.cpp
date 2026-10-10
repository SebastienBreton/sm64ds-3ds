// visual-enhancements phase 2: see enhance_runtime.h.
#include "enhance_runtime.h"
#include "enhance_overlay.h"
#include "enhance_settings.h"
#include <3ds.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

extern "C" const u8 default_font_bin[];   // libctru 8x8 CP437 console font

namespace enhance_runtime {
namespace {
struct Channel {
    int width = 0, height = 0;
    uint32_t* buffer[2] = {nullptr, nullptr};
    std::atomic<uint32_t> published{0};    // 0 = nothing to draw
    std::atomic<int> slot{0};
    std::atomic<uint32_t> acked{0};
    uint32_t next = 1;
    bool allocate(int w, int h) {
        if (buffer[0]) return true;
        width = w; height = h;
        buffer[0] = new (std::nothrow) uint32_t[size_t(w) * h];
        buffer[1] = new (std::nothrow) uint32_t[size_t(w) * h];
        if (!buffer[0] || !buffer[1]) { delete[] buffer[0]; delete[] buffer[1]; buffer[0] = buffer[1] = nullptr; return false; }
        return true;
    }
    // Game thread: may write a new image only once the worker consumed the
    // published one (it never reads the other slot).
    uint32_t* writable() {
        const uint32_t p = published.load(std::memory_order_acquire);
        if (p && acked.load(std::memory_order_acquire) != p) return nullptr;
        return buffer[1 - slot.load(std::memory_order_relaxed)];
    }
    void publish() {
        slot.store(1 - slot.load(std::memory_order_relaxed), std::memory_order_relaxed);
        published.store(++next, std::memory_order_release);
    }
    void hide() { published.store(0, std::memory_order_release); }
};
Channel g_channels[2];
enhance::SettingsOverlay g_overlay;
uint32_t g_rendered = 0;          // overlay generation last rendered
uint32_t g_hide_until_release = 0;
bool g_settings_announced = false;
// FPS strip statistics (worker writes, game thread reads)
std::atomic<uint32_t> g_frames{0};
std::atomic<uint32_t> g_frame_us_total{0};
std::atomic<uint32_t> g_gpu_us_total{0};
uint64_t g_fps_tick = 0;
uint32_t g_fps_updates = 0;

void update_fps_strip() {
    const auto& live = enhance::live();
    Channel& c = g_channels[1];
    ++g_fps_updates;
    if (!live.show_fps.load(std::memory_order_relaxed) || g_overlay.open()) { c.hide(); return; }
    const uint64_t now = svcGetSystemTick();
    if (!g_fps_tick) { g_fps_tick = now; g_fps_updates = 0; return; }
    const double seconds = double(now - g_fps_tick) / double(SYSCLOCK_ARM11);
    if (seconds < .5 && c.published.load(std::memory_order_relaxed)) return;
    if (!c.allocate(256, 16)) return;
    uint32_t* img = c.writable();
    if (!img) return;
    const uint32_t frames = g_frames.exchange(0), frame_us = g_frame_us_total.exchange(0), gpu_us = g_gpu_us_total.exchange(0);
    char text[40];
    const double ups = seconds > 0 ? g_fps_updates / seconds : 0;
    if (live.gpu_timing.load(std::memory_order_relaxed) && frames)
        std::snprintf(text, sizeof text, "%4.1f upd/s  GPU %4.1f ms", ups, gpu_us / 1000.0 / frames);
    else
        std::snprintf(text, sizeof text, "%4.1f upd/s  %4.1f fps", ups, frames / (seconds > 0 ? seconds : 1));
    (void)frame_us;
    for (int i = 0; i < 256 * 16; ++i) img[i] = 0;
    const int width = enhance::draw_text(img, 256, 16, 4, 4, text, 0xffffff60u, default_font_bin);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < width + 8 && x < 256; ++x) if (!(img[y * 256 + x] >> 24)) img[y * 256 + x] = 0xc0000000u;
    c.publish();
    g_fps_tick = now; g_fps_updates = 0;
}
}  // namespace

uint32_t filter_input(uint32_t held, uint32_t down, bool& touch_allowed) {
    if (!g_settings_announced) {
        // boot_apply() (game_entry.cpp) normally published the settings.
        g_settings_announced = true;
        if (enhance::live().generation.load(std::memory_order_acquire) == 0) enhance::publish(enhance::Settings::safe());
    }
    const bool was_open = g_overlay.open();
    const bool swallow = g_overlay.update(held, down);
    const bool open = g_overlay.open();
    if (g_overlay.take_save_request()) {
        const bool ok = enhance::save_file(enhance::kSettingsPath, enhance::snapshot());
        std::printf("Settings overlay: saved %s (%s)\n", enhance::kSettingsPath, ok ? "ok" : "FAILED");
    }
    if (was_open != open) std::printf("Settings overlay %s\n", open ? "opened" : "closed");
    enhance::live().overlay_open.store(open, std::memory_order_release);
    if (was_open && !open) g_hide_until_release = held;
    g_hide_until_release &= held;
    Channel& c = g_channels[0];
    if (open) {
        if (g_overlay.generation() != g_rendered && c.allocate(enhance::SettingsOverlay::kW, enhance::SettingsOverlay::kH)) {
            if (uint32_t* img = c.writable()) {
                g_overlay.render(img, default_font_bin);
                c.publish();
                g_rendered = g_overlay.generation();
            }
        }
    } else if (c.published.load(std::memory_order_relaxed)) {
        c.hide();
        g_rendered = 0;
    }
    update_fps_strip();
    if (swallow || open) { touch_allowed = false; return 0; }
    touch_allowed = true;
    uint32_t keys = held & ~g_hide_until_release;
    if (keys & KEY_SELECT) keys &= ~uint32_t(KEY_START);   // the chord never reaches the game
    return keys;
}

const uint32_t* overlay_image(unsigned kind, uint32_t& generation, int& width, int& height) {
    if (kind > 1) return nullptr;
    Channel& c = g_channels[kind];
    generation = c.published.load(std::memory_order_acquire);
    if (!generation) return nullptr;
    width = c.width; height = c.height;
    return c.buffer[c.slot.load(std::memory_order_relaxed)];
}

void acknowledge(unsigned kind, uint32_t generation) {
    if (kind <= 1) g_channels[kind].acked.store(generation, std::memory_order_release);
}

void note_present(double frame_ms, double gpu_ms) {
    g_frames.fetch_add(1, std::memory_order_relaxed);
    g_frame_us_total.fetch_add(uint32_t(frame_ms * 1000.0), std::memory_order_relaxed);
    g_gpu_us_total.fetch_add(uint32_t(gpu_ms * 1000.0), std::memory_order_relaxed);
}

}  // namespace enhance_runtime
