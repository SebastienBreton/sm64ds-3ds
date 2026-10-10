#pragma once
#include <cstdint>
#include <limits>
// One event() AFTER each original FrameEnd's work;
// begin_cycle() ONLY at the actual original outer-loop boundary, never inferred
// from geometry, scene, a cache hit, or a guessed yield count. No device stepping.
namespace observed_cycle {
struct Decision { bool valid=false; uint64_t wait=0,lateness=0,target=0; };
class Clock {
 uint64_t next_=0,whole_=0,remainder_=0,denominator_=1,fraction_=0;
 uint64_t frame_=0,cycle_events_=0,last_late_=0,cycles_=0,rebases_=0,discarded_=0;
 bool valid_=false;
 bool advance(){
  const bool carry=remainder_ && fraction_>=denominator_-remainder_;
  if(carry)fraction_-=denominator_-remainder_;else fraction_+=remainder_;
  const auto max=std::numeric_limits<uint64_t>::max();
  if(next_>max-whole_ || (carry && next_+whole_==max))return valid_=false;
  next_+=whole_+unsigned(carry);return true;
 }
public:
 Clock(uint64_t rate,uint64_t hz_n,uint64_t hz_d,uint64_t now,uint64_t completed_frames=0):next_(now),frame_(completed_frames){
  if(!rate||!hz_n||!hz_d||rate>std::numeric_limits<uint64_t>::max()/hz_d)return;
  const auto scaled=rate*hz_d;whole_=scaled/hz_n;remainder_=scaled%hz_n;denominator_=hz_n;
  if(!whole_)return;valid_=true;advance();
 }
 bool begin_cycle(){
  if(!valid_)return false;
  // Last observed deadline decision is the end of the preceding cycle. Use
  // its lateness, NOT current wall time: post-hook CPU work and an overslept
  // early wait must not create a fresh period on every fast cycle.
  if(cycle_events_ && last_late_){
   const auto max=std::numeric_limits<uint64_t>::max();
   if(next_>max-last_late_||discarded_>max-last_late_)return valid_=false;
   next_+=last_late_;discarded_+=last_late_;++rebases_;
  }
  cycle_events_=0;last_late_=0;++cycles_;return true;
 }
 Decision event(uint64_t completed_frame,uint64_t now){
  Decision d;
  if(!valid_||frame_==std::numeric_limits<uint64_t>::max()||completed_frame!=frame_+1){valid_=false;return d;}
  d.target=next_;d.wait=now<next_?next_-now:0;d.lateness=now>next_?now-next_:0;
  ++frame_;++cycle_events_;last_late_=d.lateness;
  if(!advance())return d;
  d.valid=true;return d;
 }
 bool valid()const{return valid_;} uint64_t target()const{return next_;}
 uint64_t events_in_cycle()const{return cycle_events_;} uint64_t frames()const{return frame_;}
 uint64_t cycles()const{return cycles_;} uint64_t rebases()const{return rebases_;}
 uint64_t discarded()const{return discarded_;}
};
}
