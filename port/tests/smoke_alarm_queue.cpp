// Deterministic queue unit test: clock/IRQ/rearm are test-only fixtures.
#include "hal/alarm_layout.h"
#include <cstdio>
static uint64_t now;
static NativeAlarm *armed;
static unsigned arm_calls;
extern "C" uint64_t func_02059650() { return now; }
extern "C" void func_02059c18(NativeAlarm *p) { armed=p; ++arm_calls; }
extern "C" int _ZN3IRQ7DisableEv() { return 0; }
extern "C" void _ZN3IRQ7RestoreEj(unsigned) {}
static void callback(int) {}
#define CHECK(x) do { if(!(x)){std::printf("FAIL line%d\n",__LINE__);return 1;} }while(0)
int main() {
    NativeAlarm a={},b={},c={},periodic={};
    a.callback=b.callback=c.callback=periodic.callback=callback;
    func_02059a60(&a,300);func_02059a60(&b,100);func_02059a60(&c,300);
    CHECK(data_020a6444[0]==&b && data_020a6444[1]==&c);
    CHECK(b.next==&a && a.prev==&b && a.next==&c && c.prev==&a);
    CHECK(a.key==300 && b.key==100 && armed==&b && arm_calls==2);
    func_02059950(&a);CHECK(b.next==&c && c.prev==&b && !a.callback && !a.step);
    func_02059950(&b);CHECK(data_020a6444[0]==&c && c.prev==nullptr && armed==&c);
    func_02059950(&c);CHECK(!data_020a6444[0] && !data_020a6444[1]);
    now=0x100000005ULL;periodic.base=0x100000000ULL;periodic.step=3;
    func_02059a60(&periodic,0);CHECK(periodic.key==0x100000006ULL);
    CHECK(periodic.base==0x100000000ULL && periodic.step==3);
    func_02059950(&periodic);CHECK(!periodic.callback && !periodic.step);
    // All fields above4GB force both words of the packed64-bit values to work.
    std::puts("smoke_alarm_queue:DS layout,stable deadline order,cancel,rearm and64-bit periodic schedule passed");
    return 0;
}
