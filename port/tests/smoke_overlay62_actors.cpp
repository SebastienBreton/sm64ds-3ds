#include "ntr/mmio.h"
#include "unmatched/native_overlay.h"
#include "Heap.h"
#include "daRFlag_c.h"
#include "daHolhei_c.h"
#include "daJango_c.h"
#include "daRNk_c.h"
#include "PathPtr.h"
#include "SharedFilePtr.h"
#include <cstdio>
extern "C" {
void func_02059bc0();
void CleanCommonModelDataArr();
void __sinit_02075154();void __sinit_0207501c();
void __sinit_02073a24();void __sinit_02074da8();
void __sinit_ov062_0211cf30();void __sinit_ov062_0211d2d8();
void __sinit_ov062_0211d4a0();void __sinit_ov062_0211d690();void __sinit_ov062_0211d6fc();
int sm64ds_set_overlay_scope(int);unsigned sm64ds_destroy_overlay_resources(int);
daRFlag_c* daRFlag_c_classInit();
daHolhei_c* daHolhei_c_classInit();
daJango_c* daJango_c_classInit();
daRNk_c* daRNk_c_classInit();
extern unsigned char data_0209f220;
extern signed char data_0209f2f8;
extern int data_020a0d84,data_020a0d88;
dActor_c* daNknk_c_classInit_NOKONOKO();
dActor_c* daNknk_c_classInit_NOKONOKO_S();
void __sinit_ov002_02100560();void __sinit_ov002_02107298();void __sinit_ov002_021019d0();
}
int main(){
 if(!ntr::io_init())return 2;
 auto* root=Heap::SetupRootHeap();if(!root)return 2;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();
 __sinit_02073a24();__sinit_02074da8();
 const int previous=sm64ds_set_overlay_scope(2);
 __sinit_ov002_02100560();__sinit_ov002_02107298();__sinit_ov002_021019d0();
 sm64ds_set_overlay_scope(previous);
 // Four synthetic path nodes exercise the real PathPtr loader. They do not
 // constitute a gameplay or original-level path fidelity test.
 PathDef path={0,4,0,0,0x80};
 short nodes[4][3]={{100,254,200},{200,254,200},{200,254,300},{100,254,300}};
 data_020a0d84=reinterpret_cast<int>(&path);data_020a0d88=reinterpret_cast<int>(nodes);
 const auto before=root->VMemoryLeft();
 unsigned afterFirst=0;
 for(unsigned cycle=0;cycle<2;++cycle){
  LoadOverlay(62);
  auto* flag=daRFlag_c_classInit();if(!flag)return 3;
  std::printf("flag factory cycle=%u size=%u\n",cycle,unsigned(sizeof(*flag)));std::fflush(stdout);
  flag->mPosX=100*4096;flag->mPosY=254*4096;flag->mPosZ=-200*4096;
  if(flag->InitResources()!=1)return 4;
  for(unsigned i=0;i<120;++i)if(flag->Behavior()!=1)return 5;
  if(flag->mHasTouchedFlag!=255||flag->mVictoryTimer!=0)return 6;
  if(flag->mModelAnim.mat4x3.t.x!=flag->mPosX/8 ||
     flag->mModelAnim.mat4x3.t.y!=(flag->mPosY+60*4096)/8 ||
     flag->mModelAnim.mat4x3.t.z!=flag->mPosZ/8)return 7;
  if(flag->CleanupResources()!=1)return 8;
  delete flag;
  auto* chuckya=daHolhei_c_classInit();if(!chuckya)return 10;
  chuckya->mPosX=100*4096;chuckya->mPosY=254*4096;chuckya->mPosZ=-200*4096;
  std::puts("Chuckya factory: loading model and five animations");std::fflush(stdout);
  if(chuckya->InitResources()!=1)return 11;
  if(!chuckya->mState || chuckya->mHomePosX!=chuckya->mPosX ||
     chuckya->mHomePosY!=chuckya->mPosY || chuckya->mHomePosZ!=chuckya->mPosZ)return 12;
  for(unsigned i=0;i<120;++i)chuckya->mModel.Advance();
  if(chuckya->CleanupResources()!=1)return 13;
  delete chuckya;
  for(unsigned variant=0;variant<4;++variant){
   auto* koopa=variant<2?daNknk_c_classInit_NOKONOKO():daNknk_c_classInit_NOKONOKO_S();
   if(!koopa)return 16;
   koopa->actorID=variant<2?0xcb:0xcc;koopa->param1=variant&1;
   std::printf("Koopa resource variant=%u\n",variant);std::fflush(stdout);
   if(koopa->InitResources()!=1)return 17;
   if(koopa->CleanupResources()!=1)return 18;
   delete koopa;
  }
  auto* bird=daJango_c_classInit();if(!bird)return 19;
  bird->param1=0;
  std::puts("Klepto: loading blended model and path");std::fflush(stdout);
  if(bird->InitResources()!=1)return 20;
  if(bird->mPathNodePosX!=100*4096||bird->mPathNodePosY!=254*4096||bird->mPathNodePosZ!=200*4096)return 21;
  for(unsigned i=0;i<120;++i)bird->mBlendModelAnim.Advance();
  if(bird->CleanupResources()!=1)return 22;
  delete bird;
  // Castle grounds registers a star marker even outside the selected star.
  data_0209f220=1;data_0209f2f8=1;
  auto* racer=daRNk_c_classInit();if(!racer)return 23;
  racer->param1=0;racer->mAngleX=0;
  std::puts("Koopa the Quick: loading model, animations and path");std::fflush(stdout);
  if(racer->InitResources()!=1)return 24;
  std::printf("race path count=%d target=%d,%d,%d tracked=%d\n",racer->mNumPathPts,racer->mPathTarget.x,racer->mPathTarget.y,racer->mPathTarget.z,int(racer->mTrackedStar));std::fflush(stdout);
  if(racer->mNumPathPts!=4 || racer->mPathTarget.x!=100*4096 ||
     racer->mPathTarget.y!=254*4096 || racer->mPathTarget.z!=200*4096 || racer->mTrackedStar<0)return 25;
  for(unsigned i=0;i<120;++i)racer->mModelAnim.Advance();
  if(racer->CleanupResources()!=1)return 26;
  racer->UntrackStar(racer->mTrackedStar);
  if(racer->mTrackedStar!=-1)return 27;
  delete racer;
  UnloadOverlay(62);
  unsigned remaining=sm64ds_destroy_overlay_resources(62);
  std::printf("overlay62 cycle=%u flagBehavior=120 remainingDestructors=%u heapBefore=%u heapAfter=%u\n",cycle,remaining,unsigned(before),unsigned(root->VMemoryLeft()));std::fflush(stdout);
  if(remaining!=0)return 9;
  if(cycle==0)afterFirst=root->VMemoryLeft();
  else if(root->VMemoryLeft()!=afterFirst)return 14;
 }
 ShadowModel::CleanAll();
 CleanCommonModelDataArr();
 std::printf("scene cleanup heapBefore=%u heapAfter=%u\n",unsigned(before),unsigned(root->VMemoryLeft()));
 if(root->VMemoryLeft()!=before)return 15;
 sm64ds_destroy_overlay_resources(2);
 std::puts("smoke_overlay62_actors: PASS six factories, resources, animation, path loading and scene cleanup twice");return 0;
}
