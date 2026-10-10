// Recovered timer programming with explicit DS layout and translated MMIO.
#include "hal/alarm_layout.h"
#include "ntr/mmio.h"
extern "C" void func_02056e4c(unsigned, unsigned, unsigned);
extern "C" void func_02059824();
extern "C" void _ZN3IRQ10EnableIRQsEj(unsigned);
void func_02059c18(NativeAlarm *alarm) {
    uint64_t now=func_02059650();
    NTR_MMIO(uint16_t,0x04000106)=0;
    int64_t delta=(int64_t)(alarm->key-now);
    func_02056e4c(1,(unsigned)(uintptr_t)&func_02059824,0);
    uint16_t timer=delta<0 ? 0xfffe : (delta<0x10000 ? (uint16_t)~(uint32_t)delta : 0);
    NTR_MMIO(uint16_t,0x04000104)=timer;
    NTR_MMIO(uint16_t,0x04000106)=0xc1;
    _ZN3IRQ10EnableIRQsEj(0x10);
}
