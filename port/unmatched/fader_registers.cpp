#include "G2x.h"
#include "ntr/mmio.h"
void G2x::SetBlendBrightness(volatile unsigned short* p, unsigned short val, short amt) {
    p = ntr::xlate<unsigned short>(p);
    p[0] = val | (amt < 0 ? 0xc0 : 0x80);
    p[2] = amt < 0 ? -amt : amt;
}
extern "C" void func_02053a90(unsigned short* p, int value) {
    p = ntr::xlate<unsigned short>(p);
    *p = value == 0 ? 0 : (value > 0 ? value | 0x4000 : -value | 0x8000);
}
