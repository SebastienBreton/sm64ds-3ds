#include "ntr/mmio.h"
#include "Heap.h"
#include "audio/sound_status.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
// Run original memory initialization; transport/queue entry points are observed
// here because IPC integration is not implemented by this storage fixture.
#define func_0205b358 storageInitialize
#define func_0205b554 storageInitializeStatus
#define func_0205ae64 storageReady
#define func_0205b1d8 storageAllocate
#define func_0205b1a4 storageAppend
#define func_0205b070 storageSubmit
extern "C" {
#include "src/func_0205b554.c"
#include "src/func_0205b358.c"
}
#undef func_0205b358
#undef func_0205b554
#undef func_0205ae64
#undef func_0205b1d8
#undef func_0205b1a4
#undef func_0205b070
extern "C" unsigned char storageStatusBytes[0x280] asm("data_020a64e0");
static unsigned readyCalls,allocateCalls,appendCalls,submitCalls;
static void* submitted;
extern "C" void storageReady(){++readyCalls;}
extern "C" void* storageAllocate(int flag){
 ++allocateCalls;if(flag!=1)return nullptr;
 auto* node=data_020a6484;if(node)data_020a6484=node->next;
 return node;
}
extern "C" void storageAppend(Node5b1a4* node){++appendCalls;submitted=node;}
extern "C" void storageSubmit(int flag){if(flag==1)++submitCalls;}
int main(){
 if(!ntr::io_init()||!Heap::SetupRootHeap())return 2;
 const auto base=reinterpret_cast<std::uintptr_t>(data_020a6760);
 if(reinterpret_cast<std::uintptr_t>(data_020a7760)-base!=0x1000||reinterpret_cast<std::uintptr_t>(&data_020a7f48)-base!=0x17e8)return 3;
 if(base-reinterpret_cast<std::uintptr_t>(&data_020a64e0)!=0x280)return 4;
 for(unsigned iteration=0;iteration<2;++iteration){
  std::memset(data_020a6760,0xa5,0x1800);
  std::memset(storageStatusBytes,0xa5,sizeof(storageStatusBytes));
  storageInitialize();
  if(data_020a7fc0!=&data_020a64e0||data_020a6494!=&data_020a6760[255]||data_020a6484!=&data_020a6760[1])return 5;
  unsigned free=0;for(auto* node=data_020a6484;node;node=node->next){
   if(free>=255||node!=&data_020a6760[free+1])return 6;++free;
  }
  if(free!=255||data_020a6760[255].next)return 7;
  auto* status=reinterpret_cast<Sm64dsSoundStatus*>(&data_020a64e0);
  if(status->completedBatches||status->activePlayers||status->enabledChannels||status->activeCaptures)return 8;
  for(auto byte:status->reserved)if(byte!=0xa5)return 9;
  for(const auto& player:status->players){if(player.ticks)return 10;for(auto v:player.variables)if(v!=-1)return 11;}
  for(auto v:status->globals)if(v!=-1)return 12;
  if(submitted!=data_020a6760||data_020a6760[0].pad[0]!=0x19||unsigned(data_020a6760[0].pad[1])!=reinterpret_cast<std::uintptr_t>(status))return 13;
  if(data_020a648c||data_020a6490||data_020a64a0||data_020a6498||data_020a649c||data_020a6488||data_020a64a4!=1)return 14;
 }
 if(readyCalls!=2||allocateCalls!=2||appendCalls!=2||submitCalls!=2)return 15;
 std::puts("smoke_sound_storage: PASS original init twice, contiguous256-node pool,255 free nodes, exact tail/status aliases,640-byte status, command19 submission observed");return 0;
}
