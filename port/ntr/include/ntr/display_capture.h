#pragma once
#include <stdint.h>

namespace ntr {

// Capture works on physical A..D banks, each 128 KiB, regardless of their
// current CPU aliases. Only banks currently mapped to LCDC participate.
struct DisplayCaptureMemory {
    uint8_t* banks[4];
    uint8_t lcdc_mask;
    void* context = nullptr;
    void (*written)(void*, unsigned bank, unsigned byte_offset, unsigned bytes) = nullptr;
};
uint8_t display_capture_lcdc_mask(const uint8_t controls[4]);

enum class DisplayCaptureStatus {
    Inactive, OutsideCapture, DestinationUnmapped, MissingComposite,
    MissingGeometry, MissingFifo, Captured
};
struct DisplayCaptureResult {
    DisplayCaptureStatus status;
    unsigned bank = 0, byte_offset = 0, pixels = 0;
};
struct DisplayCaptureSources {
    bool active = false, composite = false, geometry = false, fifo = false;
};
struct DisplayCaptureFrameResult {
    DisplayCaptureStatus status;
    unsigned captured_lines = 0;
};

// minigames-2d-gpu: a whole-frame source-A capture the resident GPU backend
// performs itself (render-to-texture, no CPU readback): enable, source A
// only, BG+OBJ+3D (not raw 3D), 256x192, LCDC destination bank, write offset
// 0 or 32 KiB (no 128 KiB wrap), engine A in normal display mode.
// Returns false when a capture is requested that this path cannot perform.
struct DisplayCaptureGpuRequest { bool active=false; unsigned bank=0,byte_offset=0; };
bool display_capture_gpu_request(uint32_t control,const uint8_t vram_controls[4],
                                 uint32_t display_control_a,DisplayCaptureGpuRequest& request);

// Frame/scanline driver owns this state. No IO or presentation dependencies.
// Source A rows use the PPU/GX 0xAARRGGBB format before master brightness;
// alpha zero becomes a clear bit15, any nonzero alpha becomes bit15=1.
// Source B FIFO, when selected, is a caller-supplied row of 256 RGB555 words.
class DisplayCapture {
public:
    void reset();
    uint32_t control() const { return control_; }
    bool latched() const { return latched_; }
    // offset 0/2, width 2, or offset0 width4. Byte writes are ignored on DS.
    bool write_control(unsigned offset, uint32_t value, unsigned width);
    // Pure query for backend readback decisions. line0 includes a pending
    // enable before scanline latches; unmapped destinations require no inputs.
    DisplayCaptureSources sources(unsigned line,bool outside_drawing_range,
                                  uint8_t lcdc_mask) const;
    // outside_drawing_range describes the GPU's drawing range, not DISPCNT
    // forced blank: forced blank still produces/captures a white source A.
    DisplayCaptureResult scanline(unsigned line, bool outside_drawing_range,
                                  uint32_t display_control, DisplayCaptureMemory& memory,
                                  const uint32_t* composite, const uint32_t* geometry,
                                  const uint16_t* fifo = nullptr);
    // Synchronous fixed-register frame. Inputs/output are row-major256x192;
    // output must not alias the supplied source arrays.
    // Composite is BGOBJ before master, even in VRAM/display-off modes.
    // VRAM scan-out reads each row BEFORE its capture write, preserving
    // same-bank feedback. Missing required inputs fail before any writes.
    // Does not advance VBlank; the timing owner calls vblank() afterwards.
    DisplayCaptureFrameResult frame(uint32_t display_control, uint16_t master,
                                     DisplayCaptureMemory& memory,
                                     const uint32_t* composite, const uint32_t* geometry,
                                     uint32_t* output, const uint16_t* fifo = nullptr);
    // Clears enable only when a capture latched on scanline0 this frame.
    void vblank();
private:
    uint32_t control_ = 0;
    bool latched_ = false;
};
}
