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
 Sm64dsSoundStatus queuedStatus{};Sm64dsSoundNode queuedNodes[2]{};
 Sm64dsSoundQueue queue(service,queuedNodes,2,queuedStatus);
 queuedNodes[0].next=reinterpret_cast<std::uintptr_t>(queuedNodes+1);
 queuedNodes[0].command=0x19;queuedNodes[0].args[0]=reinterpret_cast<std::uintptr_t>(&queuedStatus);
 queuedNodes[1].command=0;for(unsigned i=0;i<4;++i)queuedNodes[1].args[i]=args[i];
 if(!queue.submit(reinterpret_cast<std::uintptr_t>(queuedNodes))){std::printf("queued start: %s\n",queue.error());return 9;}
 if(queuedStatus.completedBatches!=1||queuedStatus.activePlayers!=8)return 108;
 if(service.activeMask()!=8)return 10;
 Sm64dsSoundStatus status;std::memset(&status,0xa5,sizeof(status));
 auto checkStatus=[&](){
  service.publishStatus(status);
  auto* previous=data_020a7fc0;data_020a7fc0=reinterpret_cast<unsigned int*>(&status);
  auto mask=func_0205b608();data_020a7fc0=previous;
  if(mask!=service.activeMask()||status.completedBatches!=0xa5a5a5a5||status.activeCaptures)return false;
  for(auto byte:status.reserved)if(byte!=0xa5)return false;
  for(unsigned id=0;id<16;++id){
   std::uint32_t ticks;if(!service.ticks(id,&ticks)||status.players[id].ticks!=ticks)return false;
   for(unsigned index=0;index<32;++index){std::int16_t value;
    if(!service.variable(id,index,&value)||value!=(index<16?status.players[id].variables[index]:status.globals[index-16]))return false;
   }
  }
  return true;
 };
 if(!checkStatus()||status.enabledChannels)return 103;
 if(!queue.submit(reinterpret_cast<std::uintptr_t>(queuedNodes))||service.activeMask()!=8)return 11;
 if(service.execute(0x1e,args)||service.activeMask()!=8)return 12;
 // Empty and out-of-range masks must leave the known title PCM unchanged.
 std::uintptr_t noTracks[4]={0x02000003,0,0xa,0x8000};
 if(!service.execute(4,noTracks))return 69;
 noTracks[1]=0x10000;if(!service.execute(4,noTracks))return 70;
 std::int16_t pcm[2048];uint32_t hash=2166136261u;
 for(unsigned i=0;i<160;++i){if(!service.render(pcm,1024))return 13;for(auto b:reinterpret_cast<const unsigned char(&)[4096]>(pcm)){hash^=b;hash*=16777619u;}}
 if(hash!=0xb1442dcc || service.activeMask()!=8)return 14;
 if(!queue.refresh()||queuedStatus.completedBatches!=2||queuedStatus.activePlayers!=8||!queuedStatus.enabledChannels)return 109;
 if(!checkStatus()||!status.enabledChannels||!status.players[3].ticks)return 104;
 std::uintptr_t trackArgs[4]={0x02000003,0xffff,0xa,0x8000};
 if(!service.execute(4,trackArgs))return 60;
 for(unsigned i=0;i<8;++i)if(!service.render(pcm,1024))return 61;
 for(auto v:pcm)if(v)return 62;
 trackArgs[3]=0;if(!service.execute(4,trackArgs))return 63;
 unsigned trackRestored=0;for(unsigned i=0;i<8;++i){if(!service.render(pcm,1024))return 64;for(auto v:pcm)trackRestored+=v!=0;}
 if(!trackRestored)return 65;
 trackArgs[0]=0x01000003;trackArgs[2]=0xa;if(service.execute(4,trackArgs))return 66;
 trackArgs[2]=9;trackArgs[1]=0;if(!service.execute(4,trackArgs))return 67;
 trackArgs[0]=0x01000010;if(service.execute(4,trackArgs))return 68;
 std::printf("command track controls: packed width/id, mask, live mute/restore=%u PASS\n",trackRestored);
 std::uintptr_t channelArgs[4]={3,0xffff,0,0},releaseArgs[4]={3,0xffff,2,0};
 if(!service.execute(6,channelArgs)||!service.execute(5,releaseArgs))return 78;
 for(unsigned i=0;i<16;++i)if(!service.render(pcm,1024)||service.activeMask()!=8)return 79;
 for(auto v:pcm)if(v)return 80;
 channelArgs[2]=0x1ffff;if(!service.execute(6,channelArgs))return 81;
 unsigned maskRestored=0;for(unsigned i=0;i<80;++i){if(!service.render(pcm,1024))return 82;for(auto v:pcm)maskRestored+=v!=0;}
 if(!maskRestored)return 83;
 std::printf("command channel mask: zero forbids new notes, restore=%u nonzero PASS\n",maskRestored);
 std::uintptr_t muteArgs[4]={3,0xffff,1,0};
 if(!service.execute(5,muteArgs))return 71;
 for(unsigned i=0;i<80;++i)if(!service.render(pcm,1024)||service.activeMask()!=8)return 72;
 for(auto v:pcm)if(v)return 73;
 muteArgs[2]=0;if(!service.execute(5,muteArgs))return 74;
 unsigned unmuted=0;for(unsigned i=0;i<80;++i){if(!service.render(pcm,1024))return 75;for(auto v:pcm)unmuted+=v!=0;}
 if(!unmuted)return 76;
 muteArgs[2]=2;if(!service.execute(5,muteArgs))return 77;
 std::printf("command track mute: silent while active, unmuted_nonzero=%u, release-only accepted PASS\n",unmuted);
 std::uintptr_t priorityArgs[4]={3,4,0x140,1};
 if(!service.execute(3,priorityArgs))return 58;
 priorityArgs[3]=2;if(service.execute(3,priorityArgs))return 59;
 std::uintptr_t volumeArgs[4]={3,6,0x8000,2};
 if(!service.execute(3,volumeArgs))return 48;
 for(unsigned i=0;i<8;++i)if(!service.render(pcm,1024))return 49;
 for(auto v:pcm)if(v)return 50;
 volumeArgs[2]=0;if(!service.execute(3,volumeArgs))return 51;
 unsigned restored=0;for(unsigned i=0;i<8;++i){if(!service.render(pcm,1024))return 52;for(auto v:pcm)restored+=v!=0;}
 if(!restored)return 53;
 volumeArgs[3]=4;if(service.execute(3,volumeArgs))return 54;
 std::uintptr_t tempoArgs[4]={3,0x1a,128,2};if(!service.execute(3,tempoArgs))return 55;
 if(!service.render(pcm,1024))return 56;
 tempoArgs[2]=256;if(!service.execute(3,tempoArgs))return 57;
 std::printf("command attenuation/tempo: mute, restore=%u nonzero, signed16 and width validation PASS\n",restored);
 std::uintptr_t pauseArgs[4]={3,1,0,0};
 if(!service.execute(2,pauseArgs)||service.activeMask()!=8)return 42;
 for(unsigned i=0;i<160;++i)if(!service.render(pcm,1024)||service.activeMask()!=8)return 43;
 for(auto v:pcm)if(v)return 44;
 if(!checkStatus()||status.activePlayers!=8||status.enabledChannels)return 105;
 pauseArgs[1]=0;if(!service.execute(2,pauseArgs)||service.activeMask()!=8)return 45;
 unsigned resumedSamples=0;
 for(unsigned i=0;i<80;++i){if(!service.render(pcm,1024))return 46;for(auto v:pcm)resumedSamples+=v!=0;}
 if(!resumedSamples)return 47;
 std::printf("command pause/resume: active mask preserved, pause silent, resumed_nonzero=%u\n",resumedSamples);
 if(!service.execute(1,args)||service.activeMask())return 15;
 for(unsigned i=0;i<160;++i)if(!service.render(pcm,1024))return 16;
 for(auto v:pcm)if(v)return 17;
 if(!checkStatus()||status.activePlayers||status.enabledChannels)return 106;
 std::puts("shared status: original getter, player masks, hardware enables, locals/globals/ticks and untouched queue counter/padding PASS");
 // Repeated replacement is now legal, including while old notes are active.
 for(unsigned restart=0;restart<32;++restart){
  if(!service.execute(0,args)||!service.render(pcm,1024)||service.activeMask()!=8)return 23;
 }
 args[0]=5;if(!service.execute(0,args)||service.activeMask()!=40)return 24;
 if(!service.render(pcm,1024))return 25;
 args[0]=3;if(!service.execute(1,args)||service.activeMask()!=32)return 26;
 if(!service.render(pcm,1024)||service.activeMask()!=32)return 27;
 args[0]=5;if(!service.execute(1,args)||service.activeMask())return 28;
 for(unsigned i=0;i<160;++i)if(!service.render(pcm,1024))return 29;
 for(auto v:pcm)if(v)return 30;
 args[0]=3;
 // After draining, the same original start command can reuse the player slot.
 if(!service.execute(0,args)||service.activeMask()!=8)return 18;
 if(!service.execute(1,args))return 19;
 for(unsigned i=0;i<160;++i)if(!service.render(pcm,1024))return 20;
 service.unregisterResource(sequence.data);
 if(service.execute(0,args)||service.activeMask())return 21;
 args[0]=16;if(service.execute(1,args))return 22;
 std::printf("sound commands: title_hash=%08lx active/start/stop/reuse=PASS\n",(unsigned long)hash);
 }
 // Persistent variables outlive finished sequences and owner collection.
 {
  Sm64dsSoundCommands service;std::int16_t value=0,pcm[2048];std::uint32_t ticks=99;
  if(!service.ticks(3,&ticks)||ticks!=0)return 99;
  auto equals=[&](unsigned player,unsigned index,int expected){return service.variable(player,index,&value)&&value==expected;};
  if(!equals(3,0,-1)||!equals(3,31,-1))return 86;
  std::uintptr_t local[4]={3,0,0x1007b,0},global[4]={15,0x1002a,0,0};
  if(!service.execute(7,local)||!service.execute(8,global)||!equals(3,0,123)||!equals(5,0,-1)||!equals(5,31,42))return 87;
  unsigned char events[]={0xa1,0xb0,1,31,0xb1,31,1,0,0xb0,2,7,0,0xff};
  if(!service.registerResource({events,sizeof(events)})||!service.registerResource(bank))return 88;
  for(auto wave:waves)if(wave.data&&!service.registerResource(wave))return 89;
  std::uintptr_t start[4]={3,reinterpret_cast<std::uintptr_t>(events),0,reinterpret_cast<std::uintptr_t>(bank.data)};
  if(!service.execute(0,start)||!equals(3,0,-1)||!equals(5,31,42))return 90;
  if(!service.render(pcm,1024)||service.activeMask()||!equals(3,1,42)||!equals(3,2,7)||!equals(5,31,43))return 91;
  if(!service.ticks(3,&ticks)||ticks!=1)return 100;
  Sm64dsSoundStatus snapshot{};service.publishStatus(snapshot);
  const auto* packed=reinterpret_cast<const unsigned char*>(&snapshot);
  if(read16(packed+0x20+3*0x24+2)!=42||read16(packed+0x20+3*0x24+4)!=7||read16(packed+0x260+30)!=43||read32(packed+0x40+3*0x24)!=1)return 107;
  local[1]=15;local[2]=0x1fffe;
  if(!service.execute(7,local)||!equals(3,15,-2)||!service.render(pcm,1024)||!equals(3,15,-2))return 92;
  start[2]=sizeof(events);if(service.execute(0,start)||!equals(3,15,-2))return 93;
  if(!service.ticks(3,&ticks)||ticks!=1)return 101;
  start[2]=0;if(!service.execute(0,start)||!equals(3,15,-1)||!equals(3,31,43))return 94;
  if(!service.ticks(3,&ticks)||ticks!=0)return 102;
  local[2]=77;if(!service.execute(7,local)||!service.execute(1,start)||!service.render(pcm,1024)||!equals(3,15,77))return 95;
  local[0]=16;if(service.execute(7,local))return 96;
  local[0]=3;local[1]=16;if(service.execute(7,local))return 97;
  global[0]=16;if(service.execute(8,global)||service.variable(0,32,&value)||!equals(3,15,77))return 98;
  std::puts("commands7/8: signed16, idle writes, event sharing, finish/stop collection, restart reset and invalid input PASS");
 }
 // Exercise mixer lifetime directly: validation failure preserves an existing
 // player; released owners/resources survive replacement until their tail ends.
 {
  auto* mixer=sm64ds_mixer_open(32768);if(!mixer)return 31;
  Sm64dsSoundBuffer events{raw+read32(raw+24),sequence.size-read32(raw+24)};
  std::int16_t pcm[2048];
  if(!sm64ds_mixer_start(mixer,3,events,0,bank,waves)||!sm64ds_mixer_render(mixer,pcm,327))return 32;
  for(unsigned i=0;i<40;++i)if(!sm64ds_mixer_render(mixer,pcm,1024))return 33;
  const auto channels=sm64ds_mixer_channels(mixer);if(!channels)return 34;
  if(!sm64ds_mixer_track_channels(mixer,3,0xffff,0)||sm64ds_mixer_channels(mixer)!=channels)return 84;
  if(!sm64ds_mixer_track_channels(mixer,3,0xffff,0xffff))return 85;
  if(sm64ds_mixer_start(mixer,3,events,0,{bank.data,15},waves)||sm64ds_mixer_active(mixer)!=8||sm64ds_mixer_channels(mixer)!=channels)return 35;
  if(!sm64ds_mixer_start(mixer,3,events,0,bank,waves)||sm64ds_mixer_retired(mixer)!=1||sm64ds_mixer_channels(mixer)!=channels)return 36;
  if(!sm64ds_mixer_stop(mixer,3)||sm64ds_mixer_active(mixer))return 37;
  for(unsigned i=0;i<160;++i)if(!sm64ds_mixer_render(mixer,pcm,1024))return 38;
  if(sm64ds_mixer_retired(mixer)||sm64ds_mixer_channels(mixer))return 39;
  for(auto v:pcm)if(v)return 40;
  sm64ds_mixer_close(mixer);
 }
 std::printf("command mixer root_after=%u\n",root->VMemoryLeft());
 if(root->VMemoryLeft()!=rootBefore)return 41;
 std::free(bytes);
 std::puts("smoke_sound_commands: PASS shared commands0/1/2 and volume/tempo parameters, pause/resume, simultaneous players, 32 replacements, tail retention, invalid-resource isolation and root recovery");return 0;
}
