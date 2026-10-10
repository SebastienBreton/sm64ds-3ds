#include "audio/sseq_decoder.h"
#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
static unsigned read32(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);}
static unsigned read16(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
static uint32_t render(Sm64dsSequence* seq){
 std::int16_t pcm[2048];uint32_t hash=2166136261u;
 for(unsigned i=0;i<160;++i){if(!sm64ds_sseq_render(seq,pcm,1024))return 0;for(auto b:reinterpret_cast<const unsigned char(&)[4096]>(pcm)){hash^=b;hash*=16777619u;}}
 return hash;
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 3;
 char path[512];std::snprintf(path,sizeof(path),"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
 for(unsigned mode=0;mode<2;++mode){
  auto* reference=mode?sm64ds_ssar_open(path,4,0,32768):sm64ds_sseq_open(path,55,32768);if(!reference)return 4;
  const auto expected=render(reference);sm64ds_sseq_close(reference);
  FILE* in=std::fopen(path,"rb");if(!in)return 5;std::fseek(in,0,SEEK_END);const unsigned length=std::ftell(in);std::rewind(in);
  auto* bytes=static_cast<unsigned char*>(std::malloc(length));if(!bytes)return 6;
  if(std::fread(bytes,1,length,in)!=length)return 7;std::fclose(in);
  auto* info=bytes+read32(bytes+24);auto* fat=bytes+read32(bytes+32);
  auto entry=[&](unsigned type,unsigned id){auto* table=info+read32(info+8+type*4);return info+read32(table+4+id*4);};
  auto file=[&](unsigned id){auto* f=fat+12+id*16;return Sm64dsSoundBuffer{bytes+read32(f),read32(f+4)};};
  auto* meta=entry(mode?1:0,mode?4:55);auto sequence=file(read32(meta));auto* raw=static_cast<const unsigned char*>(sequence.data);
  unsigned dataOffset=read32(raw+24),offset=0,bankID,volume;
  if(mode){auto* e=raw+32;offset=read32(e);bankID=read16(e+4);volume=e[6];}
  else {bankID=read16(meta+4);volume=meta[6];}
  Sm64dsSoundBuffer events{raw+dataOffset,read32(raw+8)-dataOffset};
  auto* bankMeta=entry(2,bankID);auto bank=file(read32(bankMeta));Sm64dsSoundBuffer waves[4]{};
  for(unsigned i=0;i<4;++i){unsigned id=read16(bankMeta+4+i*2);if(id!=0xffff)waves[i]=file(read32(entry(3,id)));}
  // The game's bank loader stores binding links in the reserved header region.
  std::memset(static_cast<unsigned char*>(const_cast<void*>(bank.data))+24,0xa5,32);
  const unsigned before=root->VMemoryLeft();
  auto bad=bank;bad.size=15;
  if(sm64ds_sseq_open_data(events,offset,bad,waves,32768,volume) || root->VMemoryLeft()!=before)return 8;
  if(sm64ds_sseq_open_data(events,events.size,bank,waves,32768,volume) || root->VMemoryLeft()!=before)return 9;
  unsigned char shortBank[16];std::memcpy(shortBank,bank.data,16);shortBank[8]=16;shortBank[9]=shortBank[10]=shortBank[11]=0;
  if(sm64ds_sseq_open_data(events,offset,{shortBank,16},waves,32768,volume) || root->VMemoryLeft()!=before)return 12;
  auto* decoded=sm64ds_sseq_open_data(events,offset,bank,waves,32768,volume);
  if(!decoded){std::printf("loaded: %s\n",sm64ds_sseq_error());return 10;}
  std::memset(bytes,0xdd,length);std::free(bytes);
  const auto actual=render(decoded);sm64ds_sseq_close(decoded);
  std::printf("loaded mode=%u expected=%08lx actual=%08lx root_before=%u root_after=%u\n",mode,(unsigned long)expected,(unsigned long)actual,before,root->VMemoryLeft());
  if(actual!=expected||!actual||root->VMemoryLeft()!=before)return 11;
 }
 std::puts("smoke_sseq_loaded: PASS loaded SSEQ/SSAR snapshots, released source buffers, invalid spans and allocation recovery");return 0;
}
