#include "audio/sound_commands.h"
#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
// Compile the unmodified original getter in this focused probe only.
extern "C" {
unsigned int func_0205b608(void);
extern unsigned int* data_020a7fc0;
}
static unsigned read32(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);}
static unsigned read16(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 3;
 char path[512];std::snprintf(path,sizeof(path),"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
 FILE* in=std::fopen(path,"rb");if(!in)return 4;std::fseek(in,0,SEEK_END);unsigned length=std::ftell(in);std::rewind(in);
 auto* bytes=static_cast<unsigned char*>(std::malloc(length));if(!bytes)return 5;
 if(std::fread(bytes,1,length,in)!=length)return 6;std::fclose(in);
 auto* info=bytes+read32(bytes+24);auto* fat=bytes+read32(bytes+32);
 auto entry=[&](unsigned type,unsigned id){auto* table=info+read32(info+8+type*4);return info+read32(table+4+id*4);};
 auto file=[&](unsigned id){auto* f=fat+12+id*16;return Sm64dsSoundBuffer{bytes+read32(f),read32(f+4)};};
 auto* meta=entry(0,55);auto sequence=file(read32(meta));auto* raw=static_cast<const unsigned char*>(sequence.data);
 auto* bankMeta=entry(2,read16(meta+4));auto bank=file(read32(bankMeta));Sm64dsSoundBuffer waves[4]{};
 std::printf("command mixer root_before=%u\n",root->VMemoryLeft());
 const unsigned rootBefore=root->VMemoryLeft();
 {
 Sm64dsSoundCommands service;
 if(!service.registerResource(sequence)||!service.registerResource(bank))return 7;
 for(unsigned i=0;i<4;++i){
  unsigned id=read16(bankMeta+4+i*2);uint32_t address=0;
  if(id!=0xffff){waves[i]=file(read32(entry(3,id)));if(!service.registerResource(waves[i]))return 8;address=reinterpret_cast<std::uintptr_t>(waves[i].data);}
  std::memcpy(static_cast<unsigned char*>(const_cast<void*>(bank.data))+0x18+i*8,&address,4);
 }
 std::uintptr_t args[4]={3,reinterpret_cast<std::uintptr_t>(raw+read32(raw+24)),0,reinterpret_cast<std::uintptr_t>(bank.data)};

 const auto events=args[1];
 const auto cursor=events+(static_cast<const unsigned char*>(reinterpret_cast<const void*>(events))[0]==0xfe?3:0);
 auto start=[&](){return service.execute(0,args)&&service.activeMask()==8;};
 auto invalidate=[&](unsigned command,std::uintptr_t first,std::uintptr_t last){std::uintptr_t range[4]={first,last,0,0};return service.execute(command,range);};
 {
  std::int16_t pcm[1024];Sm64dsSoundStatus status{};
  auto render=[&](unsigned blocks,bool expectSilent){
   bool heard=false;
   for(unsigned i=0;i<blocks;++i){if(!service.render(pcm,512))return false;service.publishStatus(status);heard|=status.enabledChannels!=0;if(expectSilent && status.enabledChannels)return false;}
   return expectSilent || heard;
  };
  std::uintptr_t reservation[4]={0xffff,0,0,0};
  if(!start() || !render(32,false) || !service.execute(0x16,reservation))return 30;
  service.publishStatus(status);if(status.enabledChannels || !render(32,true))return 31;
  if(!service.execute(0x17,reservation) || !render(128,false))return 32;
  reservation[1]=1;
  if(!service.execute(0x16,reservation) || !start() || !render(32,true))return 33;
  std::uintptr_t trackChannels[4]={3,0xffff,0xffff,0};
  if(!service.execute(6,trackChannels) || !render(128,false))return 34;
  reservation[1]=0;
  if(!service.execute(0x16,reservation) || !render(32,true))return 35;
  if(!service.execute(0x17,reservation) || !render(128,false))return 36;
  reservation[1]=1;if(!service.execute(0x17,reservation))return 37;
  std::uintptr_t stop[4]={3,0,0,0};if(!service.execute(1,stop))return 38;
  // Clear any release tails before the cursor-boundary cases below.
  reservation[1]=0;if(!service.execute(0x16,reservation)||!service.execute(0x17,reservation))return 39;
  std::puts("sound reservations: strong/weak locks stop channels, suppress notes, preserve explicit-mask override and unlock");
 }
 if(!start() || !invalidate(0x1b,cursor-1,cursor-1)||service.activeMask()!=8)return 10;
 if(!invalidate(0x1b,cursor,cursor)||service.activeMask())return 11;
 if(!start() || !invalidate(0x1c,reinterpret_cast<std::uintptr_t>(bank.data)-1,reinterpret_cast<std::uintptr_t>(bank.data)-1)||service.activeMask()!=8)return 12;
 if(!invalidate(0x1c,reinterpret_cast<std::uintptr_t>(bank.data),reinterpret_cast<std::uintptr_t>(bank.data))||service.activeMask())return 13;
 if(!start())return 14;
 std::int16_t pcm[1024];Sm64dsSoundStatus status{};
 for(unsigned i=0;i<128;++i){if(!service.render(pcm,512))return 15;service.publishStatus(status);if(status.enabledChannels)break;}
 if(!status.enabledChannels || service.activeMask()!=8)return 16;
 std::uintptr_t pause[4]={3,1,0,0};if(!service.execute(2,pause))return 17;
 for(auto wave:waves)if(wave.data){const auto first=reinterpret_cast<std::uintptr_t>(wave.data);if(!invalidate(0x1d,first,first+wave.size))return 18;}
 service.publishStatus(status);
 if(status.enabledChannels || service.activeMask()!=8)return 19;
 pause[1]=0;if(!service.execute(2,pause))return 23;
 for(unsigned i=0;i<128;++i){if(!service.render(pcm,512))return 24;service.publishStatus(status);if(status.enabledChannels)return 25;}
 pause[1]=1;if(!service.execute(2,pause))return 26;
 // Bank invalidation stops the player even when paused; wave invalidation did not.
 if(!invalidate(0x1c,reinterpret_cast<std::uintptr_t>(bank.data),reinterpret_cast<std::uintptr_t>(bank.data)+bank.size)||service.activeMask())return 20;
 if(start())return 21; // Deleted resources must no longer resolve for new starts.
 std::printf("sound invalidation: cursor and bank inclusive boundaries, PCM channels only, paused player stop, stale resource rejection\n");
 }
 if(root->VMemoryLeft()!=rootBefore)return 22;
 std::free(bytes);std::puts("smoke_sound_invalidation: PASS ARM7-derived selectors and exact mixer heap recovery");return 0;
}
