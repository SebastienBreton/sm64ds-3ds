// Native display blanking boundary for the DS LCD-power operation.
#include <3ds.h>
#include "ntr/mmio.h"
namespace {
u64 last_off=0;
bool was_off=false;
}
extern "C" int func_0205f68c(int mode,int led_mode,int skip_delay,int wait_led) {
    if (mode!=0 && mode!=1) return 1; // Original SDK convention.
    const u64 now=svcGetSystemTick();
    // Original SDK requires8 VBlanks after power-off. On3DS retain a
    // conservative134ms hardware settling interval using a monotonic clock.
    if (mode==1 && !skip_delay && was_off &&
        now-last_off < (static_cast<u64>(SYSCLOCK_ARM11)*134)/1000) return 0;
    const Result result=GSPGPU_SetLcdForceBlack(mode==0);
    if (R_FAILED(result)) return 0;
    unsigned power=static_cast<unsigned>(ntr::io_read(0x04000304,2));
    power=mode ? (power|1u) : (power&~1u);
    ntr::io_write(0x04000304,power,2);
    if (mode==0) { last_off=now;was_off=true; }
    // The3DS OS owns its system power LED. DS blink requests have no native
    // equivalent here; original LCD operation ignores the LED command result.
    (void)led_mode;(void)wait_led;
    return 1;
}
