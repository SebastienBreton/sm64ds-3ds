#include "platform/3ds/observed_cycle_clock.h"
#include <cassert>
#include <iostream>
#include <vector>
using observed_cycle::Clock;
static uint64_t run(const std::vector<unsigned>& work,const std::vector<unsigned>& cycle_sizes){
 Clock c(60000,60,1,0);uint64_t now=0,frame=0;size_t i=0;
 for(auto count:cycle_sizes){assert(c.begin_cycle());for(unsigned j=0;j<count;++j){now+=work.at(i++);auto d=c.event(++frame,now);assert(d.valid);now+=d.wait;}}
 assert(i==work.size()&&frame==work.size());return now;
}
int main(){
 // A period is1000ticks. Preserve debt within actual2-yield cycle.
 assert(run({2400,300},{2})==2700); //40ms +5ms, not56.67ms
 assert(run({2400,300,2400,300},{2,2})==5400);
 assert(run({100,100,100,100},{2,2})==4000); // early work remains paced
 // Genuine arbitrary cycles and30/60 style transitions; no predictedlength.
 assert(run({2400,300,100,100,100,100},{2,1,3})==6700);
 // One slow event followed by8held boundaries repays debt with no deletion.
 assert(run({7000,1,1,1,1,1,1,1,1},{9})==9000);
 // A long pause translates epoch only at next observed cycle, no catch-up events.
 assert(run({600000,1,100},{2,1})==601001);
 {Clock c(60000,60,1,0);assert(c.begin_cycle());auto d=c.event(1,100);assert(d.wait==900);assert(c.begin_cycle());assert(c.target()==2000);}
 // Rational rate has no drift over a large no-overload trace and variedcycles.
 {Clock c(1000000,60000,1001,0);uint64_t now=0;for(uint64_t f=1;f<=100000;++f){if(f%7==1)assert(c.begin_cycle());auto d=c.event(f,now);assert(d.valid);now+=d.wait;assert(now==(f*1000000ull*1001)/60000);}assert(!c.rebases());}
 // Reject duplicate/skipped device frames, invalidfrequency and overflow.
 {Clock c(60000,60,1,0);assert(!c.event(2,0).valid);assert(!c.begin_cycle());}
 {Clock c(60000,60,1,0);assert(c.event(1,0).valid);assert(!c.event(1,0).valid);}
 {Clock c(0,60,1,0);assert(!c.valid());}
 {Clock c(UINT64_MAX,1,2,0);assert(!c.valid());}
 {Clock c(60000,60,1,UINT64_MAX-10);assert(!c.valid());}
 std::cout<<"PASS observed-cycle policy: overload, early pacing,1/2/3/9yield cycles, transitions, longpause,100000rational events, invalid/overflow guards\n";
}
