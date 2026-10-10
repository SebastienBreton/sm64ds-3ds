// cstick-camera: see cstick_camera.h.
#include "cstick_camera.h"
#include "enhance_settings.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __3DS__
#include <3ds.h>
#endif

extern "C" {
// Command records (Stage::CheckInput), 4 x 0x18 bytes: +4 held, +6 pressed,
// +0x16 control mode (0 Standard, 1 Touch, 2 Dual-hand).
extern unsigned char data_0209f498[];
extern unsigned char data_0209f2d8, data_0209d660, data_0209f21c, data_020a0e40;
extern unsigned char data_0209f294, data_0209f2c4, data_0209f20c;
extern unsigned char data_ov002_02111180;   // zoom touch button highlight timer
extern int data_0209caa0[];
extern int data_0209fc48;
extern void* data_0209f318;                 // the Camera
}

namespace cstick_camera {
namespace {
constexpr int kRecord = 0x18;
constexpr unsigned kRight = 0x100, kLeft = 0x200, kBehind = 0x4000, kZoom = 0x8000;
constexpr float kPitchRate = 64.f;   // Fix12 of distance per update at full tilt

int g_env = -1;              // SM64DS_CSTICK_CAMERA: -1 unset, else forced mode
bool g_init = false, g_trace = false;
bool g_inject = false;
int g_ix = 0, g_iy = 0;
unsigned g_applied = 0;

// Per-update state (game thread only).
void* g_cam = nullptr;       // camera object the state belongs to
unsigned g_prev = 0;         // command bits the C-Stick wrote last update
int g_hdir = 0, g_vdir = 0;  // DsArrows digital directions (hysteresis)
float g_speed = 0.f;         // Free: yaw speed, s16 units/update (+ = 0x100)
bool g_free_active = false;  // Free: this update's arrow bit is the C-Stick's
int g_step = kDsStep;        // Free: magnitude handed to the camera
int g_pitch = 0;             // Free: tilt offset, Fix12 of camera distance
const void* g_zone = nullptr;

void init() {
    if (g_init) return;
    g_init = true;
    if (const char* e = std::getenv("SM64DS_CSTICK_CAMERA")) g_env = std::atoi(e);
    g_trace = std::getenv("SM64DS_CSTICK_TRACE") != nullptr;
}

struct Sample { int x, y; };
Sample sample() {
    if (g_inject) return {g_ix, g_iy};
    if (enhance::live().overlay_open.load(std::memory_order_relaxed)) return {0, 0};
#ifdef __3DS__
    // hidScanInput() ran for this update; on New 3DS libctru's hidInit also
    // started ir:rst and hidScanInput scans it. Old 3DS: stays {0, 0}.
    circlePosition c{};
    hidCstickRead(&c);
    return {c.dx, c.dy};
#else
    return {0, 0};
#endif
}

// Same gate as the original Stage::CheckInput main part.
bool check_input_ran() {
    const unsigned m = data_0209f2d8;
    if (m == 1) return true;
    if (m == 2 || !(data_0209caa0[2] & 0x80)) return false;
    return data_0209d660 != 0 || data_0209fc48 == 0;
}

// Digital direction with hysteresis; dominant axis only.
int digital(int prev, int v, int other) {
    const int a = std::abs(v);
    const int s = v > 0 ? 1 : -1;
    if (prev != 0 && s == prev && a >= kDigitalOff && a >= std::abs(other) / 2) return prev;
    return (a >= kDigitalOn && a > std::abs(other)) ? s : 0;
}

float curve(int v) {
    const int a = std::abs(v);
    if (a <= kDeadZone) return 0.f;
    float t = float(a - kDeadZone) / float(kFull - kDeadZone);
    if (t > 1.f) t = 1.f;
    return 0.3f * t + 0.7f * t * t;   // fine control near the centre
}

void reset_motion() { g_prev = 0; g_hdir = g_vdir = 0; g_speed = 0.f; }

inline unsigned rd16(const unsigned char* p) { unsigned short v; std::memcpy(&v, p, 2); return v; }
inline void wr16(unsigned char* p, unsigned v) { const unsigned short s = (unsigned short)v; std::memcpy(p, &s, 2); }
}  // namespace

Mode mode() {
    init();
    if (g_env >= 0 && g_env <= 2) return Mode(g_env);
    const unsigned v = enhance::live().camera_cstick.load(std::memory_order_relaxed);
    return v <= 2 ? Mode(v) : Mode::DsArrows;
}

void set_mode(Mode m) { init(); g_env = int(m); }

float free_target_speed(int x) {
    const float c = curve(x);
    return x > 0 ? c * float(kDsStep) : -c * float(kDsStep);
}

float free_smooth(float speed, float target) {
    const bool accelerating = (target > 0.f && speed >= 0.f && target > speed) ||
                              (target < 0.f && speed <= 0.f && target < speed);
    speed += (target - speed) * (accelerating ? 0.30f : 0.45f);
    if (target == 0.f && std::fabs(speed) < 12.f) speed = 0.f;
    return speed;
}

void after_check_input() {
    g_free_active = false;
    const Mode md = mode();
    void* cam = data_0209f318;
    if (cam != g_cam) { g_cam = cam; reset_motion(); g_pitch = 0; g_zone = nullptr; }
    if (md != Mode::Free) g_pitch = 0;
    if (md == Mode::Off || !cam || !check_input_ran()) { reset_motion(); return; }
    const unsigned index = data_020a0e40;
    if (index >= 4 || index >= data_0209f21c) { reset_motion(); return; }
    unsigned char* rec = data_0209f498 + index * kRecord;
    const unsigned ctl = rec[0x16];
    const Sample s = sample();

    // Gates of the original paths.
    const bool ui_free = (unsigned char)(data_0209f294 | data_0209f2c4 | data_0209f20c) == 0 && data_0209d660 == 0;
    const bool keys_ok = data_0209fc48 == 0;   // Stage::CheckInput key remap
    unsigned cam_flags;
    std::memcpy(&cam_flags, static_cast<unsigned char*>(cam) + 0x154, 4);
    // Arrows: touch buttons (Standard, Dual-hand) need camera flag 0x1000 and
    // no pause/message (Stage::CheckCameraInput); Touch mode turns with X/Y.
    const bool arrows_ok = ctl == 1 ? keys_ok : (ctl <= 2 && ui_free && (cam_flags & 0x1000));

    unsigned held = rd16(rec + 4), pressed = rd16(rec + 6);
    unsigned cur = 0;
    if (md == Mode::DsArrows) {
        g_hdir = digital(g_hdir, s.x, s.y);
        g_vdir = digital(g_vdir, s.y, s.x);
        if (g_hdir && arrows_ok) cur |= g_hdir > 0 ? kRight : kLeft;
        // Down = L ("behind Mario"; Dual-hand: Y), a key in every mode.
        if (g_vdir < 0 && keys_ok) cur |= kBehind;
        // Up = zoom: X in Standard mode, the zoom touch button otherwise.
        if (g_vdir > 0 && (ctl == 0 ? keys_ok : ui_free)) {
            cur |= kZoom;
            if (ctl != 0 && !(g_prev & kZoom)) data_ov002_02111180 = 0x10;
        }
    } else {   // Free
        const float target = arrows_ok ? free_target_speed(s.x) : 0.f;
        g_speed = arrows_ok ? free_smooth(g_speed, target) : 0.f;
        if (g_speed != 0.f) {
            cur = g_speed > 0.f ? kRight : kLeft;
            int step = int(std::lround(std::fabs(g_speed)));
            g_step = step < 1 ? 1 : step;
            g_free_active = true;
        }
        if (ctl != 1 && ui_free && (cam_flags & 0x1000)) {
            const float ty = curve(s.y);
            if (ty > 0.f) {
                g_pitch += int(std::lround((s.y > 0 ? ty : -ty) * kPitchRate));
                if (g_pitch > kMaxPitch) g_pitch = kMaxPitch;
                if (g_pitch < -kMaxPitch) g_pitch = -kMaxPitch;
            }
        }
    }
    if (cur) {
        held |= cur;
        pressed |= cur & ~g_prev;
        // Stage::CheckInput: both arrows held = behind Mario (not Dual-hand).
        if (keys_ok && ctl != 2 && (held & 0x300) == 0x300) {
            held |= kBehind;
            if (pressed & 0x300) pressed |= kBehind;
        }
        wr16(rec + 4, held);
        wr16(rec + 6, pressed);
        ++g_applied;
    }
    if (pressed & kBehind) g_pitch = 0;   // L / C-Stick down: back to the DS view
    if (g_trace && (cur != g_prev || s.x || s.y)) {
        static int px = 1 << 20, py, pp;
        if (s.x != px || s.y != py || g_pitch != pp || cur != g_prev)
            std::printf("C-Stick camera: raw=%d,%d mode=%d ctl=%u bits=%04x speed=%.1f pitch=%d\n", s.x, s.y,
                        int(md), ctl, cur, double(g_speed), g_pitch);
        px = s.x; py = s.y; pp = g_pitch;
    }
    g_prev = cur;
}

}  // namespace cstick_camera

extern "C" {
int sm64ds_cstick_cam_step(int ds_step) {
    return cstick_camera::g_free_active ? cstick_camera::g_step : ds_step;
}

int sm64ds_cstick_cam_pitch(int distance, const void* cam) {
    using namespace cstick_camera;
    const void* zone = nullptr;
    if (cam) std::memcpy(&zone, static_cast<const unsigned char*>(cam) + 0x13c, sizeof zone);
    if (zone != g_zone) {   // new camera zone: the level's own view again
        g_zone = zone;
        g_pitch = 0;
    }
    if (g_pitch == 0) return 0;
    return int(((long long)distance * g_pitch + 0x800) >> 12);
}

int sm64ds_cstick_cam_offset(short* yaw_offset) {
    using namespace cstick_camera;
    if (!g_free_active) return 0;
    *yaw_offset = short(*yaw_offset + (g_speed > 0.f ? g_step : -g_step));
    return 1;
}

void sm64ds_cstick_inject(int active, int x, int y) {
    cstick_camera::g_inject = active != 0;
    cstick_camera::g_ix = x;
    cstick_camera::g_iy = y;
}

unsigned sm64ds_cstick_applied() { return cstick_camera::g_applied; }
}
