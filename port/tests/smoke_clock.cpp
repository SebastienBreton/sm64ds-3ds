#include <cstdint>
#include <cstdio>
#ifdef __3DS__
#include <3ds.h>
#else
#include <thread>
#include <chrono>
#endif
extern "C" uint64_t func_02059650();
int main() {
    uint64_t previous=func_02059650();
    for (int i=0;i<10000;++i) {
        uint64_t current=func_02059650();
        if (current<previous) return 1;
        previous=current;
    }
    const uint64_t start=func_02059650();
#ifdef __3DS__
    svcSleepThread(20000000);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
#endif
    const uint64_t elapsed=func_02059650()-start;
    //20ms is about10473 DS ticks. Allow scheduler delay, but catch missing
    // clock advancement and incorrect microsecond/millisecond clock units.
    if (elapsed<9000 || elapsed>104732) {
        std::printf("FAIL DS tick interval:%llu\n",(unsigned long long)elapsed);
        return 1;
    }
    std::printf("smoke_clock:10000 monotonic samples,20ms wait=%llu DS ticks\n",(unsigned long long)elapsed);
    return 0;
}
