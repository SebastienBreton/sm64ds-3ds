#include "ntr/mmio.h"
#include "Heap.h"
#include "daStarGate_c.h"
#include "ShadowModel.h"
#include <cstdio>
#include <cstring>
extern "C" {
void func_02059bc0();void __sinit_02075154();void __sinit_0207501c();
void __sinit_02073a24();void __sinit_02074da8();void CleanCommonModelDataArr();
daStarGate_c* daStarGate_c_classInit();
void sm64ds_init_star_gate();int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
extern unsigned char data_ov100_02148934[];
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();__sinit_02073a24();__sinit_02074da8();
 auto before=root->VMemoryLeft();
 using Gate=daStarGate_c;using State=Gate::State;
 State* states[]={&Gate::ST_WAIT,&Gate::ST_TALKING_TO_PLAYER,&Gate::ST_UNLOCKING,&Gate::ST_OPEN_CLOSE,&Gate::ST_STAY_CLOSED};
 const State expected[]={{nullptr,&Gate::St_Wait_Main},
  {&Gate::St_TalkingToPlayer_Init,&Gate::St_TalkingToPlayer_Main},
  {&Gate::St_Unlocking_Init,&Gate::St_Unlocking_Main},
  {&Gate::St_OpenClose_Init,&Gate::St_OpenClose_Main},{nullptr,&Gate::St_StayClosed_Main}};
 // The handle must not be initialized by the CRT before overlay ownership.
 if(*reinterpret_cast<unsigned short*>(data_ov100_02148934)!=0)return 3;
 for(unsigned i=0;i<3;++i){
  auto old=sm64ds_set_overlay_scope(100);sm64ds_init_star_gate();sm64ds_set_overlay_scope(old);
  if(*reinterpret_cast<unsigned short*>(data_ov100_02148934)!=755)return 4;
  for(unsigned j=0;j<5;++j)if(states[j]->init!=expected[j].init || states[j]->main!=expected[j].main)return 7;
  for(unsigned kind=0;kind<4;++kind){
   auto* gate=daStarGate_c_classInit();if(!gate)return 8;
   gate->param1=kind<<16;gate->mPosX=100*4096;gate->mPosY=254*4096;gate->mPosZ=200*4096;
   if(gate->InitResources()!=1 || gate->param1!=kind || gate->mState!=&Gate::ST_WAIT)return 9;
   if(gate->mModel.mat4x3.t.x!=gate->mPosX/8 || gate->mModel.mat4x3.t.y!=gate->mPosY/8 || gate->mModel.mat4x3.t.z!=gate->mPosZ/8)return 10;
   if(gate->CleanupResources()!=1)return 11;delete gate;
   CleanCommonModelDataArr();
  }
  if(sm64ds_destroy_overlay_resources(100)!=1 || sm64ds_destroy_overlay_resources(100)!=0)return 5;
  if(root->VMemoryLeft()!=before)return 6;
  for(auto* state:states)std::memset(state,0xa5,sizeof(*state));
  std::printf("star gate initializer cycle=%u handle=1185 file=755 callbacks=1 variants=4\n",i);
 }
 std::puts("smoke_star_gate_init: PASS explicit overlay-owned initialization");return 0;
}
