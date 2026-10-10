#pragma once
#include "ntr/display_capture.h"

namespace ntr {
enum class UnmappedCaptureCompletion {Inactive,OutsideDrawingRange,Completed,NeedsMappedDriver};
// Isolated fixed-register frame prototype. Does not expose a capture driver
// without physical banks. Caller must provide the mapping at scanline0 and
// must not use this shortcut for frames with scanline mapping changes.
inline UnmappedCaptureCompletion complete_unmapped_capture_frame(
    DisplayCapture& timing,uint32_t display,const uint8_t controls[4],bool outside=false){
    const uint32_t before=timing.control();
    if(!(before&0x80000000u) && !timing.latched())return UnmappedCaptureCompletion::Inactive;
    const unsigned bank=(before>>16)&3;
    if(display_capture_lcdc_mask(controls)&(1u<<bank))
        return UnmappedCaptureCompletion::NeedsMappedDriver;
    DisplayCaptureMemory memory{};
    // A disabled/non-LCDC destination prevents every source read and write,
    // but DS still latches enable at line0 and clears that latch at VBlank.
    for(unsigned line=0;line<192;++line)
        timing.scanline(line,outside,display,memory,nullptr,nullptr,nullptr);
    timing.vblank();
    return outside?UnmappedCaptureCompletion::OutsideDrawingRange:UnmappedCaptureCompletion::Completed;
}
}
