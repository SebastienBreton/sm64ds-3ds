#include "ntr/rt.h"
#include "ntr/mmio.h"
#include <cstdint>
#include <cstdio>
extern "C" void OS_SleepThread(uint16_t*);
extern "C" void OS_WakeupThread(uint16_t*);
struct MsgQueue { uint16_t send,receive;uint32_t* array;int capacity,first,used; };
static_assert(sizeof(MsgQueue)==20,"original message queue ABI");
extern "C" int func_020587e4(MsgQueue*,uint32_t*,int);
namespace {
uint32_t messages[2];
MsgQueue queue{0,0,messages,2,0,0};
uint16_t first=0,second=0;
int phase=0,failure=0;
void game() {
    OS_WakeupThread(&first); // No stored notification for a future sleeper.
    ntr::rt_irq_disable();
    OS_SleepThread(&first);
    if(ntr::rt_frame()!=3 || !ntr::rt_irq_masked() || first) failure=1;
    phase=1;
    ntr::rt_irq_enable();
    OS_SleepThread(&second);
    if(ntr::rt_frame()!=6 || ntr::rt_irq_masked() || second) failure=1;
    phase=2;
    uint32_t message=0xdeadbeef;
    if(func_020587e4(&queue,&message,0)!=0 || message!=0xdeadbeef) failure=1;
    if(func_020587e4(&queue,&message,1)!=1 || message!=0x12345678 ||
       queue.first!=1 || queue.used!=0 || ntr::rt_frame()!=9) failure=1;
    phase=3;
}
bool frame(uint64_t number) {
    if(ntr::rt_irq_masked()) failure=1;
    if(number==1) OS_WakeupThread(&second); // Wrong queue must not wake first.
    if(number==2 && phase!=0) failure=1;
    if(number==3) OS_WakeupThread(&first);
    if(number==4) OS_WakeupThread(&first);
    if(number==5 && phase!=1) failure=1;
    if(number==6) OS_WakeupThread(&second);
    if(number==7) OS_WakeupThread(&first);
    if(number==8 && phase!=2) failure=1;
    if(number==9) {
        messages[0]=0x12345678;queue.used=1;
        OS_WakeupThread(&queue.receive);
    }
    return true;
}
}
int main() {
    if(!ntr::io_init()) return 1;
    const auto frames=ntr::rt_run(game,frame,12);
    std::printf("cooperative wait: frames=%llu phase=%d failures=%d\n",
                (unsigned long long)frames,phase,failure);
    return failure || phase!=3 || frames!=9;
}
