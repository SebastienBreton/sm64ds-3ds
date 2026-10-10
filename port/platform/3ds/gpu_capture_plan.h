#pragma once
// minigames-2d-gpu: DS dual-screen 3D (display capture) on the resident GPU
// path, kept free of citro3d types so game-side code can fill it. The
// presenting thread fills g_capture_plan right before present_candidates;
// the presentation consumes and clears it.
//  capture: engine A's BG+OBJ+3D (before master brightness) is quantized to
//           RGB555 and kept in a GPU surface for physical bank capture_bank.
//  view:    engine B shows bank view_bank 1:1 (ntr::ppu_capture_view); B's
//           PPU candidates are ignored and its panel samples that surface.
//  delay_3d: engine A composes the previous frame's geometry (on the DS the
//           3D layer appears one frame after its geometry, while these games
//           write POWCNT/capture registers for the frame being displayed);
//           presented mono.
#include <cstdint>
namespace ds_gpu {
struct CaptureFramePlan {
    bool capture=false,view=false,delay_3d=false;
    unsigned capture_bank=0,view_bank=0;
    uint16_t view_master=0;
};
extern CaptureFramePlan g_capture_plan;
// Read a captured bank surface back as DS RGB555|0x8000 words (256x192,
// row-major). Renderer must be idle on this thread's side (waits for the
// GPU queue). False when the surface does not exist or memory is short.
bool capture_bank_readback_rgb555(unsigned bank,uint16_t* out);
}
