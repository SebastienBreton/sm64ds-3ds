#pragma once
// cstick-camera: New 3DS C-Stick camera control (port feature, 3DS only).
//
// DS original: the camera is turned with the two "camera arrow" buttons drawn
// on the touch screen (dMeter_c::RenderCameraButtons). Stage::CheckCameraInput
// turns a touch on them into game-level command bits of the local command
// record data_0209f498[i] (+4 held, +6 pressed): 0x200 = left arrow,
// 0x100 = right arrow (both = 0x4000, "behind Mario", added by
// Stage::CheckInput). The follow camera (func_02009e70) then turns its yaw
// (+0x17c) by 0x400 per update while an arrow is held (0x200: -, 0x100: +);
// the 8-direction camera (func_0200bb28) steps 0x2000 per press and repeats
// every 20 updates. The physical keys feed the same command record through
// the per-control-mode key tables (Standard: L = 0x4000 behind Mario,
// X = 0x8000 "zoom" = cycles far camera / first person; Touch mode: X/Y =
// right/left arrows).
//
// Modes ([input] camera_cstick in settings.ini, overlay "Caméra (C-Stick)"):
//  * Off       : the C-Stick does nothing (DS behaviour).
//  * DsArrows  : after the original Stage::CheckInput (linker --wrap shared
//                with analog_pad.cpp) the C-Stick writes exactly what the DS
//                arrows / keys write: left/right = arrow bits 0x200/0x100
//                (held + pressed edge), down = 0x4000 (as L: camera behind
//                Mario), up = 0x8000 (as X: zoom button). Same gates as the
//                original touch path (camera flag 0x1000, no pause/message...),
//                so step size, repeat rate, sounds and the bottom-screen arrow
//                highlight are the DS ones.
//  * Free      : analog camera on the same command path: the arrow bit is
//                held while the C-Stick is deflected and the follow camera's
//                per-update step (0x400 on DS) becomes proportional to the
//                deflection with smoothing (sm64ds_cstick_cam_step, guarded
//                hook in func_02009e70); the 8-direction camera turns
//                continuously instead of 45-degree steps (func_0200bb28 hook).
//                C-Stick Y tilts the camera: an offset (fraction of the camera
//                distance) added to the game's own height target BEFORE its
//                clamps (mode pitch limits, ceiling, floor), kept after release
//                and reset by L ("behind Mario"), a camera-zone change or a new
//                level. Collision, follow and Mario's camera-relative controls
//                stay the game's.
//
// Old 3DS (no C-Stick: hidCstickRead stays 0) and all three modes without
// C-Stick input are identical to the DS.
#include <cstdint>

namespace cstick_camera {

enum class Mode : uint8_t { Off = 0, DsArrows = 1, Free = 2 };

// Raw libctru C-Stick units (irrstCstickRead; about +-146 on hardware).
constexpr int kDigitalOn = 48;    // DsArrows: direction pressed above this
constexpr int kDigitalOff = 32;   // ... released below this (hysteresis)
constexpr int kDeadZone = 16;     // Free: no motion below
constexpr int kFull = 104;        // Free: full speed / tilt rate at or above
constexpr int kDsStep = 0x400;    // DS follow-camera yaw step per update
constexpr int kMaxPitch = 0x800;  // Free: tilt offset limit, Fix12 of distance

Mode mode();                 // env SM64DS_CSTICK_CAMERA (0/1/2) > settings
void set_mode(Mode m);       // explicit override (tests)

// Pure helpers (host-testable).
// Free mode: target yaw speed (s16 angle units per update, signed: + = right
// arrow direction) for a raw C-Stick x.
float free_target_speed(int x);
// One smoothing step of the yaw speed towards the target.
float free_smooth(float speed, float target);

// Called once per game update right after the original Stage::CheckInput.
void after_check_input();

}  // namespace cstick_camera

extern "C" {
// Hooks called from the decomp (func_02009e70 / func_0200bb28, guarded by
// SM64DS_PLATFORM_PC && __3DS__). Identity when the free camera is inactive.
int sm64ds_cstick_cam_step(int ds_step);
int sm64ds_cstick_cam_pitch(int distance, const void* cam_mode);
int sm64ds_cstick_cam_offset(short* yaw_offset);
// Test harness: replaces the HID C-Stick sample (raw units); active=0 = HID.
void sm64ds_cstick_inject(int active, int x, int y);
// Diagnostics: updates where the C-Stick wrote command bits / analog steps.
unsigned sm64ds_cstick_applied();
}
