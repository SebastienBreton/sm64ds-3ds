// Native constructors reproduce the FaderColor portion of __sinit_02073e6c.
#include "FaderColor.h"
#include "FaderBrightness.h"
static_assert(sizeof(FaderColor) == 0x10, "native color fader layout");
static_assert(sizeof(FaderBrightness) == 0x0c, "native brightness fader layout");
extern "C" {
FaderColor data_0209b294;
// __sinit_02074edc: currInterp=0x1000, speed=0, brightness vtable.
FaderBrightness data_0209f5d0;
// Color fader constructed by the recovered __sinit_02074edc.
FaderColor data_0209f5e8;
// ARM9 symbol bounds: 0209d3ac..0209d3b0, backdrop palette scratch.
alignas(4) unsigned char data_0209d3ac[4];
}
