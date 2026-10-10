#include "ntr/mmio.h"
#include "Heap.h"
#include "daObjKey_c.h"
#include "ShadowModel.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
extern "C" {
void func_02059bc0();void __sinit_02075154();void __sinit_0207501c();
void __sinit_02073a24();void __sinit_02074da8();
void __sinit_ov002_02100560();void __sinit_ov002_02107298();void __sinit_ov002_021019d0();
void __sinit_ov002_02106e40();
void __sinit_ov089_021328d4();void CleanCommonModelDataArr();
int sm64ds_set_overlay_scope(int);unsigned sm64ds_destroy_overlay_resources(int);
daObjKey_c* daObjKey_c_classInit_OBJ_KEY();daObjKey_c* daObjKey_c_classInit_LAST_STAR();
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();
 __sinit_02073a24();__sinit_02074da8();
 auto previous=sm64ds_set_overlay_scope(2);
 __sinit_ov002_02100560();__sinit_ov002_02107298();__sinit_ov002_021019d0();
 __sinit_ov002_02106e40();
 sm64ds_set_overlay_scope(previous);
 LoadOverlay(85); // Kind3 uses the rabbit-key shared model from this overlay.
 const auto before=root->VMemoryLeft();unsigned first=0;
 for(unsigned cycle=0;cycle<2;++cycle){
  LoadOverlay(89);
  for(unsigned kind=0;kind<8;++kind){
   std::printf("key resource cycle=%u kind=%u\n",cycle,kind);std::fflush(stdout);
   auto* key=kind==7?daObjKey_c_classInit_LAST_STAR():daObjKey_c_classInit_OBJ_KEY();
   if(!key)return 3;key->param1=kind;key->actorID=kind==7?283:282;
   if(key->InitResources()!=1)return 4;
   if(key->mState!=kind)return 5;
   for(unsigned i=0;i<120;++i)key->mModelAnim.Advance();
   if(key->CleanupResources()!=1)return 6;
   delete key;
  }
  UnloadOverlay(89);
  if(sm64ds_destroy_overlay_resources(89)!=0)return 7;
  if(cycle==0)first=root->VMemoryLeft();else if(root->VMemoryLeft()!=first)return 8;
 }
 ShadowModel::CleanAll();CleanCommonModelDataArr();
 std::printf("key scene heap before=%u after=%u\n",unsigned(before),unsigned(root->VMemoryLeft()));
 if(root->VMemoryLeft()!=before)return 9;
 UnloadOverlay(85);sm64ds_destroy_overlay_resources(2);
 std::puts("smoke_overlay89_actors: PASS eight resource variants and animations twice");return 0;
}
