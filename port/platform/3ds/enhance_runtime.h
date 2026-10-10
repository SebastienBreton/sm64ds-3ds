#pragma once
// visual-enhancements phase 2: runtime glue between the game thread (input,
// settings overlay model, settings.ini) and the presentation worker (overlay
// texture upload + draw in viewer_3ds.cpp).
//
// Game thread, once per game update, right after hidScanInput():
//   enhance_runtime::filter_input(held, down) -> keys the DS game may see.
// The SELECT+START chord opens/closes the settings screen; while it is open
// the game sees no buttons/touch/Circle Pad (live().overlay_open), START is
// never forwarded while SELECT is held, and keys held when it closes stay
// hidden until released (no accidental press on close).
//
// Worker (inside a GPU frame, after FrameBegin):
//   const Image* img = enhance_runtime::overlay_image(generation) -> the
//   latest rendered 320x240 overlay image (or null when closed); the caller
//   re-uploads only when `generation` changes, then acknowledge(generation).
#include <cstdint>

namespace enhance_runtime {

uint32_t filter_input(uint32_t held, uint32_t down, bool& touch_allowed);

// Presentation side. Returns null when nothing has to be drawn.
// kind 0 = full settings screen (320x240), 1 = FPS strip (256x16).
const uint32_t* overlay_image(unsigned kind, uint32_t& generation, int& width, int& height);
void acknowledge(unsigned kind, uint32_t generation);

// Presentation statistics fed by the worker for the FPS strip.
void note_present(double frame_ms, double gpu_ms);

}  // namespace enhance_runtime
