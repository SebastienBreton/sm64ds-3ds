// Nitro OS ticks:33,513,982 Hz timer source with divide-by64 (control0xc1).
// CTR system ticks run at268,111,856 Hz, exactly512 times the DS tick rate.
#include <cstdint>
#ifdef __3DS__
#include <3ds.h>
#else
#include <chrono>
#endif
extern "C" uint64_t func_02059650() {
#ifdef __3DS__
    static_assert(SYSCLOCK_ARM11 == 33513982u * 8, "clock ratio changed");
    return svcGetSystemTick() / 512;
#else
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    // Quotient/remainder avoids overflowing when converting long uptimes.
    return uint64_t(ns / 64000000000LL) * 33513982ULL +
           uint64_t(ns % 64000000000LL) * 33513982ULL / 64000000000ULL;
#endif
}
