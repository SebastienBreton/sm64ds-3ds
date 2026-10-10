#include "ntr/mmio.h"
#include "audio/sound_worker.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" {
void func_020506fc(int);
void func_020500a4(void*);
extern int data_020a55fc,data_020a55f8;
extern unsigned data_020a5600[5];
}
static std::int16_t left[1024],right[1024];
static unsigned calls=0,errors=0;
static void callback(void* l,void* r,unsigned bytes,int format,void* arg){
 unsigned offset=(calls%2)*512;
 if(l!=left+offset || r!=right+offset || bytes!=1024 || format!=0 || arg!=&calls)++errors;
 static_cast<std::int16_t*>(l)[0]=calls+1;
 static_cast<std::int16_t*>(r)[0]=-(int(calls)+1);
 ++calls;
}
int main(){
 if(!ntr::io_init())return 2;
 alignas(4) unsigned stream[20]{};
 stream[0]=1;stream[1]=1;stream[2]=0;
 stream[3]=reinterpret_cast<unsigned>(left);stream[4]=reinterpret_cast<unsigned>(right);
 stream[5]=sizeof(left);stream[11]=2;
 stream[12]=reinterpret_cast<unsigned>(&callback);stream[13]=reinterpret_cast<unsigned>(&calls);
 data_020a55fc=0;
 // Original synchronous fallback and native worker must receive the same
 // half-buffer callback ABI, including byte lengths and block alternation.
 for(unsigned i=0;i<2;++i)func_020500a4(stream);
 if(calls!=2 || errors || stream[6])return 3;
 func_020506fc(2);
 if(!data_020a55fc || data_020a55f8 || data_020a5600[2]!=8)return 4;
 for(unsigned cycle=0;cycle<16;++cycle){
  unsigned before=calls;
  for(unsigned i=0;i<8;++i)func_020500a4(stream);
  if(calls!=before || data_020a5600[4]!=8 || data_020a55f8 || stream[6])return 5;
  // Reinitialization must preserve pending work.
  func_020506fc(2);
  if(data_020a5600[4]!=8 || !sm64ds_sound_worker_poll())return 6;
  if(calls!=before+8 || errors || data_020a5600[4])return 7;
  if(!sm64ds_sound_worker_poll() || calls!=before+8)return 8;
 }
 if(left[0]!=129 || left[512]!=130 || right[0]!=-129 || right[512]!=-130)return 9;
 std::puts("smoke_sound_worker: PASS original direct callback ABI,128 queued blocks,16 ring wraps,idempotent init,idle poll");
 return 0;
}
