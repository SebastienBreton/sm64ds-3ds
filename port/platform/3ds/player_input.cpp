#include "player_input.h"
#include "ds_input.h"
#ifdef __3DS__
#include <3ds.h>
#endif
extern "C" {
extern uint16_t data_020a0e58[];
extern uint16_t data_0209f49c, data_0209f49e;
void _ZN5Stage10CheckInputEv(void*);
}
namespace player_input {
static uint16_t previous;
void reset() { previous=0; }
void submit(uint32_t held) {
    const uint16_t ds=ds_input::buttons(held);
    data_020a0e58[0]=ds;
    data_020a0e58[1]=ds&~previous;
    previous=ds;
    // Stage::CheckInput accumulates each mapping; edges must not persist.
    data_0209f49c=data_0209f49e=0;
    _ZN5Stage10CheckInputEv(nullptr); // recovered body only accesses globals
}
void wait_frame() {
#ifdef __3DS__
    gspWaitForVBlank();
#endif
}
void poll() {
#ifdef __3DS__
    hidScanInput();
    submit(hidKeysHeld());
#endif
}
}
