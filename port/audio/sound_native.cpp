#include "sound_native.h"
#include "sound_commands.h"
#include "sound_output_3ds.h"
#include <new>
#include <cstdio>
#include <cstdlib>
#include <3ds.h>
extern "C" {
volatile int sm64ds_sound_game_waiting=0;
__attribute__((weak)) bool sm64ds_on_present_worker(){return false;}
void hal_flush_deferred_frees();
extern Sm64dsSoundNode data_020a6760[256];
extern unsigned char nativeSoundStatusBytes[0x280] asm("data_020a64e0");
extern unsigned int* data_020a7fc0;
void sm64ds_native_sound_alarm(unsigned) __attribute__((weak));
void sm64ds_native_sound_alarm_direct(unsigned) __attribute__((weak));
}
namespace {
// The realtime pump may run on the core-1 worker, like the DS ARM7 beside the
// game. `lock` serializes the command service between the two threads.
// Alarms raised by a worker render are game code: they wait for the game side.
struct Backend {
 Sm64dsSoundCommands service;
 Sm64dsSoundQueue queue;
 Sm64dsSoundOutput output; // Joined before queue/service destruction.
 bool outputOpen=false;
 RecursiveLock lock;
 // Periodic alarms keep firing while a long game update (scene load) runs;
 // coalesce repeats as counts so every callback is still delivered once
 // the game parks, in bounded storage (8 alarm indices x a few tags).
 struct Pending{unsigned packet,count;};
 Pending alarms[64]{};unsigned alarmCount=0;bool alarmOverflow=false;
 Backend():queue(service,data_020a6760,256,*reinterpret_cast<Sm64dsSoundStatus*>(nativeSoundStatusBytes)){
  RecursiveLock_Init(&lock);
  if(sm64ds_native_sound_alarm)service.setAlarmCallback([](unsigned index,unsigned tag,void* context){
   auto* self=static_cast<Backend*>(context);const unsigned packet=index|(tag<<8);
   if(!sm64ds_on_present_worker()){sm64ds_native_sound_alarm(packet);return;}
   // Capture-effect alarms must run at their audio position: deferred to the
   // next game frame, ch1/ch3 replayed half-buffers unprocessed or processed
   // twice (audible ~15.6 ms discontinuities). See sound_worker.cpp.
   if(sm64ds_native_sound_alarm_direct){sm64ds_native_sound_alarm_direct(packet);return;}
   if(self->alarmCount && self->alarms[self->alarmCount-1].packet==packet){++self->alarms[self->alarmCount-1].count;return;}
   for(unsigned i=0;i<self->alarmCount;++i)if(self->alarms[i].packet==packet && self->alarmCount==64){++self->alarms[i].count;return;}
   if(self->alarmCount<64)self->alarms[self->alarmCount++]={packet,1};else self->alarmOverflow=true;
  },this);
 }
};
// Game-side lockers announce themselves; the worker's chunk loop yields to
// them so a game command never waits behind several consecutive renders.
struct Guard {RecursiveLock* l;explicit Guard(RecursiveLock* lock):l(lock){
 __sync_fetch_and_add(&sm64ds_sound_game_waiting,1);RecursiveLock_Lock(l);__sync_fetch_and_sub(&sm64ds_sound_game_waiting,1);}
 ~Guard(){RecursiveLock_Unlock(l);}};
alignas(Backend) unsigned char storage[sizeof(Backend)];
Backend* backend=nullptr;
[[noreturn]] void fatal(const char* message){
 std::fprintf(stderr,"FATAL: native sound IPC: %s\n",message);std::fflush(nullptr);std::abort();
}
}
// The original ARM7-ready polling is replaced by availability of the synchronous
// service. Original command allocation/initialization/submission stay intact.
extern "C" void func_0205ae64(){if(!backend)backend=new(storage) Backend;}
extern "C" int IPCSend(unsigned int channel,unsigned int payload,unsigned int flag){
 if(channel!=7||flag)fatal("unsupported channel or IPC flag");
 if(!backend)fatal("audio service not initialized");
 // Decode new wave archives before taking the lock (the realtime renderer
 // would otherwise starve for the 30-110 ms of a first-use SWAR decode).
 if(payload)backend->queue.prepare(payload);
 Guard guard(&backend->lock);
 if(payload){if(!backend->queue.submit(payload))fatal(backend->queue.error());}
 else if(!backend->queue.refresh())fatal(backend->queue.error());
 return 0;
}
bool sm64ds_native_sound_register(Sm64dsSoundBuffer resource){
 if(!backend)return false;Guard guard(&backend->lock);return backend->service.registerResource(resource);
}
void sm64ds_native_sound_unregister(const void* resource){
 if(backend){Guard guard(&backend->lock);backend->service.unregisterResource(resource);}
}
bool sm64ds_native_sound_render(std::int16_t* pcm,unsigned frames){
 return backend&&!backend->outputOpen&&backend->service.render(pcm,frames)&&backend->queue.refresh();
}
bool sm64ds_native_sound_output_open(){
 if(!backend || backend->outputOpen)return false;
 backend->output.setLock(&backend->lock);
 backend->outputOpen=backend->output.open(backend->service);
 return backend->outputOpen;
}
bool sm64ds_native_sound_output_pump(){
 if(!backend||!backend->outputOpen||!backend->output.pump())return false;
 Guard guard(&backend->lock);return backend->queue.refresh();
}
// Game side, game thread parked: deliver alarms raised by worker renders,
// release retired players and any game-heap blocks the worker gave up.
bool sm64ds_native_sound_maintenance(){
 if(!backend)return false;
 Backend::Pending pending[64];unsigned count;bool overflow;unsigned total=0;
 {
  Guard guard(&backend->lock);

  count=backend->alarmCount;overflow=backend->alarmOverflow;
  for(unsigned i=0;i<count;++i)pending[i]=backend->alarms[i];
  backend->alarmCount=0;backend->alarmOverflow=false;
  backend->service.collect();
 }
 hal_flush_deferred_frees();
 if(overflow){std::fputs("FATAL: native sound deferred alarm queue overflow\n",stderr);return false;}
 for(unsigned i=0;i<count;++i)for(unsigned n=0;n<pending[i].count;++n){sm64ds_native_sound_alarm(pending[i].packet);++total;}
 static unsigned peak=0;if(total>peak){peak=total;if(peak>8)std::printf("Native sound deferred alarms burst=%u entries=%u\n",total,count);}
 return true;
}
// Same as sm64ds_native_sound_maintenance when the mixer lock is free;
// otherwise does nothing and reports done=false: the caller retries with the
// blocking call, then sm64ds_native_sound_retract().
bool sm64ds_native_sound_try_maintenance(bool& done){
 done=false;
 if(!backend)return false;
 Backend::Pending pending[64];unsigned count;bool overflow;unsigned total=0;
 // Busy: return at once so presentation is not blocked; the caller completes
 // maintenance with the blocking call after presenting. No early announce:
 // parking the mixer for a whole presentation starved the audio thread
 // (audioLoad 21% -> 40% in the mini-game route). The blocking call's Guard
 // announces itself and waits at most one 128-frame render chunk.
 if(RecursiveLock_TryLock(&backend->lock))return true;
 count=backend->alarmCount;overflow=backend->alarmOverflow;
 for(unsigned i=0;i<count;++i)pending[i]=backend->alarms[i];
 backend->alarmCount=0;backend->alarmOverflow=false;
 backend->service.collect();
 RecursiveLock_Unlock(&backend->lock);
 done=true;
 hal_flush_deferred_frees();
 if(overflow){std::fputs("FATAL: native sound deferred alarm queue overflow\n",stderr);return false;}
 for(unsigned i=0;i<count;++i)for(unsigned n=0;n<pending[i].count;++n){sm64ds_native_sound_alarm(pending[i].packet);++total;}
 return true;
}
// Kept for callers of the try/blocking pair; the try path no longer announces.
void sm64ds_native_sound_retract(){}
void sm64ds_native_sound_output_close(){
 if(backend){backend->output.close();backend->outputOpen=false;}
}
bool sm64ds_native_sound_output_counts(unsigned& submitted,unsigned& completed){
 if(!backend)return false;
 submitted=backend->output.submittedBuffers();completed=backend->output.completedBuffers();return true;
}
const char* sm64ds_native_sound_output_error(){
 return backend?backend->output.error():"native sound backend not initialized";
}
void sm64ds_native_sound_shutdown(){
 if(backend){backend->~Backend();backend=nullptr;data_020a7fc0=nullptr;}
}
