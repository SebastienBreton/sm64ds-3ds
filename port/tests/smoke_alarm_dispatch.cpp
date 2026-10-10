#include "hal/alarm_layout.h"
#include "ntr/mmio.h"
#include "ntr/rt.h"
#include <3ds.h>
#include <cstdio>
extern "C" void sm64ds_alarm_poll();
extern "C" void func_02059bc0();
extern "C" int func_0201aa50(NativeAlarm*,uint32_t,uint32_t,void(*)(int),int);
// Original Crash is only reachable for invalid scheduling; fail this test.
extern "C" void Crash() { std::puts("FAIL invalid alarm scheduling"); __builtin_trap(); }
static int count, value;
static void callback(int arg) { ++count; value=arg; sm64ds_alarm_poll(); }
#define CHECK(x) do { if(!(x)){std::printf("FAIL line %d\n",__LINE__);return 1;} }while(0)
static bool resumed;
static NativeAlarm frame_alarm;
static void game_loop() {
    frame_alarm.callback=callback; frame_alarm.argument=73;
    func_02059a60(&frame_alarm,func_02059650()+5000);
    ntr::rt_vblank_wait();
    resumed=true;
}
static bool host_frame(uint64_t) { svcSleepThread(20000000); return true; }
int main() {
    CHECK(ntr::io_init());
    NativeAlarm abi_alarm={};
    CHECK(func_0201aa50(&abi_alarm,123,1,callback,99)==0);
    func_02059bc0();
    uint64_t before=func_02059650();
    CHECK(func_0201aa50(&abi_alarm,123,1,callback,99)==1);
    CHECK(abi_alarm.key>=before+0x10000007bULL && abi_alarm.key<=func_02059650()+0x10000007bULL);
    CHECK(abi_alarm.callback==callback && abi_alarm.argument==99);
    func_02059bc0(); CHECK(data_020a6444[0]==&abi_alarm);
    func_02059950(&abi_alarm);
    NTR_MMIO(uint32_t,0x04000208)=1;
    NativeAlarm alarm={}; alarm.callback=callback; alarm.argument=42;
    func_02059a60(&alarm,func_02059650()+5000);
    sm64ds_alarm_poll(); CHECK(count==0);
    ntr::rt_irq_disable(); svcSleepThread(20000000);
    sm64ds_alarm_poll(); CHECK(count==0);
    ntr::rt_irq_enable();
    NTR_MMIO(uint32_t,0x04000208)=0;
    sm64ds_alarm_poll(); CHECK(count==0);
    NTR_MMIO(uint32_t,0x04000208)=1;
    NTR_MMIO(uint32_t,0x04000210)=0;
    sm64ds_alarm_poll(); CHECK(count==0);
    NTR_MMIO(uint32_t,0x04000210)=0x10;
    NTR_MMIO(uint16_t,0x04000106)=0;
    sm64ds_alarm_poll(); CHECK(count==0);
    NTR_MMIO(uint16_t,0x04000106)=0xc1;
    sm64ds_alarm_poll(); CHECK(count==1 && value==42 && !alarm.callback);
    sm64ds_alarm_poll(); CHECK(count==1 && !data_020a6444[0]);
    alarm.callback=callback; alarm.step=5000; alarm.base=func_02059650();
    func_02059a60(&alarm,0);
    svcSleepThread(20000000); sm64ds_alarm_poll();
    CHECK(count==2 && data_020a6444[0]==&alarm && alarm.key>func_02059650());
    func_02059950(&alarm); svcSleepThread(20000000); sm64ds_alarm_poll();
    CHECK(count==2 && !data_020a6444[0]);
    CHECK(ntr::rt_run(game_loop,host_frame,3)==1);
    CHECK(resumed && count==3 && value==73 && !frame_alarm.callback);
    std::puts("smoke_alarm_dispatch: VBlank callback integration, native clock, callback, IRQ masks, periodic rearm, cancellation and reentrancy passed");
    return 0;
}
