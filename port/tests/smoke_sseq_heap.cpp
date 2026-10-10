#include "audio/sseq_decoder.h"
#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {extern void* data_020a0ea0;}
int main(){
 if(!ntr::io_init())return 2;
 auto* root=Heap::SetupRootHeap();if(!root)return 3;
 char path[512];std::snprintf(path,sizeof(path),"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
 // Prime process-wide standard-library state before checking allocation recovery.
 std::printf("heap root=%p initial_free=%u\n",root,root->VMemoryLeft());
 auto* warm=sm64ds_sseq_open(path,55,32768);if(!warm)return 4;sm64ds_sseq_close(warm);
 const unsigned initial=root->VMemoryLeft();
 for(unsigned pass=0;pass<2;++pass){
  auto* scene=Heap::CreateExpandingHeap(256*1024,root,4);if(!scene)return 5;
  data_020a0ea0=scene;const unsigned sceneFree=scene->VMemoryLeft();
  std::printf("heap pass=%u scene=%p root_free=%u opening\n",pass,scene,root->VMemoryLeft());
  auto* seq=sm64ds_sseq_open(path,55,32768);
  if(!seq){std::printf("open: %s\n",sm64ds_sseq_error());return 6;}
  if(data_020a0ea0!=scene || scene->VMemoryLeft()!=sceneFree)return 7;
  std::printf("heap opened root_free=%u invalid-open next\n",root->VMemoryLeft());
  auto* invalid=sm64ds_sseq_open(path,999,32768);
  if(invalid || data_020a0ea0!=scene || scene->VMemoryLeft()!=sceneFree)return 8;
  std::int16_t pcm[2048];uint32_t hash=2166136261u;
  for(unsigned chunk=0;chunk<160;++chunk){
   if(chunk==1){
    data_020a0ea0=root;scene->Destroy();
    scene=Heap::CreateExpandingHeap(256*1024,root,4);if(!scene)return 9;
    void* fill=scene->Allocate(200*1024,4);if(!fill)return 10;
    std::memset(fill,0xa5,200*1024);scene->Deallocate(fill);
    data_020a0ea0=scene;
   }
   const auto before=scene->VMemoryLeft();
   if(!sm64ds_sseq_render(seq,pcm,1024))return 11;
   if(data_020a0ea0!=scene || before!=scene->VMemoryLeft())return 12;
   for(auto b:reinterpret_cast<const unsigned char(&)[4096]>(pcm)){hash^=b;hash*=16777619u;}
  }
  const auto before=scene->VMemoryLeft();sm64ds_sseq_close(seq);
  if(data_020a0ea0!=scene || before!=scene->VMemoryLeft())return 13;
  data_020a0ea0=root;scene->Destroy();
  std::printf("audio heap pass=%u hash=%08lx root_before=%u root_after=%u\n",pass,(unsigned long)hash,initial,root->VMemoryLeft());
  if(hash!=0x380174bd || root->VMemoryLeft()!=initial)return 14;
 }
 std::puts("smoke_sseq_heap: PASS scene destruction/reuse, error restoration, PCM and root allocation recovery");return 0;
}
