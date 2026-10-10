#include "ntr/rt.h"
#include "ntr/mmio.h"
#include <cstdint>
#include <cstdio>
extern "C" {
int data_0209d514=0,data_0208ee44=2;
unsigned char data_0209d4f0=1,data_0209d4dc=0;
int data_0209d500=0,data_0209d4fc=0;
int displays=0,callbacks=0;
void func_02019144() { ++displays; }
void func_02019100() { ++callbacks; }
void func_0201a4e4();
void _ZN3IRQ13SetIRQHandlerEjPFvvE(unsigned,void(*)());
void OS_SleepThread(uint16_t*);
}
namespace {
int test_irqs=0,failed=0,done=0;
void irq() { ++test_irqs;if(!ntr::rt_irq_masked()) failed=1; }
void game() {
    for(int i=1;i<=4;++i) {
        OS_SleepThread(reinterpret_cast<uint16_t*>(&data_0209d500));
        if(ntr::rt_frame()!=unsigned(i*2) || displays!=i) failed=1;
    }
    OS_SleepThread(reinterpret_cast<uint16_t*>(&data_0209d4fc));
    done=1;
}
bool frame(uint64_t) { return true; }
}
int main() {
    if(!ntr::io_init()) return 1;
    _ZN3IRQ13SetIRQHandlerEjPFvvE(2,irq);
    ntr::io_write(0x04000208,1,4);ntr::io_write(0x04000210,2,4);
    ntr::rt_irq_disable();ntr::rt_raise_irq(2);
    if(test_irqs) return 2;
    ntr::rt_irq_enable();if(test_irqs!=1) return 3;
    ntr::io_write(0x04000210,0,4);ntr::rt_raise_irq(2);
    if(test_irqs!=1) return 4;
    ntr::io_write(0x04000210,2,4);if(test_irqs!=2) return 5;
    ntr::io_write(0x04000208,0,4);ntr::rt_raise_irq(2);
    if(test_irqs!=2) return 6;
    ntr::io_write(0x04000208,1,4);if(test_irqs!=3) return 7;
    ntr::rt_irq_disable();ntr::rt_raise_irq(2);
    ntr::io_write(0x04000214,2,4);ntr::rt_irq_enable();
    if(test_irqs!=3) return 8;
    func_0201a4e4();
    auto frames=ntr::rt_run(game,frame,12);
    const auto flags=*static_cast<uint32_t*>(ntr::host_ptr(0x023c3ff8));
    std::printf("original VBlank: frames=%llu displays=%d callbacks=%d done=%d failures=%d shared=%x\n",
                (unsigned long long)frames,displays,callbacks,done,failed,flags);
    return failed || !done || frames!=9 || displays!=4 || callbacks!=9 || !(flags&1);
}
