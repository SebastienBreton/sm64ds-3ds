#include "ntr/mmio.h"
#include "Heap.h"
#include "audio/sound_commands.h"
#include <cstdio>
#include <cstring>
extern "C" {
void** func_0205b274(int);
extern unsigned int* data_020a7fc0;
extern int data_020a6488,data_020a6498,data_020a64a0;
extern void** data_020a64a8[9];
extern void** data_020a6494;
extern void** data_020a6484;
extern Sm64dsSoundNode data_020a6760[256];
}
static std::uint32_t address(const void* p){return reinterpret_cast<std::uintptr_t>(p);}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 3;
 const auto before=root->VMemoryLeft();
 {
 Sm64dsSoundCommands service;Sm64dsSoundStatus status{};
 auto* previous=data_020a7fc0;data_020a7fc0=reinterpret_cast<unsigned int*>(&status);
 auto* nodes=data_020a6760;std::memset(nodes,0,256*sizeof(*nodes));
 Sm64dsSoundQueue queue(service,nodes,256,status);
 if(queue.refresh()||queue.submit(0))return 4;
 nodes[0]={address(nodes+1),0x19,{address(&status),0,0,0}};
 nodes[1]={0,7,{3,15,0x1fffe,0}};
 for(unsigned i=2;i<255;++i)nodes[i].next=address(nodes+i+1);
 data_020a6484=reinterpret_cast<void**>(nodes+2);data_020a6494=reinterpret_cast<void**>(nodes+255);
 data_020a6488=0;data_020a6498=0;data_020a64a0=1;data_020a64a8[0]=reinterpret_cast<void**>(nodes);
 if(!queue.submit(address(nodes))||status.completedBatches!=1||status.players[3].variables[15]!=-2)return 5;
 if(nodes[0].next!=address(nodes+1)||nodes[1].next)return 6;
 if(func_0205b274(0)!=reinterpret_cast<void**>(nodes)||data_020a6488!=1||data_020a64a0||data_020a6498!=1)return 7;
 if(data_020a6484!=reinterpret_cast<void**>(nodes+2)||data_020a6494!=reinterpret_cast<void**>(nodes+1)||nodes[255].next!=address(nodes))return 8;
 if(func_0205b274(0))return 9;
 // Remove the next two free nodes and exercise the ring's8->0 wrap.
 data_020a6484=reinterpret_cast<void**>(nodes+4);
 nodes[2]={address(nodes+3),8,{15,42,0,0}};nodes[3]={0,7,{3,0,7,0}};
 data_020a6498=8;data_020a64a0=1;data_020a64a8[8]=reinterpret_cast<void**>(nodes+2);
 if(!queue.submit(address(nodes+2))||status.completedBatches!=2||status.globals[15]!=42||status.players[3].variables[0]!=7)return 10;
 if(func_0205b274(0)!=reinterpret_cast<void**>(nodes+2)||data_020a6498||data_020a6488!=2||data_020a64a0)return 11;
 if(nodes[1].next!=address(nodes+2)||data_020a6494!=reinterpret_cast<void**>(nodes+3))return 12;
 if(!queue.refresh()||status.completedBatches!=2)return 13;
 // Invalid topology/opcodes are rejected before any command changes state.
 nodes[0]={address(nodes),7,{3,0,999,0}};
 if(queue.submit(address(nodes))||status.completedBatches!=2)return 14;
 nodes[0].next=address(nodes+1);nodes[1]={0,9,{0,0,0,0}};
 if(queue.submit(address(nodes)))return 15;
 std::int16_t value;if(!service.variable(3,0,&value)||value!=7)return 16;
 if(queue.submit(address(nodes)+1)||queue.submit(address(nodes+256)))return 17;
 // A full pool is a legal batch, with command order visible in the variables.
 for(unsigned i=0;i<256;++i)nodes[i]={i==255?0:address(nodes+i+1),7,{0,i%16,i,0}};
 if(!queue.submit(address(nodes))||status.completedBatches!=3)return 18;
 for(unsigned i=0;i<16;++i)if(status.players[0].variables[i]!=240+i)return 19;
 status.completedBatches=0xffffffff;nodes[0].next=0;
 if(!queue.submit(address(nodes))||status.completedBatches)return 20;
 // Execution failure is latched: a partially processed list is never replayed.
 nodes[0]={address(nodes+1),7,{3,0,999,0}};nodes[1]={0,7,{16,0,1,0}};
 if(queue.submit(address(nodes))||status.completedBatches)return 21;
 if(!service.variable(3,0,&value)||value!=999)return 22;
 nodes[0].args[2]=888;nodes[1].args[0]=3;
 if(queue.submit(address(nodes))||queue.refresh()||status.completedBatches)return 23;
 if(!service.variable(3,0,&value)||value!=999)return 24;
 data_020a7fc0=previous;
 }
 if(root->VMemoryLeft()!=before)return 25;
 std::puts("smoke_sound_queue: PASS command19 bind, ordered batches, original recycling and ring wrap,256-node batch, counter wrap, malformed rejection, failure latch and root recovery");return 0;
}
