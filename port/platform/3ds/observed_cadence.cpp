#include "observed_cadence.h"
#include "observed_cycle_clock.h"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace observed_cadence {
namespace {
// Port scheduling state must not allocate from the original game's heap.
std::optional<observed_cycle::Clock> clock;
uint64_t last_frame=0,observed_cycles=0,cycle_frames=0,histogram[9]{};
uint64_t completed_cycle_frames=0,max_cycle_frames=0,bootstrap_frames=0;
uint64_t wait_ticks=0,late_ticks=0,debt_ticks=0,repaid_ticks=0,skipped_frames=0;
bool catch_up=true;
bool configured=false,enabled=false;
[[noreturn]] void fail(const char* reason){
    std::fprintf(stderr,"FATAL: observed cadence %s\n",reason);std::abort();
}
}
void configure(){
    const char* option=std::getenv("PLAYER_OBSERVED_CADENCE");
    enabled=!option || (option[0]=='1' && option[1]==0);
    {const char* skip=std::getenv("SM64DS_FRAME_SKIP");catch_up=!(skip && skip[0]=='0');}
    if(enabled)setenv("PLAYER_RESIDENT_NO_VBLANK_WAIT","1",1);
    else unsetenv("PLAYER_RESIDENT_NO_VBLANK_WAIT");
    clock.reset();last_frame=observed_cycles=cycle_frames=0;
    completed_cycle_frames=max_cycle_frames=bootstrap_frames=wait_ticks=late_ticks=0;
    debt_ticks=repaid_ticks=skipped_frames=0;
    for(auto& count:histogram)count=0;
    configured=true;
}
void cycle_begin(){
    if(!configured)return;
    if(observed_cycles){
        ++histogram[cycle_frames<8?cycle_frames:8];
        completed_cycle_frames+=cycle_frames;
        if(cycle_frames>max_cycle_frames)max_cycle_frames=cycle_frames;
    }else bootstrap_frames=last_frame;
    cycle_frames=0;++observed_cycles;
    // Retail timing:33513982 ARM7 cycles/s and560190 cycles/display frame.
    if(!clock)clock.emplace(SYSCLOCK_ARM11,33513982,560190,svcGetSystemTick(),last_frame);
    if(!clock->begin_cycle())fail("invalid cycle boundary");
}
void after_frame(uint64_t frame){
    if(!configured)return;
    if(frame!=last_frame+1)fail("omitted or repeated original frame hook");
    last_frame=frame;++cycle_frames;
    if(!clock)return;
    const auto decision=clock->event(frame,svcGetSystemTick());
    if(!decision.valid)fail("invalid frame sequence/deadline");
    late_ticks+=decision.lateness;
    // Debt: up to 4 display frames of presentation lateness (longer stalls,
    // e.g. scene loads, are not caught up). Early frames repay it by skipping
    // their wait, so the original game keeps its real-time speed.
    const uint64_t frame_ticks=SYSCLOCK_ARM11*560190ull/33513982ull;
    uint64_t wait=decision.wait;
    if(catch_up && enabled){
        if(decision.lateness && decision.lateness<15*frame_ticks){debt_ticks+=decision.lateness;if(debt_ticks>4*frame_ticks)debt_ticks=4*frame_ticks;}
        else if(decision.lateness)debt_ticks=0;
        if(wait && debt_ticks){const uint64_t used=wait<debt_ticks?wait:debt_ticks;debt_ticks-=used;repaid_ticks+=used;wait-=used;}
    }
    if(enabled && wait){
        // Overshoot remains debt; no artificial device events or extra game
        // updates are generated to catch up after a slow presentation.
        const auto start=svcGetSystemTick();
        const auto target=start+wait;
        // Sleep to within spin_ticks of the deadline, then poll: thread
        // wake-up lands up to ~1 ms late, which made on-time 30 Hz updates
        // overshoot their period (castle-30fps). Nothing else runs on this
        // core while the game thread is parked at VBlank.
        static const uint64_t spin_ticks=[](){const char* s=std::getenv("PLAYER_CADENCE_SPIN_US");
            return uint64_t(s?std::strtoull(s,nullptr,10):1500ull)*SYSCLOCK_ARM11/1000000ull;}();
        for(;;){
            const auto now=svcGetSystemTick();if(now>=target)break;
            if(target-now<=spin_ticks)continue;
            const auto ns=(target-now-spin_ticks)*1000000000ull/SYSCLOCK_ARM11;
            svcSleepThread(ns?ns:1);
        }
        wait_ticks+=svcGetSystemTick()-start;
    }
}
bool behind(){
    const uint64_t frame_ticks=SYSCLOCK_ARM11*560190ull/33513982ull;
    return configured && enabled && catch_up && clock && debt_ticks>frame_ticks/2;
}
void note_skipped(){++skipped_frames;}
void report(){
    if(!configured)return;
    std::printf("Observed cadence enabled=%u frames=%llu cycles=%llu rebases=%llu discarded=%llu waitTicks=%llu lateTicks=%llu histogram=",
        unsigned(enabled),(unsigned long long)last_frame,(unsigned long long)observed_cycles,
        (unsigned long long)(clock?clock->rebases():0),(unsigned long long)(clock?clock->discarded():0),
        (unsigned long long)wait_ticks,(unsigned long long)late_ticks);
    for(unsigned i=0;i<9;++i)std::printf("%s%llu",i?",":"",(unsigned long long)histogram[i]);
    std::printf(" partial=%llu closedFrames=%llu maxCycle=%llu bootstrap=%llu\n",
        (unsigned long long)cycle_frames,(unsigned long long)completed_cycle_frames,
        (unsigned long long)max_cycle_frames,(unsigned long long)bootstrap_frames);
    std::printf("Frame skip catchUp=%u skippedFrames=%llu repaidMs=%.1f\n",unsigned(catch_up),(unsigned long long)skipped_frames,double(repaid_ticks)*1000.0/SYSCLOCK_ARM11);
}
}
extern "C" void sm64ds_observed_cycle_begin(){observed_cadence::cycle_begin();}
