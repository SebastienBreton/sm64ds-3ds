#include "ntr/mmio.h"
#include "Heap.h"
#include "audio/sound_native.h"
#include "audio/sound_commands.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
extern "C" {
void func_0204f070();
int func_02051918(unsigned,void*);
int func_0205179c(unsigned,int,void*);
unsigned* _ZN5Sound17InfoSequenceEntry9GetWithIDEj(unsigned);
unsigned* _ZN5Sound23InfoInstrumentBankEntry9GetWithIDEj(unsigned);
void* func_020509b0(unsigned);
void* func_0205b1d8(int);
void func_0205b1a4(void*);
int func_0205b070(int);
void** func_0205b274(int);
extern unsigned* data_020a7fc0;
void* _ZN6Memory8AllocateEj(unsigned);
void _ZN6Memory10DeallocateEPv(void*);
void* func_0205130c(unsigned,unsigned);
void func_02050f34(void*,int,int,int);
int func_0205117c(void*);
void* func_020511d4(void*,unsigned,void*,void*,unsigned);
void func_020510a4(void*,int);
void func_020512f0(void*);
int func_02052008(void*);
int FS_CloseFile(void*);
extern unsigned char data_0209b4b4[];
extern char data_0208e498[];
extern void* data_020a5bb8;
}
static unsigned callbacks;
static void discarded(void* p,int bytes,int tag,int value){
 if(bytes!=128 || tag!=0x1234 || value!=0x5678)callbacks=100;
 else {
  auto* b=static_cast<unsigned char*>(p);
  for(int i=0;i<bytes;++i)if(b[i]!=0xa5){callbacks=100;return;}
  ++callbacks;
 }
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 const unsigned initial=root->VMemoryLeft();func_0204f070();
 if(std::getenv("SOUND_CACHE_PLAY_PROBE")) {
  // The mixer is session-owned and allocated lazily, independently of SDAT
  // archive heaps. Establish that lifetime before measuring cache recovery.
  auto* node=static_cast<Sm64dsSoundNode*>(func_0205b1d8(1));if(!node)return 24;
  node->command=7;node->args[0]=3;node->args[1]=0;node->args[2]=0;node->args[3]=0;
  func_0205b1a4(node);if(!func_0205b070(1)||!func_0205b274(1))return 25;
  std::int16_t warm[1024];if(!sm64ds_native_sound_render(warm,512))return 26;
 }
 const unsigned before=root->VMemoryLeft();
 for(unsigned cycle=0;cycle<3;++cycle){
  auto* buffer=static_cast<unsigned char*>(_ZN6Memory8AllocateEj(0x100000));if(!buffer)return 3;
  auto* heap=func_0205130c(reinterpret_cast<unsigned>(buffer),0x100000);if(!heap)return 4;
  std::memset(data_0209b4b4,0,0x88);
  func_02050f34(data_0209b4b4,reinterpret_cast<int>(data_0208e498),reinterpret_cast<int>(heap),0);
  if(data_020a5bb8!=data_0209b4b4 || std::memcmp(data_0209b4b4,"SDAT",4))return 5;
  auto* info=*reinterpret_cast<unsigned char**>(data_0209b4b4+0x84);
  auto* fat=*reinterpret_cast<unsigned char**>(data_0209b4b4+0x7c);
  if(!info || !fat || std::memcmp(info,"INFO",4)||std::memcmp(fat,"FAT ",4))return 6;
  if(info<buffer || info>=buffer+0x100000 || fat<buffer || fat>=buffer+0x100000)return 7;
  if(!func_02052008(heap))return 8;
  for(unsigned iteration=0;iteration<8;++iteration){
   const int state=func_0205117c(heap);if(state<0)return 9;
   auto* scratch=func_020511d4(heap,128,(void*)discarded,(void*)0x1234,0x5678);if(!scratch)return 10;
   std::memset(scratch,0xa5,128);callbacks=0;
   func_020510a4(heap,state);
   if(callbacks!=1 || *reinterpret_cast<void**>(data_0209b4b4+0x84)!=info ||
      *reinterpret_cast<void**>(data_0209b4b4+0x7c)!=fat)return 11;
  }
  if(std::getenv("SOUND_GROUP_PROBE")) {
   const int group=std::atoi(std::getenv("SOUND_GROUP_PROBE"));
   std::printf("loading original sound group %d\n",group);std::fflush(stdout);
   if(!func_02051918(group,heap))return 16;
   unsigned loaded=0;for(unsigned i=0;i<*reinterpret_cast<unsigned*>(fat+8);++i)
    if(*reinterpret_cast<unsigned*>(fat+12+i*16+8))++loaded;
   std::printf("sound group %d loaded; FAT cached files=%u\n",group,loaded);std::fflush(stdout);
  }
  if(std::getenv("SOUND_CACHE_PLAY_PROBE")) {
   if(!func_0205179c(55,7,heap))return 17;
   auto* seqInfo=_ZN5Sound17InfoSequenceEntry9GetWithIDEj(55);
   auto* bankInfo=_ZN5Sound23InfoInstrumentBankEntry9GetWithIDEj(*reinterpret_cast<unsigned short*>(reinterpret_cast<char*>(seqInfo)+4));
   auto* seq=static_cast<unsigned char*>(func_020509b0(seqInfo[0]));
   void* bank=func_020509b0(bankInfo[0]);
   if(!seq || !bank)return 18;
   auto* node=static_cast<Sm64dsSoundNode*>(func_0205b1d8(1));if(!node)return 19;
   node->command=0;node->args[0]=3;
   node->args[1]=reinterpret_cast<unsigned>(seq+*reinterpret_cast<unsigned*>(seq+24));
   node->args[2]=0;node->args[3]=reinterpret_cast<unsigned>(bank);
   func_0205b1a4(node);if(!func_0205b070(1)||!func_0205b274(1))return 20;
   std::int16_t pcm[1024];unsigned nonzero=0,hash=2166136261u;
   for(unsigned block=0;block<64;++block){
    if(!sm64ds_native_sound_render(pcm,512))return 21;
    for(auto sample:pcm){nonzero+=sample!=0;hash=(hash^static_cast<unsigned short>(sample))*16777619u;}
   }
   if(!nonzero)return 22;
   std::printf("original SDAT cache title PCM nonzero=%u hash=%08x\n",nonzero,hash);
  }
  if(!FS_CloseFile(data_0209b4b4+0x30))return 12;
  func_020512f0(heap);
  if(std::getenv("SOUND_CACHE_PLAY_PROBE") && reinterpret_cast<Sm64dsSoundStatus*>(data_020a7fc0)->activePlayers)return 23;
  if(*reinterpret_cast<void**>(data_0209b4b4+0x84) || *reinterpret_cast<void**>(data_0209b4b4+0x7c))return 13;
  _ZN6Memory10DeallocateEPv(buffer);data_020a5bb8=nullptr;
  std::printf("sound archive cycle=%u INFO/FAT,32 players,8 save/restore states free=%u\n",cycle,unsigned(root->VMemoryLeft()));
  if(root->VMemoryLeft()!=before)return 14;
 }
 sm64ds_native_sound_shutdown();if(root->VMemoryLeft()!=initial)return 15;
 std::puts("smoke_sound_heap_archive: PASS original SDAT boot tables, sound heap rollback/callbacks, exact root recovery");return 0;
}
