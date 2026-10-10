#pragma once
// visual-enhancements: port-side settings screen ("OPTIONS", English UI).
//
// Opened with SELECT+START held together for ~0.25 s (works in menus and in
// game; the DS game never sees SELECT+START together because the overlay
// swallows input while open). Drawn on the BOTTOM screen over the game.
//   Haut/Bas: choisir   Gauche/Droite ou A: changer   B: fermer (enregistre)
//   X: valeurs recommandées   Y: valeurs d'origine (port actuel)
//
// This module is the model + a CPU renderer into a 320x240 RGBA image
// (0xAARRGGBB, top row first) using an 8x8 CP437 bitmap font (libctru's
// default_font_bin on 3DS). Uploading/drawing that image (one 512x256 RGBA8
// texture quad over the bottom panel) is the Phase 2 hook; the image only
// changes when the overlay state changes, so the upload is rare.
// Pure C++: host-renderable for screenshots/tests.
#include "enhance_settings.h"
#include <cstdint>

namespace enhance {

// libctru KEY_* bit values (hid.h), duplicated to stay libctru-free.
enum Key : uint32_t {
    kKeyA = 1u << 0, kKeyB = 1u << 1, kKeySelect = 1u << 2, kKeyStart = 1u << 3,
    kKeyRight = 1u << 4, kKeyLeft = 1u << 5, kKeyUp = 1u << 6, kKeyDown = 1u << 7,
    kKeyR = 1u << 8, kKeyL = 1u << 9, kKeyX = 1u << 10, kKeyY = 1u << 11,
};

class SettingsOverlay {
public:
    static constexpr int kW = 320, kH = 240;
    static constexpr int kChordFrames = 15;

    // Per presented frame, game thread. held = hidKeysHeld(), down = hidKeysDown().
    // Returns true when the game must NOT see this frame's input.
    bool update(uint32_t held, uint32_t down);
    bool open() const { return open_; }
    // True once after the overlay closed with changes: caller saves the file
    // (save_file(kSettingsPath, snapshot())) outside time-critical code.
    bool take_save_request() { const bool r = save_requested_; save_requested_ = false; return r; }
    // Image generation: bumps when the rendered image changes.
    uint32_t generation() const { return generation_; }
    // Render the current state. font: 256 glyphs x 8 bytes, MSB = left pixel.
    void render(uint32_t* rgba, const uint8_t* font8x8) const;
    // Measured costs shown in the info line (filled by the renderer/bench).
    void set_cost_note(int item, const char* note);

    int selected() const { return selected_; }
    static int item_count();
private:
    void change(int item, int dir);
    bool open_ = false, save_requested_ = false, dirty_ = false;
    int chord_ = 0, selected_ = 0;
    uint32_t generation_ = 1;
    bool chord_latched_ = false;
    char notes_[16][40] = {};
};

// Text helpers (also used by an optional FPS counter): UTF-8 subset -> CP437
// (é è ê à ç ô û É supported). Returns pixels advanced.
int draw_text(uint32_t* rgba, int w, int h, int x, int y, const char* utf8, uint32_t color,
              const uint8_t* font8x8, int scale = 1);

}  // namespace enhance
