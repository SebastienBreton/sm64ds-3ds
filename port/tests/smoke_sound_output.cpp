#include "audio/sound_commands.h"
#include "audio/sound_output_3ds.h"
#include "ntr/mmio.h"
#include <3ds.h>
extern "C" bool _ZN4Heap13SetupRootHeapEv();
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
static unsigned read32(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);}
static unsigned read16(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
static s16 captured[160*256*2];
int main(){
 if(!ntr::io_init())return 2;if(!_ZN4Heap13SetupRootHeapEv())return 3;
 char path[512];std::snprintf(path,sizeof(path),"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
 FILE* in=std::fopen(path,"rb");if(!in)return 4;std::fseek(in,0,SEEK_END);unsigned length=std::ftell(in);std::rewind(in);
 auto* bytes=static_cast<unsigned char*>(std::malloc(length));if(!bytes)return 5;
 if(std::fread(bytes,1,length,in)!=length)return 6;std::fclose(in);
 auto* info=bytes+read32(bytes+24);auto* fat=bytes+read32(bytes+32);
 auto entry=[&](unsigned type,unsigned id){auto* table=info+read32(info+8+type*4);return info+read32(table+4+id*4);};
 auto file=[&](unsigned id){auto* f=fat+12+id*16;return Sm64dsSoundBuffer{bytes+read32(f),read32(f+4)};};
 auto* meta=entry(0,55);auto sequence=file(read32(meta));auto* raw=static_cast<const unsigned char*>(sequence.data);
 auto* bankMeta=entry(2,read16(meta+4));auto bank=file(read32(bankMeta));Sm64dsSoundBuffer waves[4]{};
 {
 Sm64dsSoundCommands service;
 if(!service.registerResource(sequence)||!service.registerResource(bank))return 7;
 for(unsigned i=0;i<4;++i){
  unsigned id=read16(bankMeta+4+i*2);uint32_t address=0;
  if(id!=0xffff){waves[i]=file(read32(entry(3,id)));if(!service.registerResource(waves[i]))return 8;address=reinterpret_cast<std::uintptr_t>(waves[i].data);}
  std::memcpy(static_cast<unsigned char*>(const_cast<void*>(bank.data))+0x18+i*8,&address,4);
 }
 std::uintptr_t args[4]={3,reinterpret_cast<std::uintptr_t>(raw+read32(raw+24)),0,reinterpret_cast<std::uintptr_t>(bank.data)};
 Sm64dsSoundOutput output;
 for(unsigned pass=0;pass<2;++pass){
  if(!output.open(service)){std::printf("output open: %s\n",output.error());return 9;}
  std::memset(captured,0,sizeof(captured));
  ndspWaveBuf capture{};capture.data_pcm16=captured;capture.nsamples=160*256;ndspSetCapture(&capture);
  args[0]=3;if(!service.execute(0,args))return 10;
  const u64 start=svcGetSystemTick();
  for(unsigned frame=0;frame<120;++frame){
   if(frame==30){args[0]=5;if(!service.execute(0,args)||service.activeMask()!=40)return 11;}
   if(frame==60){args[0]=3;if(!service.execute(1,args)||service.activeMask()!=32)return 12;}
   if(frame==90){args[0]=5;if(!service.execute(0,args)||service.activeMask()!=32)return 13;}
   if(!output.pump()){std::printf("output pump: %s\n",output.error());return 14;}
   svcSleepThread(16666667);
  }
  const double seconds=double(svcGetSystemTick()-start)/SYSCLOCK_ARM11;
  args[0]=5;if(!service.execute(1,args)||service.activeMask())return 15;
  if(!output.pump())return 16;
  const unsigned submitted=output.submittedBuffers(),completed=output.completedBuffers();
  // Close while queued: the worker must no longer access PCM/descriptors.
  output.close();output.close();
  unsigned nonzero=0,peak=0;
  for(auto v:captured){nonzero+=v!=0;unsigned mag=v<0?-int(v):int(v);if(mag>peak)peak=mag;}
  std::printf("sound output pass=%u submitted=%u completed=%u seconds=%.3f captured_nonzero=%u peak=%u\n",pass,submitted,completed,seconds,nonzero,peak);
  if(submitted<100||completed<97||submitted-completed>3||seconds<1.5||seconds>5||nonzero<1000||peak<100)return 17;
  char capturePath[64];std::snprintf(capturePath,sizeof(capturePath),"sound-output-mixed-%u.pcm",pass);
  FILE* captureFile=std::fopen(capturePath,"wb");if(!captureFile)return 18;
  if(std::fwrite(captured,1,sizeof(captured),captureFile)!=sizeof(captured))return 19;std::fclose(captureFile);
 }
 }
 // libctru may retain process-wide state; synthesis lifetime itself is covered
 // by smoke_sound_commands, whose root recovery is exact.
 std::free(bytes);
 std::puts("smoke_sound_output: PASS frame-pumped commands, simultaneous/replaced players, DSP capture, queued close and reopen");return 0;
}
