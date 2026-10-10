// analog-pad: see analog_pad.h.
#include "analog_pad.h"
#include "enhance_settings.h"
#include "cstick_camera.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __3DS__
#include <3ds.h>
#endif

extern "C" {
// Original command records (Stage::CheckInput), 4 x 0x18 bytes at 0209f498:
// +0x08 mag, +0x0a nx, +0x0c ny, +0x0e ang (s16), +0x14 touching, +0x16 mode.
extern unsigned char data_0209f498[];
extern unsigned char data_0209f2d8, data_0209d660, data_0209f21c, data_020a0e40;
extern int data_0209caa0[];
extern int data_0209fc48;
short _ZN4cstd5atan2E5Fix12IiES1_(int y, int x);
// Linker --wrap pairs (CMakeLists.txt, smoke_player). Weak: a diagnostic
// link without the wrap options keeps the original calls and these inert.
void __real__ZN5Stage10CheckInputEv(void*) __attribute__((weak));
int __real__ZN7fBase_c14BeforeBehaviorEv(void*) __attribute__((weak));
void __real__ZN7fBase_c13AfterBehaviorEj(void*, unsigned) __attribute__((weak));
extern char _ZTV6dMap_c[];
}

namespace analog_pad {
namespace {
constexpr int kRecord = 0x18;
int g_env = -1;            // SM64DS_ANALOG_PAD: -1 unset, else forced value
bool g_init = false;
bool g_trace = false;
bool g_inject = false;     // harness sample replaces HID
int g_ix = 0, g_iy = 0;
bool g_idpad = false;
unsigned g_applied = 0;
// Set while the current update's local record carries the analog sample.
bool g_injected_this_update = false;
unsigned char* g_hidden_record = nullptr;   // dMap_c is running with touching cleared

void init() {
    if (g_init) return;
    g_init = true;
    if (const char* e = std::getenv("SM64DS_ANALOG_PAD")) g_env = std::strcmp(e, "0") != 0;
    g_trace = std::getenv("SM64DS_ANALOG_TRACE") != nullptr;
    // The settings overlay / a future boot_apply() may already have published
    // the user's settings; otherwise read settings.ini once (absent = default).
    if (enhance::live().generation.load(std::memory_order_acquire) == 0) {
        enhance::Settings s = enhance::Settings::safe();
        const bool loaded = enhance::load_file(enhance::kSettingsPath, s);
        enhance::publish(s);
        std::printf("Analog pad: settings %s circle_pad=%s env=%d\n", loaded ? "loaded" : "absent",
                    s.circle_pad_analog ? "analog" : "ds", g_env);
    }
}

struct Sample { int x, y; bool dpad; };
Sample sample() {
    if (g_inject) return {g_ix, g_iy, g_idpad};
    // visual-enhancements: the settings screen swallows every input.
    if (enhance::live().overlay_open.load(std::memory_order_relaxed)) return {0, 0, false};
#ifdef __3DS__
    // hidScanInput() already ran for this update (func_02019390 wrapper).
    circlePosition c{};
    hidCircleRead(&c);
    return {c.dx, c.dy, (hidKeysHeld() & (KEY_DRIGHT | KEY_DLEFT | KEY_DUP | KEY_DDOWN)) != 0};
#else
    return {0, 0, false};
#endif
}

// Same gate as the original Stage::CheckInput main part (otherwise it cleared
// every record and returned: cut-scenes, wipes, star-select handoff...).
bool check_input_ran() {
    const unsigned m = data_0209f2d8;
    if (m == 1) return true;
    if (m == 2 || !(data_0209caa0[2] & 0x80)) return false;
    return data_0209d660 != 0 || data_0209fc48 == 0;
}
}  // namespace

Result map(int x, int y) {
    const float r = std::sqrt(float(x) * float(x) + float(y) * float(y));
    if (r <= float(kDeadZone)) return {false, 0, 0, 0};
    float t = (r - float(kDeadZone)) / float(kFullRadius - kDeadZone);
    if (t > 1.f) t = 1.f;
    // Linear onto the DS touch-stick range where the original speed curve
    // moves: 0x471 (10 of 36 px, speed 0, turns) .. 0x1000 (36 px, full run).
    const int mag = int(float(kMagStart) + t * float(0x1000 - kMagStart) + 0.5f);
    // DS touch frame: +x right, +y DOWN (the Circle Pad's +y is up).
    const float ux = float(x) / r, uy = -float(y) / r;
    return {true, int16_t(mag), int16_t(std::lround(ux * float(mag))), int16_t(std::lround(uy * float(mag)))};
}

bool enabled() {
    init();
    if (g_env >= 0) return g_env != 0;
    return enhance::live().circle_pad_analog.load(std::memory_order_relaxed);
}

void set_enabled(bool on) {
    init();
    g_env = on ? 1 : 0;
}

// Called after the original Stage::CheckInput.
static void apply() {
    g_injected_this_update = false;
    if (!enabled() || !check_input_ran()) return;
    const unsigned index = data_020a0e40;
    if (index >= 4 || index >= data_0209f21c) return;
    unsigned char* rec = data_0209f498 + index * kRecord;
    // Standard control mode only, not while the touch screen is the stick.
    if (rec[0x16] != 0 || rec[0x14] != 0) return;
    const Sample s = sample();
    if (s.dpad) return;   // the real D-pad keeps the DS digital behaviour
    const Result a = map(s.x, s.y);
    if (!a.active) {
        // Inside the dead zone the HID may still report a digital Circle Pad
        // direction (bits 28-31 -> DS D-pad); analog mode must not walk then.
        if (s.x != 0 || s.y != 0) {
            std::memset(rec + 8, 0, 8);   // mag, nx, ny, ang
        }
        return;
    }
    const short ang = _ZN4cstd5atan2E5Fix12IiES1_(a.nx, a.ny);
    std::memcpy(rec + 0x08, &a.mag, 2);
    std::memcpy(rec + 0x0a, &a.nx, 2);
    std::memcpy(rec + 0x0c, &a.ny, 2);
    std::memcpy(rec + 0x0e, &ang, 2);
    rec[0x14] = 1;
    g_injected_this_update = true;
    ++g_applied;
    if (g_trace) {
        static int px = 1 << 20, py;
        if (s.x != px || s.y != py)
            std::printf("Analog pad: raw=%d,%d mag=%04x n=%d,%d ang=%04x\n", s.x, s.y, unsigned(a.mag) & 0xffff,
                        a.nx, a.ny, unsigned(ang) & 0xffff);
        px = s.x; py = s.y;
    }
}

}  // namespace analog_pad

