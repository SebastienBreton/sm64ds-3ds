// Wait queues for the current cooperative native game task. Host frame hooks
// and alarm callbacks may wake it; a frame alone is not a wake notification.
// Native audio workers must marshal notifications into this serialized runtime.
#include "ntr/rt.h"
#include "ntr/mmio.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
namespace {
struct Wait { uint16_t* queue; bool ready; };
Wait* waiting=nullptr;
[[noreturn]] void invalid_wait() {
    std::fputs("FATAL: thread wait requires a running cooperative task, non-null queue and no nested wait\n",stderr);
    std::fflush(stderr);std::abort();
}
}
extern "C" void OS_SleepThread(uint16_t* queue) {
    if (!ntr::rt_running() || !queue || waiting) invalid_wait();
    queue=ntr::xlate<uint16_t>(queue);
    // There is one emulated game task, assigned wait-mask bit0. Refuse
    // unidentified task bits rather than dropping waits from an unknown task.
    if (*queue) invalid_wait();
    Wait waiter{queue,false};
    waiting=&waiter;
    *queue=1;
    // Sleeping releases the emulated CPU's IRQ mask; restore the caller's
    // mask only when it resumes. This permits real callback wakeups.
    const uint32_t saved=ntr::rt_irq_enable();
    do { ntr::rt_vblank_wait(); } while (!waiter.ready);
    ntr::rt_irq_restore(saved);
    waiting=nullptr;
}
extern "C" void OS_WakeupThread(uint16_t* queue) {
    if (!queue) return;
    queue=ntr::xlate<uint16_t>(queue);
    const uint32_t saved=ntr::rt_irq_disable();
    if (waiting && waiting->queue==queue && (*queue&1)) waiting->ready=true;
    *queue=0;
    ntr::rt_irq_restore(saved);
}
