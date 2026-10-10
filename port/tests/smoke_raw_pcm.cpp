#include "ntr/mmio.h"
#include "Heap.h"
#include "audio/sound_commands.h"
#include <cstdio>
#include <cstdint>
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 3;
 auto before=root->VMemoryLeft();
 {
  Sm64dsSoundCommands service;
  alignas(4) std::int16_t left[64],right[64];
  for(unsigned i=0;i<64;++i){left[i]=4096;right[i]=-8192;}
  std::uintptr_t setup[4]={1|(512u<<16),reinterpret_cast<std::uintptr_t>(left),32|(127u<<24),(1u<<26)|(1u<<24)};
  if(!service.execute(0xb,setup))return 4;
  setup[0]=3|(512u<<16);setup[1]=reinterpret_cast<std::uintptr_t>(right);setup[3]|=127u<<16;
  if(!service.execute(0xb,setup))return 5;
  std::int16_t pcm[1024];Sm64dsSoundStatus status{};
  if(!service.render(pcm,512))return 6;
  for(auto s:pcm)if(s)return 7; // Configuration alone does not start playback.
  std::uintptr_t start[4]={10,0,0,0};if(!service.execute(9,start)||!service.render(pcm,512))return 8;
  service.publishStatus(status);if(status.enabledChannels!=10)return 9;
  for(unsigned i=32;i<512;++i)if(pcm[i*2]!=4096||pcm[i*2+1]!=-8192)return 10;
  for(auto& v:left)v=-4096; // Capture writes must become visible after FIFO refill.
  if(!service.render(pcm,512))return 11;
  for(unsigned i=32;i<512;++i)if(pcm[i*2]!=-4096||pcm[i*2+1]!=-8192)return 12;
  start[0]=2;start[3]=1;
  if(service.execute(10,start))return 20; // Hold must not silently become stop.
  service.publishStatus(status);if(status.enabledChannels!=10)return 21;
  start[3]=0;if(!service.execute(10,start)||!service.render(pcm,512))return 13;
  service.publishStatus(status);if(status.enabledChannels!=8)return 14;
  for(unsigned i=0;i<512;++i)if(pcm[i*2]||pcm[i*2+1]!=-8192)return 15;
  start[0]=8;if(!service.execute(10,start)||!service.render(pcm,512))return 16;
  service.publishStatus(status);if(status.enabledChannels)return 17;
  for(auto s:pcm)if(s)return 18;
 }
 if(root->VMemoryLeft()!=before)return 19;
 std::puts("smoke_raw_pcm: PASS packed native PCM commands,independent stereo channels,live DMA source,stop/status,exact heap recovery");return 0;
}
