#include "sound_worker.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
struct Stream {
 std::uint32_t active,kind,format;
 void* left;
 void* right;
 std::uint32_t bytes,index,channels,playChannels,captures;
 std::int32_t alarm;
 std::uint32_t blocks;
 void (*callback)(void*,void*,unsigned,int,void*);
 void* argument;
};
struct Work { Stream* stream; unsigned bytes,offset; void* left; void* right; };
struct Queue { std::uint16_t send,receive; Work** array; int capacity,first,used; };
static_assert(sizeof(Work)==0x14 && sizeof(Queue)==0x14);
static_assert(offsetof(Stream,callback)==0x30 && offsetof(Stream,argument)==0x34);
bool polling=false;
}
extern "C" {
extern int data_020a55fc,data_020a55f8;
extern Queue data_020a5600;
extern Work* data_020a5614[8];
Work data_020a5718[8]{};
void func_02058940(void*,void*,int);
int func_020587e4(void*,void*,int);
void _ZN4CP1519InvalidateDataCacheEjj(unsigned,unsigned);
void func_0205b470(unsigned);

// Replacement for the SDK thread start in func_020506fc. Queue layout and
// messages remain original; the worker is advanced by the native audio pump.
void func_020506fc(int /*priority*/) {
 if(data_020a55fc)return;
 data_020a55f8=0;
 func_02058940(&data_020a5600,data_020a5614,8);
 data_020a55fc=1;
}
}
extern "C" void sm64ds_native_sound_alarm(unsigned packet){
 func_0205b470(packet);
 if(!sm64ds_sound_worker_poll()){
  std::fputs("FATAL: native sound stream worker queue\n",stderr);std::abort();
 }
}
// Worker-thread alarm delivery (the DS runs it from the ARM7 alarm IRQ). The
// game's only alarm user is the NNS capture effect, func_020500a4: with its
// capture thread present it posts through OS_SendMessage (emulated IRQ mask
// and thread wakeup, game-thread only). Its no-thread branch selects the same
// half-buffer and calls the effect callback (surround/headphone DSP, no OS or
// IRQ calls) at the alarm's exact audio position, before ch1/ch3 replay it.
// The caller holds the sound service lock (render), serializing the command
// side; no other code reads the thread flag outside this alarm context.
extern "C" void sm64ds_native_sound_alarm_direct(unsigned packet){
 const int thread=data_020a55fc;
 data_020a55fc=0;
 func_0205b470(packet);
 data_020a55fc=thread;
}
bool sm64ds_sound_worker_poll() {
 if(!data_020a55fc)return true;
 if(polling || data_020a5600.capacity!=8 || data_020a5600.used<0 || data_020a5600.used>8)return false;
 polling=true;
 // Snapshot the pending count: callback-produced messages belong to the next
 // pump, and must not make this cooperative worker monopolize the game task.
 const int count=data_020a5600.used;
 for(int i=0;i<count;++i){
  Work* work=nullptr;
  if(!func_020587e4(&data_020a5600,&work,0) || !work || !work->stream || !work->stream->callback){polling=false;return false;}
  _ZN4CP1519InvalidateDataCacheEjj(reinterpret_cast<unsigned>(work->left),work->bytes);
  _ZN4CP1519InvalidateDataCacheEjj(reinterpret_cast<unsigned>(work->right),work->bytes);
  work->stream->callback(work->left,work->right,work->bytes,work->stream->format,work->stream->argument);
 }
 polling=false;
 return true;
}
