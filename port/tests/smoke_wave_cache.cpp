#include "audio/sseq_decoder.h"
#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
static unsigned read32(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);}
static unsigned read16(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
#define CHECK(c) do {if(!(c)){printf("wave cache FAIL line %d: %s (%s)\n",__LINE__,#c,sm64ds_sseq_error());return 1;}}while(0)
int main(){
 CHECK(ntr::io_init());auto* root=Heap::SetupRootHeap();CHECK(root);
 char path[512];std::snprintf(path,sizeof(path),"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
 FILE* in=std::fopen(path,"rb");CHECK(in);std::fseek(in,0,SEEK_END);unsigned length=std::ftell(in);std::rewind(in);
 auto* bytes=static_cast<unsigned char*>(std::malloc(length));CHECK(bytes);
 CHECK(std::fread(bytes,1,length,in)==length);std::fclose(in);
 auto* info=bytes+read32(bytes+24);auto* fat=bytes+read32(bytes+32);
 auto entry=[&](unsigned type,unsigned id){auto* table=info+read32(info+8+type*4);return info+read32(table+4+id*4);};
 auto file=[&](unsigned id){auto* f=fat+12+id*16;return Sm64dsSoundBuffer{bytes+read32(f),read32(f+4)};};
 auto* meta=entry(0,55);auto* bankMeta=entry(2,read16(meta+4));auto bank=file(read32(bankMeta));Sm64dsSoundBuffer waves[4]{};
 for(unsigned i=0;i<4;++i){unsigned id=read16(bankMeta+4+i*2);if(id!=0xffff)waves[i]=file(read32(entry(3,id)));}
 // One waiting track per owner isolates resource ownership from channel stealing.
 const unsigned char wait[]={0x80,0x7f,0x94,0,0,0};
 Sm64dsSoundBuffer events{wait,sizeof(wait)};
 printf("wave cache root baseline=%u\n",root->VMemoryLeft());
 const unsigned baseline=root->VMemoryLeft();
 for(unsigned pass=0;pass<3;++pass){
  auto* mixer=sm64ds_mixer_open(32768);CHECK(mixer);
  const unsigned before=root->VMemoryLeft();
  CHECK(sm64ds_mixer_start(mixer,0,events,0,bank,waves));
  const unsigned first=before-root->VMemoryLeft();
  for(unsigned id=1;id<8;++id){
   const unsigned previous=root->VMemoryLeft();
   CHECK(sm64ds_mixer_start(mixer,id,events,0,bank,waves));
   const unsigned added=previous-root->VMemoryLeft();
   printf("wave cache pass=%u owner=%u initial=%u added=%u\n",pass,id,first,added);
   CHECK(added<first/2);
  }
  // Native range invalidation must prevent sharing a stale decoded resource,
  // even while an older owner keeps its snapshot alive.
  for(auto wave:waves)if(wave.data){auto p=reinterpret_cast<std::uintptr_t>(wave.data);CHECK(sm64ds_mixer_invalidate(mixer,0x1d,p,p+wave.size-1));}
  const unsigned previous=root->VMemoryLeft();
  CHECK(sm64ds_mixer_start(mixer,8,events,0,bank,waves));
  const unsigned replaced=previous-root->VMemoryLeft();
  printf("wave cache invalidation: new allocation=%u initial=%u\n",replaced,first);
  CHECK(replaced>first/2);
  sm64ds_mixer_close(mixer);CHECK(root->VMemoryLeft()==baseline);
 }
 std::free(bytes);
 puts("smoke_wave_cache: PASS 8 simultaneous owners, native invalidation, complete heap recovery across 3 cycles");return 0;
}
