#pragma once
// Per-scanline WIN0H/WIN1H shadow written by rt.cpp run_scanlines() while
// H-blank IRQ handlers run (scene wipes, Wanted iris) and read by the PPU
// window masks. Lives in otherwise unused ARM9 I/O space of each engine:
// +0 u32 valid (1 = this frame ran H-blank handlers), +4 192 x {WIN0H,WIN1H}.
// Kept in the I/O arena so the prepared-scene caches snapshot it as a span.
#include <cstdint>
namespace ntr {
constexpr uint32_t SCANLINE_WINDOW_SHADOW = 0xC00u; // offset from 0x04000000 / 0x04001000
constexpr uint32_t SCANLINE_WINDOW_LINES = 192u;
constexpr uint32_t SCANLINE_WINDOW_BYTES = 4u + SCANLINE_WINDOW_LINES * 4u;
}
