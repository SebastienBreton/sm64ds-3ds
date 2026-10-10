// Native boundary for DS process lifecycle. These operations must never fall
// through to cartridge reload, ARM7 IPC or DS libc thread structures on 3DS.
// Restart/power management remain unimplemented: fail explicitly if reached.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
namespace {
[[noreturn]] void unavailable(const char *operation) {
    std::fprintf(stderr,"FATAL: native lifecycle operation unavailable: %s\n",operation);
    std::fflush(nullptr);
    std::abort();
}
[[noreturn]] void native_abort() {
    std::fprintf(stderr,"FATAL: game runtime abort\n");
    std::fflush(nullptr);
    std::abort();
}
}
extern "C" {
// Original initialized callback points through func_02073220 to DS libc abort.
void (*data_0209a6fc)()=native_abort;
void func_01ffdd98(uint32_t) { unavailable("DS cartridge soft reset"); }
int func_0205f958() { unavailable("DS power-management command 0x0e"); }
}
