#pragma once
// analog-pad: optional ANALOG Circle Pad movement (port feature, 3DS only).
//
// DS mode (original): the Circle Pad is folded into the DS D-pad bits
// (ds_input.h); Stage::CheckInput turns any direction into a full-length
// digital stick (mag 0x1000), so the player walks, and runs only with Y.
//
// Analog mode: after the original Stage::CheckInput (linker --wrap), when
// the local player is in the Standard control mode, not touching the screen
// as a stick and not holding the real D-pad, the Circle Pad is written into
// the SAME command-record fields the DS touch-screen stick produces
// (touching=1, mag, nx, ny, ang). The original player code then applies the
// DS touch-stick physics unchanged: tiptoe/slow walk at small tilt, walk,
// then run proportional to tilt up to the Y-run speed at full tilt, for every
// character (Player_ScaleByCharFactor), camera-relative like the DS.
//
// The map HUD draws the "touch circle" whenever touching=1; the analog
// sample hides that flag from dMap_c only (fBase_c Before/AfterBehavior
// wrappers), so the bottom screen stays as with the D-pad.
//
// Setting: [input] circle_pad = analog|ds in sdmc:/sm64ds/settings.ini
// (enhance_settings.h, default analog), toggled live by the settings overlay;
// SM64DS_ANALOG_PAD=0/1 (env.txt, dev builds) overrides both.
#include <cstdint>

namespace analog_pad {

// Normalisation of the raw libctru Circle Pad position (hidCircleRead,
// nominal range about +-156; real pads are not a perfect circle).
constexpr int kDeadZone = 24;     // ~15% of 156: no movement below
constexpr int kFullRadius = 140;  // ~90% of 156: full tilt (saturates)
// DS touch-stick magnitude (Fix12, 0x1000 = 36 px) where the original speed
// curve starts moving (func_ov002_020d3b9c: mag <= 0x471 -> speed 0).
constexpr int kMagStart = 0x471;

struct Result { bool active; int16_t mag, nx, ny; };
// Pure mapping (host-testable): raw pad -> DS touch-stick magnitude and
// normalised direction (nx, ny in the DS touch frame: +y = down).
Result map(int x, int y);

bool enabled();            // current setting (env > overlay/settings.ini)
void set_enabled(bool on); // explicit override (tests); also publishes live

}  // namespace analog_pad

extern "C" {
// Test harness hook: replaces the HID sample (x, y = raw Circle Pad units,
// dpad = a real D-pad direction is held). active=0 returns to HID.
void sm64ds_analog_pad_inject(int active, int x, int y, int dpad);
// Diagnostics: number of updates where the analog sample drove the player.
unsigned sm64ds_analog_pad_applied();
}
