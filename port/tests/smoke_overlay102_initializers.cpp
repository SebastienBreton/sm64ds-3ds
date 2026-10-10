#include "ntr/mmio.h"
#include "Heap.h"
#include "daShl_c.h"
#include "daObjSimpleBg_c.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
#include <cstring>
extern "C" {
void func_02059bc0();
void CleanCommonModelDataArr();
void __sinit_ov002_02100560();void __sinit_ov002_02107298();void __sinit_ov002_021019d0();
daShl_c* daShl_c_classInit();
daObjSimpleBg_c* daObjSimpleBg_c_classInit_DOKAN();
daObjSimpleBg_c* daObjSimpleBg_c_classInit_MC_DODAI();
daObjSimpleBg_c* daObjSimpleBg_c_classInit_MC_HAZAD();void __sinit_02075154();void __sinit_0207501c();
void __sinit_02073a24();void __sinit_02074da8();
void __sinit_ov102_0214d714();void __sinit_ov102_0214d908();
void __sinit_ov102_0214de70();void __sinit_ov102_0214dfac();
extern unsigned char sm64ds_overlay102_bss_begin[],sm64ds_overlay102_bss_end[];
int sm64ds_set_overlay_scope(int);unsigned sm64ds_destroy_overlay_resources(int);
extern unsigned char data_ov102_0214ea48[],data_ov102_0214ea58[],data_ov102_0214ea68[],data_ov102_0214ea78[];
extern unsigned char data_ov102_0214e5d4[],data_ov102_0214e5dc[],data_ov102_0214e5e4[],data_ov102_0214e5ec[];
extern unsigned char data_ov102_0214e5f4[],data_ov102_0214e5fc[],data_ov102_0214e604[],data_ov102_0214e60c[];
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();__sinit_02073a24();__sinit_02074da8();
 const int commonScope=sm64ds_set_overlay_scope(2);
 __sinit_ov002_02100560();__sinit_ov002_02107298();__sinit_ov002_021019d0();
 sm64ds_set_overlay_scope(commonScope);
 LoadOverlay(9);
 const auto before=root->VMemoryLeft();unsigned char pristine[960];
 if(sm64ds_overlay102_bss_end-sm64ds_overlay102_bss_begin!=960)return 3;
 unsigned char* states[]={data_ov102_0214ea48,data_ov102_0214ea58,data_ov102_0214ea68,data_ov102_0214ea78};
 unsigned char* expected[]={data_ov102_0214e5fc,data_ov102_0214e5dc,data_ov102_0214e5ec,data_ov102_0214e5d4,
                           data_ov102_0214e5e4,data_ov102_0214e5f4,data_ov102_0214e604,data_ov102_0214e60c};
 for(unsigned cycle=0;cycle<3;++cycle){
  std::memset(sm64ds_overlay102_bss_begin,0,960);
  const int prev=sm64ds_set_overlay_scope(102);
  __sinit_ov102_0214d714();__sinit_ov102_0214d908();__sinit_ov102_0214de70();__sinit_ov102_0214dfac();
  sm64ds_set_overlay_scope(prev);
  if(cycle==0)std::memcpy(pristine,sm64ds_overlay102_bss_begin,960);
  else if(std::memcmp(pristine,sm64ds_overlay102_bss_begin,960))return 4;
  for(unsigned i=0;i<4;++i){
   if(std::memcmp(states[i],expected[2*i],8)||std::memcmp(states[i]+8,expected[2*i+1],8))return 5;
  }
  for(unsigned variant=0;variant<4;++variant){
   auto* shell=daShl_c_classInit();if(!shell)return 8;
   shell->actorID=285;shell->param1=(variant&1)|((variant>>1)<<4);
   shell->mPosX=100*4096;shell->mPosY=254*4096;shell->mPosZ=-200*4096;
   std::printf("shell resource cycle=%u variant=%u\n",cycle,variant);std::fflush(stdout);
   if(shell->InitResources()!=1)return 9;
   if(shell->mModelIndex!=(variant&1)||shell->mVariant!=(variant>>1)||shell->mState!=data_ov102_0214ea68)return 10;
   if(shell->mSpawnPosX!=shell->mPosX||shell->mSpawnPosY!=shell->mPosY||shell->mSpawnPosZ!=shell->mPosZ)return 11;
   if(shell->OnYoshiTryEat()!=((variant&1)?5:6))return 12;
   if(shell->CleanupResources()!=1)return 13;delete shell;
  }
  daObjSimpleBg_c*(*factories[])()={daObjSimpleBg_c_classInit_DOKAN,daObjSimpleBg_c_classInit_MC_DODAI,daObjSimpleBg_c_classInit_MC_HAZAD};
  const unsigned ids[]={298,340,341};
  for(unsigned i=0;i<3;++i){
   auto* bg=factories[i]();if(!bg)return 14;
   bg->actorID=ids[i];bg->mPosX=100*4096;bg->mPosY=254*4096;bg->mPosZ=-200*4096;
   std::printf("SimpleBg castle resource cycle=%u id=%u\n",cycle,ids[i]);std::fflush(stdout);
   if(bg->InitResources()!=1 || bg->mVariant!=i+3)return 15;
   if(bg->CleanupResources()!=1)return 16;delete bg;
  }
  ShadowModel::CleanAll();CleanCommonModelDataArr();
  const unsigned callbacks=sm64ds_destroy_overlay_resources(102);
  std::printf("overlay102 cycle=%u callbacks=%u freeBefore=%u freeAfter=%u\n",cycle,callbacks,unsigned(before),unsigned(root->VMemoryLeft()));
  if(callbacks!=26||sm64ds_destroy_overlay_resources(102)!=0)return 6;
  if(root->VMemoryLeft()!=before)return 7;
  std::memset(sm64ds_overlay102_bss_begin,0xa5,960);
 }
 UnloadOverlay(9);
 sm64ds_destroy_overlay_resources(2);
 std::puts("smoke_overlay102_initializers: PASS four original initializers, shell states, 960-byte reset, 26 callbacks, four shell variants and three castle SimpleBg resources");return 0;
}
