#include "ntr/mmio.h"
#include "Heap.h"
#include "audio/sound_commands.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
static std::int16_t output[2][8192*2];
struct Context {
 Sm64dsSoundCommands* service;
 std::int16_t source[64]{};
 unsigned count[8]{},events[128]{},used=0,errors=0;
};
static void alarm(unsigned index,unsigned tag,void* opaque){
 auto& c=*static_cast<Context*>(opaque);
 if(index>=3 || tag!=(index==0?7:index==1?9:19) || c.used>=128){++c.errors;return;}
 c.events[c.used++]=(index<<8)|tag;++c.count[index];
 for(auto& sample:c.source)sample=1000+int(c.used)*113;
 if(index==2 && c.count[index]==3){
  std::uintptr_t stop[4]={0,0,4,0};if(!c.service->execute(10,stop))++c.errors;
 }
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 3;
 const auto before=root->VMemoryLeft();Context contexts[2]{};
 for(unsigned run=0;run<2;++run){
  Sm64dsSoundCommands service;auto& c=contexts[run];c.service=&service;
  service.setAlarmCallback(alarm,&c);
  std::uintptr_t pcm[4]={1|(512u<<16),reinterpret_cast<std::uintptr_t>(c.source),32|(127u<<24),(1u<<24)|(1u<<26)};
  if(!service.execute(0xb,pcm))return 4;
  for(unsigned i=0;i<3;++i){
   std::uintptr_t a[4]={i,i==1?10u:0x2100u,i==1?0u:0x2000u,i==0?7u:i==1?9u:19u};
   if(!service.execute(0xf,a))return 5;
  }
  std::uintptr_t start[4]={2,0,7,0};if(!service.execute(9,start))return 6;
  unsigned step=run?1024:1;
  for(unsigned i=0;i<8192;i+=step)if(!service.render(output[run]+i*2,step))return 7;
  // ARM7 alarm ticks are clock/64; first delay0x2100, period0x2000.
  // The service mixes at the DS-native cadence: 512 sound-timer ticks/frame.
  const std::uint64_t elapsed=std::uint64_t(8192)*512;
  const unsigned expected=1+(elapsed-std::uint64_t(0x2100)*32)/(std::uint64_t(0x2000)*32);
  if(c.errors || c.count[0]!=expected || c.count[1]!=1 || c.count[2]!=3)return 8;
  if(c.events[0]!=(1u<<8|9) || c.events[1]!=7 || c.events[2]!=(2u<<8|19))return 9;
  std::uintptr_t stop[4]={2,0,1,0};if(!service.execute(10,stop))return 10;
  unsigned saved=c.used;std::int16_t scratch[1024];if(!service.render(scratch,512)||c.used!=saved)return 11;
 }
 if(std::memcmp(output[0],output[1],sizeof output[0]) || contexts[0].used!=contexts[1].used ||
    std::memcmp(contexts[0].events,contexts[1].events,sizeof contexts[0].events))return 12;
 if(root->VMemoryLeft()!=before)return 13;
 std::puts("smoke_sound_alarm: PASS audio-clock deadlines,one-shot/periodic,ordered simultaneous events,callback stop,PCM equality for1/1024-frame chunks,heap recovery");return 0;
}
