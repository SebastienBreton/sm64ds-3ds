// Native boundary for the game's PM_GoSleepMode(0xc,0,0) call.
// APT owns hardware suspension, graphics rights and waking on the 3DS.
#include <3ds.h>
#include "ntr/mmio.h"
#include <cstdio>
#include <cstdlib>
namespace {
[[noreturn]] void fail(const char* operation,int result) {
    std::fprintf(stderr,"FATAL: native console sleep %s: %08x\n",operation,result);
    std::fflush(stderr);std::abort();
}
void check(const char* operation,Result result) {if(R_FAILED(result))fail(operation,result);}
}
extern "C" int func_0205f77c(int mode,int arg1,int arg2) {
    // The game's only call requests lid/card wakeup. Cartridge removal is
    // inapplicable to native SD assets. Other SDK wake modes need an adapter.
    if(mode!=0xc || arg1 || arg2)fail("unsupported wake mode",mode);
    const unsigned ime=unsigned(ntr::io_read(0x04000208,2));
    check("ptmuInit",ptmuInit());
    u8 open=0;check("shell query",PTMU_GetShellState(&open));
    const bool allowed=aptIsSleepAllowed();
    if(!open) {
        if(!allowed)aptSetSleepAllowed(true);
        check("sleep request",APT_SleepIfShellClosed());
        do {
            // APT may receive the request asynchronously. Keep waiting while
            // closed, including before its sleep notification is delivered.
            aptHandleSleep();
            if(aptShouldClose()) {ptmuExit();std::exit(0);}
            check("wake shell query",PTMU_GetShellState(&open));
            if(!open)svcSleepThread(1000000);
        } while(!open);
        if(!allowed)aptSetSleepAllowed(false);
    }
    ptmuExit();
    // Reconcile the DS shared input flag without changing other input bits.
    auto* lid=ntr::xlate<volatile unsigned short>(reinterpret_cast<void*>(0x027fffa8));
    *lid=static_cast<unsigned short>(*lid & ~0x8000u);
    // The original caller ignores the SDK return (its saved IME on normal wake).
    // Emulated interrupt masks remain intact throughout native APT handling.
    return ime;
}
