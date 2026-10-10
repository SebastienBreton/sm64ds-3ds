#include "ntr/mmio.h"
#include "unmatched/native_overlay.h"
#include "Heap.h"
#include "daOwl_c.h"
#include "ShadowModel.h"
#include <cstdio>
extern "C" {
void func_02059bc0();void __sinit_02075154();void __sinit_0207501c();
void __sinit_02073a24();void __sinit_02074da8();
void __sinit_ov094_021367e8();void CleanCommonModelDataArr();
int sm64ds_set_overlay_scope(int);unsigned sm64ds_destroy_overlay_resources(int);
daOwl_c* daOwl_c_classInit();
extern unsigned char data_ov094_02136b40[];
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();
 __sinit_02073a24();__sinit_02074da8();
 const auto before=root->VMemoryLeft();unsigned first=0;
 for(unsigned cycle=0;cycle<2;++cycle){
  LoadOverlay(94);
  auto* owl=daOwl_c_classInit();if(!owl)return 3;
  std::printf("owl resource cycle=%u size=%u\n",cycle,unsigned(sizeof(*owl)));std::fflush(stdout);
  owl->mPosX=100*4096;owl->mPosY=254*4096;owl->mPosZ=200*4096;
  if(owl->InitResources()!=1)return 4;
  auto* dormant=owl->mCurrentState;
  if(!dormant || owl->mRider || owl->mOpacity!=31)return 5;
  for(unsigned i=0;i<120;++i)if(owl->Behavior()!=1)return 6;
  if(owl->mCurrentState!=dormant || owl->mPosX!=100*4096 ||
     owl->mPosY!=254*4096 || owl->mPosZ!=200*4096)return 7;
  for(unsigned i=0;i<120;++i)owl->mModelAnim.Advance();
  if(owl->CleanupResources()!=1)return 8;delete owl;
  UnloadOverlay(94);
  if(sm64ds_destroy_overlay_resources(94)!=0)return 9;
  if(cycle==0)first=root->VMemoryLeft();else if(root->VMemoryLeft()!=first)return 10;
 }
 ShadowModel::CleanAll();CleanCommonModelDataArr();
 std::printf("owl scene heap before=%u after=%u\n",unsigned(before),unsigned(root->VMemoryLeft()));
 if(root->VMemoryLeft()!=before)return 11;
 std::puts("smoke_overlay94_actor: PASS resources, dormant behavior, animation and cleanup twice");return 0;
}
