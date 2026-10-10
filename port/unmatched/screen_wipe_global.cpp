// Native lifetime for the global dWipe_c from __sinit_02074f80. The original
// constructor manually installs DS vtables; native C++ supplies GCC vtables.
#include "dWipe_c.h"
static_assert(sizeof(dWipe_c) == 0x2c, "screen wipe ABI");
extern "C" {
dWipe_c data_0209f61c;
void func_0202ed14(void*);
}
namespace {
struct InitializeScreenWipe {
    InitializeScreenWipe() {
        // The recovered constructor starts at zero rather than the usual
        // FaderBrightness 0x1000. Then run its original state initialization.
        data_0209f61c.currInterp = 0;
        data_0209f61c.speed = 0;
        data_0209f61c.color = 0;
        func_0202ed14(&data_0209f61c);
    }
} initializeScreenWipe;
}
