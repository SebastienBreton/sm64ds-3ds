#pragma once
// visual-enhancements: user-facing display options, persisted in
// sdmc:/sm64ds/settings.ini (read at boot by game_entry.cpp, edited by the
// in-game settings overlay, enhance_overlay.h). The user build ignores
// env.txt, so this file is the only way to configure a user build.
//
// Two kinds of options:
//  * boot options, applied ONCE before the renderer starts by exporting the
//    existing environment switches (SM64DS_SCREEN_FILTER, SM64DS_TOP_LAYOUT,
//    SM64DS_TOP_SKY); changing them in the overlay says "au prochain
//    démarrage". An explicit env.txt value (dev builds) always wins.
//  * live options, read every frame by the renderer through enhance::live()
//    (atomics: written by the overlay thread, read by the present worker).
//
// Pure C++ + stdio: also compiled into host tests.
#include <atomic>
#include <cstdint>

namespace enhance {

enum class TopLayout : uint8_t { Auto = 0, Ds = 1, Stretch = 2 };          // boot
enum class ScreenFilter : uint8_t { Nearest = 0, Sharp = 1 };              // boot
enum class Res3d : uint8_t { Ds = 0, Native = 1, Ss2x = 2 };               // live (Phase 2)
enum class TexFilter : uint8_t { Nearest = 0, Auto = 1, Linear = 2 };      // live (Phase 2)

struct Settings {
    // 2D
    bool logo_hd = false;                 // HD title logo (enhance_logo.h)
    uint8_t logo_method = 7;              // enhance::Method (7 = Xbr)
    bool ui_hd = false;                   // HD menu 2D layers (future)
    TopLayout top_layout = TopLayout::Auto;
    ScreenFilter screen_filter = ScreenFilter::Sharp;
    bool full_width_sky = true;
    ScreenFilter gameplay_filter = ScreenFilter::Nearest;   // in-game top presentation
    // 3D
    Res3d res3d = Res3d::Ds;
    TexFilter tex_filter = TexFilter::Nearest;
    // misc
    bool show_fps = false;
    bool gpu_timing = false;
    // input (analog-pad, analog_pad.h): Circle Pad drives the DS touch-stick
    // analog path (speed follows tilt, no Y needed) instead of the DS
    // digital D-pad. Default ON at the user's request; kept by safe() and
    // recommended() (not a display option).
    bool circle_pad_analog = true;
    // input (cstick-camera, cstick_camera.h): New 3DS C-Stick camera.
    // 0 = off, 1 = DS arrows (same commands as the touch-screen camera
    // arrows / L / X), 2 = free analog camera. Kept by safe()/recommended().
    uint8_t camera_cstick = 1;

    // Current behaviour of the port (every new feature off). Used until the
    // features are validated on real hardware.
    static Settings safe();
    // Target defaults once validated (best quality within the frame budget).
    static Settings recommended();
    bool operator==(const Settings& o) const;
};

// Parse an INI text. Unknown keys / bad values are counted and skipped.
struct ParseReport { int lines = 0, applied = 0, unknown = 0, invalid = 0; };
ParseReport parse_ini(const char* text, Settings& inout);
// Serialize (stable order, French comments). Returns bytes written (<cap).
int write_ini(const Settings& s, char* out, int cap);

// File helpers (stdio). load: missing file -> false, settings unchanged.
bool load_file(const char* path, Settings& inout, ParseReport* report = nullptr);
// Atomic-ish save: path.tmp then rename.
bool save_file(const char* path, const Settings& s);

constexpr const char* kSettingsPath = "sdmc:/sm64ds/settings.ini";

// Boot: load kSettingsPath (if present) over Settings::safe(), publish to
// live(), and export boot options as env switches unless already set.
// Call from main() before any renderer option is read. Returns the loaded set.
Settings boot_apply(const char* path = kSettingsPath);

// Live view shared with the renderer (lock-free reads).
struct Live {
    std::atomic<bool> logo_hd{false};
    std::atomic<uint8_t> logo_method{7};
    std::atomic<bool> ui_hd{false};
    std::atomic<uint8_t> gameplay_filter{0};
    std::atomic<uint8_t> res3d{0};
    std::atomic<uint8_t> tex_filter{0};
    std::atomic<bool> show_fps{false};
    std::atomic<bool> gpu_timing{false};
    std::atomic<bool> circle_pad_analog{true};
    std::atomic<uint8_t> camera_cstick{1};
    // Settings screen open (enhance_runtime): the game sees no input.
    std::atomic<bool> overlay_open{false};
    std::atomic<uint32_t> generation{0};   // bumped on every publish
};
Live& live();
// publish()/snapshot(): game thread only (boot + overlay). Renderer threads
// read live() only.
void publish(const Settings& s);
Settings snapshot();   // last published full Settings (incl. boot options)

// Texture filter policy for a DS TEXIMAGE_PARAM (Phase 2 hook for the 3D
// texture cache): true = GPU_LINEAR. Auto = linear only for textures that
// cannot show cut-out fringes: no colour-0 transparency (bit 29) and not a
// translucent format (A3I5=1, A5I3=6); direct colour (7) always eligible.
bool texture_wants_linear(uint32_t teximage_param, TexFilter policy);

}  // namespace enhance
