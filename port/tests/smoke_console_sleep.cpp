#include <3ds.h>
#include "ntr/mmio.h"
#include <cstdio>
extern "C" void func_02019ed0();
extern "C" unsigned char data_0209d4e4;
int main() {
    if(!ntr::io_init())return 2;
    if(R_FAILED(ptmuInit()))return 3;
    u8 open=0;Result result=PTMU_GetShellState(&open);ptmuExit();
    if(R_FAILED(result) || open!=1)return 4;
    auto* lid=ntr::xlate<volatile unsigned short>(reinterpret_cast<void*>(0x027fffa8));
    for(unsigned mirror=0;mirror<4;++mirror)
        if(ntr::host_ptr(0x02000000+mirror*0x400000+0x3fffa8)!=lid)return 6;
    const bool allowed=aptIsSleepAllowed();
    for(unsigned ime=0;ime<2;++ime) {
        ntr::io_write(0x04000208,ime,2);
        ntr::io_write(0x04000210,0x1234,4);
        *lid=0x9234;data_0209d4e4=0;
        func_02019ed0();
        std::printf("sleep return: flag=%u lid=%04x IME=%llx IE=%llx allowed=%d expected=%d\n",unsigned(data_0209d4e4),unsigned(*lid),(unsigned long long)ntr::io_read(0x04000208,2),(unsigned long long)ntr::io_read(0x04000210,4),aptIsSleepAllowed(),allowed);
        if(data_0209d4e4 || *lid!=0x1234 || ntr::io_read(0x04000208,2)!=ime || ntr::io_read(0x04000210,4)!=0x1234 || aptIsSleepAllowed()!=allowed)return 5;
    }
    std::puts("smoke_console_sleep: PASS open-shell stale DS lid flag reconciled; original wrapper and IRQ state preserved (physical sleep not exercised)");
    return 0;
}
