#include "ntr/mmio.h"
#include "Heap.h"
#include "audio/sound_native.h"
#include "audio/sound_commands.h"
#include <cstdio>
#include <cstdint>
extern "C" {
void func_0205b358();
void* func_0205b1d8(int);
void func_0205b1a4(void*);
int func_0205b070(int);
void** func_0205b274(int);
unsigned func_0205b608();
int IPCSend(unsigned,unsigned,unsigned);
extern unsigned int* data_020a7fc0;
extern int data_020a6488,data_020a64a0;
extern void* data_020a6484;
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 3;
 const auto before=root->VMemoryLeft();
 for(unsigned cycle=0;cycle<2;++cycle){
  func_0205b358();
  auto* status=reinterpret_cast<Sm64dsSoundStatus*>(data_020a7fc0);
  if(!status||status->completedBatches!=1||func_0205b608()||data_020a64a0!=1)return 4;
  if(!func_0205b274(0)||data_020a6488!=1||data_020a64a0)return 5;
  for(unsigned batch=0;batch<20;++batch){
   for(unsigned index=0;index<16;++index){
    auto* node=static_cast<Sm64dsSoundNode*>(func_0205b1d8(1));if(!node)return 6;
    node->command=7;node->args[0]=3;node->args[1]=index;node->args[2]=batch*16+index;node->args[3]=0;
    func_0205b1a4(node);
   }
   if(!func_0205b070(1)||status->completedBatches!=batch+2)return 8;
   for(unsigned index=0;index<16;++index)if(status->players[3].variables[index]!=batch*16+index)return 9;
   if(!func_0205b274(1)||data_020a64a0||data_020a6488!=int(batch+2))return 10;
  }
  std::int16_t pcm[128];if(!sm64ds_native_sound_render(pcm,64))return 11;
  for(auto sample:pcm)if(sample)return 12;
  auto completed=status->completedBatches;
  if(IPCSend(7,0,0)!=0||status->completedBatches!=completed||func_0205b608())return 13;
  unsigned free=0;auto* node=static_cast<Sm64dsSoundNode*>(data_020a6484);
  while(node){if(++free>256)return 14;node=reinterpret_cast<Sm64dsSoundNode*>(node->next);}
  if(free!=256)return 15;
  sm64ds_native_sound_shutdown();if(data_020a7fc0||root->VMemoryLeft()!=before)return 16;
 }
 std::puts("smoke_sound_native: PASS original init/allocate/submit/recycle, full native pointers,40 batches, ring wraps,256 nodes recovered, wakeup and lifecycle/root recovery");return 0;
}