extern "C" {
void sm64ds_analog_pad_inject(int active, int x, int y, int dpad) {
    analog_pad::g_inject = active != 0;
    analog_pad::g_ix = x; analog_pad::g_iy = y; analog_pad::g_idpad = dpad != 0;
}
unsigned sm64ds_analog_pad_applied() { return analog_pad::g_applied; }

void __wrap__ZN5Stage10CheckInputEv(void* self) {
    __real__ZN5Stage10CheckInputEv(self);
    analog_pad::apply();
    // cstick-camera: C-Stick camera commands on the same command record
    // (apply() above already loaded settings.ini when nobody published it).
    cstick_camera::after_check_input();
}

// dMap_c draws the touch-stick circle whenever touching=1. Only the analog
// sample is hidden, and only while dMap_c's own update runs.
static bool is_map(void* self) {
    return *static_cast<void**>(self) == static_cast<void*>(_ZTV6dMap_c + 8);
}
int __wrap__ZN7fBase_c14BeforeBehaviorEv(void* self) {
    const int result = __real__ZN7fBase_c14BeforeBehaviorEv(self);
    if (analog_pad::g_injected_this_update && is_map(self)) {
        unsigned char* rec = data_0209f498 + data_020a0e40 * analog_pad::kRecord;
        if (rec[0x14]) { rec[0x14] = 0; analog_pad::g_hidden_record = rec; }
    }
    return result;
}
void __wrap__ZN7fBase_c13AfterBehaviorEj(void* self, unsigned status) {
    if (analog_pad::g_hidden_record && is_map(self)) {
        analog_pad::g_hidden_record[0x14] = 1;
        analog_pad::g_hidden_record = nullptr;
    }
    __real__ZN7fBase_c13AfterBehaviorEj(self, status);
}
}
