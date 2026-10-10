#include "ntr/mmio.h"
#include "Heap.h"
#include "CutsceneObject.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
#include <cstring>
extern "C" {
void func_02059bc0();void CleanCommonModelDataArr();
void __sinit_02075154();void __sinit_0207501c();
void __sinit_02073a24();void __sinit_02074da8();
void __sinit_ov002_02107370();void __sinit_ov002_02106e40();
void sm64ds_init_common_scenery_profiles();
extern unsigned short data_020a4b54;extern unsigned data_020a4b60;
CutsceneObject* daDemo_c_classInit();
int sm64ds_set_overlay_scope(int);unsigned sm64ds_destroy_overlay_resources(int);
extern unsigned char data_0209f2d8;
extern void* data_ov002_0210bae4[],*data_ov002_0210bcc4[],*data_ov002_0210bce8[];
extern unsigned char data_ov009_02113c20[],data_ov009_02113c28[];
void* sm64ds_demo_level_model();void** sm64ds_demo_level_animation();
void func_ov002_020f65ec(void*);
void func_ov002_020f64ac(void*,void*);
extern unsigned char data_ov002_0210bc88[];
}
static_assert(sizeof(CutsceneObject)==0x104,"native cutscene actor layout");
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();__sinit_02073a24();__sinit_02074da8();
 const int previous=sm64ds_set_overlay_scope(2);__sinit_ov002_02107370();__sinit_ov002_02106e40();sm64ds_set_overlay_scope(previous);
 sm64ds_init_common_scenery_profiles();
 LoadOverlay(9);LoadOverlay(85);
 if(sm64ds_demo_level_model()!=data_ov009_02113c20 || *sm64ds_demo_level_animation()!=data_ov009_02113c28)return 3;
 const unsigned before=root->VMemoryLeft();
 for(unsigned cycle=0;cycle<3;++cycle){
  for(unsigned variant=0x12;variant<=0x2d;++variant){
   for(unsigned mode=0;mode<(variant==0x13?2u:1u);++mode){
    data_0209f2d8=mode?2:0;
    data_020a4b54=352;data_020a4b60=variant;
    auto* actor=daDemo_c_classInit();if(!actor)return 4;
    actor->actorID=352;actor->param1=variant;
    std::printf("demo cycle=%u variant=%x multiplayer=%u\n",cycle,variant,mode);std::fflush(stdout);
    if(actor->InitResources()!=1)return 5;
    if(actor->mScaleX!=0x1000 || actor->mScaleY!=0x1000 || actor->mScaleZ!=0x1000)return 6;
    if(variant>=0x14 && variant<=0x19){
     if(!actor->mModel || actor->mModelAnim || *reinterpret_cast<void***>(actor->mModel)!=data_ov002_0210bae4)return 7;
    }else{
     if(!actor->mModelAnim || actor->mModel || *reinterpret_cast<void***>(actor->mModelAnim)!=data_ov002_0210bcc4)return 8;
     if(*reinterpret_cast<void***>(reinterpret_cast<char*>(actor->mModelAnim)+0x50)!=data_ov002_0210bce8)return 9;
     for(unsigned frame=0;frame<120;++frame){func_ov002_020f64ac(actor->mModelAnim,data_ov002_0210bc88);func_ov002_020f65ec(actor->mModelAnim);}
    }
    if(actor->CleanupResources()!=1)return 10;delete actor;
    CleanCommonModelDataArr();
    const unsigned after=root->VMemoryLeft();
    if(after!=before){std::printf("demo heap mismatch %u != %u\n",after,before);return 11;}
   }
  }
 }
 UnloadOverlay(85);UnloadOverlay(9);sm64ds_destroy_overlay_resources(2);
 std::puts("smoke_demo_resources: PASS 29 resource variants x3, 120 animation steps, native vptrs and exact heap recovery");return 0;
}
