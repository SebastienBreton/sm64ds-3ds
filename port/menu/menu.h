#pragma once
// Call after destroying live scene objects, in the native overlay registry.
extern "C" bool sm64ds_load_menu_overlay();
extern "C" bool sm64ds_unload_menu_overlay();

// Caller scans libctru HID once per frame; publishes touch/buttons for Behavior.
extern "C" bool sm64ds_menu_poll_input();

#include <cstdint>
// Deterministic/local input boundary, also used by headless gameplay probes.
// Does not sample HID or run game logic. Preserves original camera feedback.
extern "C" bool sm64ds_local_input_publish(std::uint16_t keys,const std::uint16_t* touch4);
