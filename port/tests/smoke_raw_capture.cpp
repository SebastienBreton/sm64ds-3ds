#include "ntr/mmio.h"
#include "Heap.h"
#include "audio/sound_commands.h"
#include "audio/sound_surround.h"
#include "../../../build3ds/validation/sound-surround/reference.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 3;
 const auto before=root->VMemoryLeft();
 unsigned hash=2166136261u;
 for(unsigned amount:{0u,0x1000u,0x3000u,0x8000u})for(unsigned pan=0;pan<128;++pan)for(unsigned volume:{0u,1u,64u,126u,127u})
  hash=(hash^sm64ds_surround_volume(volume,pan,amount))*16777619u;
 if(hash!=surround_reference_hash)return 23;
 {
  Sm64dsSoundCommands service;
  alignas(4) std::int16_t inputLeft[64],inputRight[64],captureLeft[1024]{},captureRight[1024]{};
  for(unsigned i=0;i<64;++i){inputLeft[i]=4096;inputRight[i]=-8192;}
  auto setup=[&](unsigned id,void* data,unsigned words,unsigned pan){
   std::uintptr_t a[4]={id|(512u<<16),reinterpret_cast<std::uintptr_t>(data),words|(127u<<24),(1u<<24)|(1u<<26)|(pan<<16)};
   return service.execute(0xb,a);
  };
  if(!setup(4,inputLeft,32,0)||!setup(5,inputRight,32,127)||!setup(1,captureLeft,512,0)||!setup(3,captureRight,512,127))return 4;
  std::uintptr_t capture[4]={reinterpret_cast<std::uintptr_t>(captureLeft),512,0x20000000,0};
  if(!service.execute(0xe,capture))return 5;
  capture[0]=reinterpret_cast<std::uintptr_t>(captureRight);capture[2]=0xa0000000;
  if(!service.execute(0xe,capture))return 6;
  std::uintptr_t route[4]={3,3,1,1};if(!service.execute(0x15,route))return 7;
  std::uintptr_t start[4]={0x3a,3,0,0};if(!service.execute(9,start))return 8;
  std::int16_t pcm[1024];Sm64dsSoundStatus status{};
  for(unsigned block=0;block<8;++block)if(!service.render(pcm,512))return 9;
  service.publishStatus(status);if(status.enabledChannels!=0x3a || status.activeCaptures!=3)return 10;
  for(unsigned i=0;i<1024;++i)if(captureLeft[i]!=4096||captureRight[i]!=-8192)return 11;
  for(unsigned i=0;i<512;++i)if(pcm[i*2]!=4096||pcm[i*2+1]!=-8192)return 12;
  std::uintptr_t surround[4]={0x3000,0,0,0};if(!service.execute(0x13,surround))return 24;
  for(unsigned block=0;block<8;++block)if(!service.render(pcm,512))return 25;
  for(unsigned i=0;i<1024;++i)if(captureLeft[i]!=3488||captureRight[i]!=-6976)return 26;
  for(unsigned i=0;i<512;++i)if(pcm[i*2]!=3488||pcm[i*2+1]!=-6976)return 27;
  surround[0]=0;if(!service.execute(0x13,surround))return 28;
  // Disable output bypass: the source channels still capture identically, but
  // a direct selector for the opposite panned channel must be silent.
  route[0]=2;route[1]=1;if(!service.execute(0x15,route)||!service.render(pcm,512))return 13;
  for(auto v:pcm)if(v)return 14;
  // Stop capture without stopping playback; clearing its RAM must persist.
  start[0]=0;if(!service.execute(10,start))return 15;
  std::memset(captureLeft,0,sizeof captureLeft);std::memset(captureRight,0,sizeof captureRight);
  route[0]=route[1]=3;if(!service.execute(0x15,route))return 16;
  for(unsigned block=0;block<4;++block)if(!service.render(pcm,512))return 17;
  for(unsigned i=0;i<1024;++i)if(captureLeft[i]||captureRight[i])return 18;
  for(auto v:pcm)if(v)return 19;
  service.publishStatus(status);if(status.activeCaptures || status.enabledChannels!=0x3a)return 20;
  start[0]=0x3a;start[1]=0;if(!service.execute(10,start))return 21;
 }
 if(root->VMemoryLeft()!=before)return 22;
 std::puts("smoke_raw_capture: PASS source mix -> stereo DMA capture -> PCM playback,output routing,no feedback,capture-only stop,status and exact heap recovery");return 0;
}
