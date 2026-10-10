// Cooperative native dispatch: callbacks run only on the game thread.
#include "hal/alarm_layout.h"
#include "ntr/mmio.h"
#include "ntr/rt.h"
extern "C" unsigned _ZN3IRQ11DisableIRQsEj(unsigned);
extern "C" {
struct TimerIRQEntry { uint32_t handler, active, argument; };
TimerIRQEntry data_020a60f4[4] = {};
uint16_t data_020a6440=0;
// Timer allocation bits used by the original OS initialization.
uint16_t data_020a642c=0;
void func_02059bc0() {
    if (data_020a6440) return;
    data_020a6440=1;
    data_020a642c|=2;
    data_020a6444[0]=data_020a6444[1]=nullptr;
    _ZN3IRQ11DisableIRQsEj(0x10);
}
void func_020599e8(int*,long long,int,int);
int func_0201aa50(NativeAlarm *alarm, uint32_t low, uint32_t high,
                  void (*callback)(int), int argument) {
    if (!data_020a6440) return 0;
    // Original five-word wrapper uses adjacent low/high registers; GCC's
    // uint64_t callee ABI requires an aligned register pair instead.
    func_020599e8(reinterpret_cast<int*>(alarm),
        (long long)((uint64_t(high)<<32)|low),
        int(reinterpret_cast<uintptr_t>(callback)), argument);
    return 1;
}
void func_02059824() {
    NTR_MMIO(uint16_t,0x04000106)=0;
    _ZN3IRQ11DisableIRQsEj(0x10);
    NTR_MMIO(uint32_t,0x023c3ff8)|=0x10;
    auto *alarm=data_020a6444[0];
    if (!alarm) return;
    if (func_02059650()<alarm->key) { func_02059c18(alarm); return; }
    data_020a6444[0]=alarm->next;
    if (alarm->next) alarm->next->prev=nullptr;
    else data_020a6444[1]=nullptr;
    auto callback=alarm->callback;
    if (!alarm->step) alarm->callback=nullptr;
    if (callback) callback(alarm->argument);
    if (alarm->step) { alarm->callback=callback; func_02059a60(alarm,0); }
    if (data_020a6444[0]) func_02059c18(data_020a6444[0]);
}
void sm64ds_alarm_poll() {
    static bool dispatching=false;
    if (dispatching || ntr::rt_irq_masked() ||
        !(uint32_t(NTR_MMIO(uint32_t,0x04000208))&1) ||
        !(uint32_t(NTR_MMIO(uint32_t,0x04000210))&0x10) ||
        !(uint16_t(NTR_MMIO(uint16_t,0x04000106))&0x80)) return;
    auto *head=data_020a6444[0];
    if (!head || func_02059650()<head->key) return;
    dispatching=true;
    func_02059824();
    dispatching=false;
}
}
