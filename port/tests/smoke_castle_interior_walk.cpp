#include <cmath>
#include "player_telemetry.h"
#include "player_interior_route.h"
// Bounded original scene handoff; overlay2 remains statically resident.
#include "ntr/mmio.h"
#include "menu/menu.h"
#include "ntr/gx.h"
#include "ntr/ppu.h"
#include "Heap.h"
#include "dScene_c.h"
#include "Stage.h"
#include "Player.h"
#include "unmatched/actor_registry.h"
#include "unmatched/native_overlay.h"
#include "audio/sound_native.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace Sound { void LoadInitialGroup(int); }
extern "C" {
void func_02059bc0();void __sinit_02075154();void __sinit_0207501c();
void __sinit_02073a24();void __sinit_02074da8();void __sinit_02074e84();
void func_0201a4e4();void func_0201fe08();void func_020133bc();
void func_02019390();void func_02044120();void func_02019404();
void func_0203bb60();void func_0203bc7c();void func_02023498();
void func_020190b8();void func_02019144();void func_02019100();void func_020132d8();
int func_0205b070(int);void** func_0205b274(int);
int func_02043098(int,int,int,int);unsigned short* func_0203dabc(int);int func_0203da9c();
void* daPly_c_classInit();void* dCamera_c_classInit();Stage* dScStage_c_classInit();
void __sinit_ov002_02107370();void sm64ds_init_common_scenery_profiles();
void __sinit_ov002_02100560();void __sinit_ov002_02107298();
void __sinit_ov002_021019d0();void __sinit_ov002_02106e40();void __sinit_ov002_021071f4();
void sm64ds_init_level_scripts();
void* daObjTatefuda_c_classInit();void* da1up_c_classInit_ONEUPKINOKO();
void* daObjBlockL_c_classInit_BLOCK_LL();
void __sinit_ov002_02101738();void __sinit_ov002_02100adc();void __sinit_ov002_02100d44();
void __sinit_ov002_0210804c();
extern unsigned char g_profile_WATERFALL[];
int sm64ds_set_overlay_scope(int);
extern void* data_020a0ea0;extern void* data_020a0eac;
extern unsigned short data_02092664;extern unsigned char data_02092660;
extern signed char data_02092110;extern unsigned char data_0209f268;
extern void* data_0209f5c0;
extern unsigned data_020a4b98[];
extern int data_0209cef8;
extern Player* data_0209f394[];
extern unsigned char data_0209f2f8;
extern unsigned char data_0209ee90[];
extern int* data_0209f318;
extern int data_0209fc48;
extern unsigned char data_020a1162[];
extern int data_020a0db0;
extern int data_0209caa0[];
extern unsigned char data_0209f2c4;
extern unsigned char data_0209f248;
extern unsigned char data_0209f22c;
extern unsigned short data_0209f300;
extern unsigned short data_0209b274;
extern unsigned char data_0209d6bc;
extern unsigned char data_0209d660;
extern unsigned char data_0209f498[];
}
// Optional linker wrappers locate mutation of the renderer list's callback.
static unsigned expectedRenderCallback;
static int activeFactory=-1;
static void list_guard(const char* phase,void* list,void* node){
 if(expectedRenderCallback && data_020a4b98[2]!=expectedRenderCallback){
   const auto* l=static_cast<unsigned*>(list);const auto* n=static_cast<unsigned*>(node);
   std::printf("FATAL: render callback changed at %s actor=%d list=%p node=%p head=%08x tail=%08x callback=%08x expected=%08x node=%08x/%08x/%08x\n",phase,activeFactory,list,node,l[0],l[1],data_020a4b98[2],expectedRenderCallback,n?n[0]:0,n?n[1]:0,n?n[2]:0);std::abort();
 }
}
#define LIST_WRAPPER(name) extern "C" int __real_##name(void*,void*);extern "C" int __wrap_##name(void*l,void*n){list_guard(#name " before",l,n);int r=__real_##name(l,n);list_guard(#name " after",l,n);return r;}
LIST_WRAPPER(func_0204405c)
LIST_WRAPPER(func_0203b244)
LIST_WRAPPER(func_0203b20c)
LIST_WRAPPER(func_0203b27c)
extern "C" int __real_func_0203b2ec(void*,void*,void*);
extern "C" int __wrap_func_0203b2ec(void*l,void*n,void*p){list_guard("insert before",l,n);int r=__real_func_0203b2ec(l,n,p);list_guard("insert after",l,n);return r;}
extern "C" int __real_func_02043098(int,int,int,int);
extern "C" int __wrap_func_02043098(int a,int b,int c,int d){int previous=activeFactory;activeFactory=a;list_guard("factory before",data_020a4b98,nullptr);int r=__real_func_02043098(a,b,c,d);list_guard("factory after",data_020a4b98,nullptr);activeFactory=previous;return r;}
extern "C" void* __real__ZN4Heap8AllocateEji(Heap*,unsigned,int);
extern "C" void* __wrap__ZN4Heap8AllocateEji(Heap* heap,unsigned size,int align){
 if(size>heap->VMaxAllocatableSize())std::printf("handoff heap request size=%u align=%d heap=%p left=%u caller=%p\n",size,align,heap,heap->VMemoryLeft(),__builtin_return_address(0));
 return __real__ZN4Heap8AllocateEji(heap,size,align);
}
extern "C" void* __real__ZN4Heap8AllocateEj(Heap*,unsigned);
extern "C" void* __wrap__ZN4Heap8AllocateEj(Heap* heap,unsigned size){
 if(size>heap->VMaxAllocatableSize())std::printf("handoff heap single size=%u heap=%p left=%u frame=%u caller=%p\n",size,heap,heap->VMemoryLeft(),data_020a0db0,__builtin_return_address(0));
 return __real__ZN4Heap8AllocateEj(heap,size);
}
#define CHECK(c) do {if(!(c)){std::printf("handoff FAILED line%d: %s\n",__LINE__,#c);return 1;}}while(0)
struct InputSegment { unsigned frames; unsigned short keys; };
static const InputSegment routeKeys[] = {{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x50},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x50},{20,0x40},{20,0x60},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x40},{20,0x60},{20,0x40},{20,0x40},{20,0x60},{30,0x0},{1,0x1},{60,0x0},{20,0x80},{20,0xa0},{20,0xa0},{20,0xa0},{20,0x20},{20,0x20},{20,0x20},{20,0x20},{20,0x20},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{10,0x822},{10,0x820},{10,0x860},{10,0x862},{10,0x860},{10,0x860},{10,0x842},{10,0x840},{10,0x840},{10,0x842},{10,0x840},{10,0x850},{10,0x852},{10,0x850},{10,0x850},{10,0x852},{10,0x850},{10,0x850},{10,0x852},{10,0x850},{10,0x840},{10,0x842},{10,0x840},{10,0x840},{10,0x842},{10,0x850},{10,0x850},{10,0x842},{10,0x840},{10,0x840},{10,0x852},{10,0x850},{10,0x850},{10,0x852},{10,0x851},{10,0x810},{10,0x812},{10,0x810},{10,0x850},{10,0x852},{10,0x840},{10,0x840},{10,0x843},{10,0x820},{10,0x821},{10,0x822},{10,0x8a0},{10,0x8a0},{10,0x8a3},{10,0x820},{10,0x811},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0},{1,0x1},{45,0x0}};
static unsigned short routeInput(unsigned elapsed) { for(const auto& s:routeKeys){if(elapsed<s.frames)return s.keys;elapsed-=s.frames;}return 0;}
static unsigned short interiorInput(unsigned elapsed){
 if(elapsed<playerRouteSettle)return 0;elapsed-=playerRouteSettle;
 for(const auto& segment:playerRouteSegments){if(elapsed<segment.updates)return segment.keys;elapsed-=segment.updates;}return 0;
}
static const char* interiorCheckpoint(unsigned elapsed){
 if(elapsed+1==playerRouteSettle)return "start";
 if(elapsed<playerRouteSettle)return nullptr;elapsed-=playerRouteSettle;
 for(const auto& segment:playerRouteSegments){if(elapsed+1==segment.updates)return segment.checkpoint;if(elapsed<segment.updates)return nullptr;elapsed-=segment.updates;}return nullptr;
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(!std::getenv("SM64DS_SAVE_PATH"))setenv("SM64DS_SAVE_PATH","sdmc:/sm64ds/menu-startfile-validation.sav",1);
 CHECK(ntr::io_init());CHECK(Heap::SetupRootHeap());
 // Original boot func0201a054 sets this graph/render baseline before scenes.
 *reinterpret_cast<unsigned*>(data_0209ee90+0x44)=0x100;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();data_020a0eac=data_020a0ea0;
 __sinit_02073a24();__sinit_02074da8();__sinit_02074e84();func_0201a4e4();
 func_020133bc();Sound::LoadInitialGroup(0);func_0201fe08();
 struct Profile{void*(*create)();unsigned short behavior,render;unsigned flags;int offsetY,radius,clip,far;};
 static Profile player={daPly_c_classInit,0xbf,0x12,2,0xfa000,0xfa000,0x2ee0000,0x2ee0000};
 static Profile signpost={daObjTatefuda_c_classInit,0xb8,0xa7,0x82,0x40000,0x80000,0x1000000,0x1000000};
 static Profile oneup={da1up_c_classInit_ONEUPKINOKO,276,140,3,0x32000,0x12c000,0x1000000,0x1000000};
 static Profile block={daObjBlockL_c_classInit_BLOCK_LL,17,34,2,0x96000,0x12c000,0x1800000,0x1800000};
 struct BaseProfile{void*(*create)();unsigned short behavior,render;};
 static BaseProfile camera={dCamera_c_classInit,0x14c,0};
 struct SceneProfile{Stage*(*create)();unsigned short behavior,render;};
 static SceneProfile stage={dScStage_c_classInit,3,6};
 data_020a4bb8[0xbf]=&player;data_020a4bb8[0x14c]=&camera;data_020a4bb8[3]=&stage;
 data_020a4bb8[0xb8]=&signpost;data_020a4bb8[276]=&oneup;data_020a4bb8[17]=&block;
 data_020a4bb8[0xc5]=g_profile_WATERFALL;
 const int scope=sm64ds_set_overlay_scope(2);__sinit_ov002_02107370();sm64ds_set_overlay_scope(scope);
 sm64ds_init_common_scenery_profiles();
 CHECK(sm64ds_try_load_overlay(7)==OVERLAY_OK);
 data_02092664=0x187;data_02092660=1;
 const auto menu=func_02043098(1,0,0,2);CHECK(menu);
 const unsigned renderCallback[2]={data_020a4b98[2],data_020a4b98[3]};
 expectedRenderCallback=renderCallback[0];
 std::printf("render callback=%08x/%08x\n",renderCallback[0],renderCallback[1]);
 bool requested=false,unloaded=false;void* spawned=nullptr;unsigned stageFrames=0;
 const unsigned limit=std::getenv("SM64DS_HANDOFF_STAGE_FRAMES")?std::strtoul(std::getenv("SM64DS_HANDOFF_STAGE_FRAMES"),nullptr,10):3000;
 CHECK(limit>=1500 && limit<=12000);
 const unsigned returnLimit=std::getenv("SM64DS_HANDOFF_RETURN_FRAMES")?std::strtoul(std::getenv("SM64DS_HANDOFF_RETURN_FRAMES"),nullptr,10):120;
 CHECK(returnLimit>=120 && returnLimit<=1200);
 const bool saveQuit=std::getenv("SM64DS_HANDOFF_SAVE_QUIT")!=nullptr;
 bool saveMenuTapped=false,saveQuitTapped=false,returnedMenu=false;unsigned returnedFrames=0;int previousPause=-1;
 int previousScript=-1;unsigned controlFrame=0;unsigned caughtTick=0;unsigned entryWaypoint=0;unsigned interiorFrame=0;unsigned interiorControlFrame=0,lastInteriorAdvance=~0u;unsigned interiorSamples=0;unsigned short chaseKeys=0;int startX=0,startZ=0;bool moved=false;
 std::int16_t pcm[2*1093];
 for(unsigned frame=0;frame<1100+limit+returnLimit;++frame){
   ntr::gx_begin_frame();
   auto* touch=func_0203dabc(func_0203da9c());std::memset(touch,0,8);
   if(frame>=700 && frame<705){touch[0]=122;touch[1]=165;touch[2]=1;}
   if(frame>=900 && frame<905){touch[0]=44;touch[1]=70;touch[2]=1;}
   auto* rawKeys=reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24);*rawKeys=0;
   if(spawned && !returnedMenu && !controlFrame && data_0209d660 && !data_0209fc48 && (data_0209caa0[2]&0x80) && (data_0209d6bc==7 || data_0209d6bc==8) && stageFrames%30==0){
     *rawKeys=1;std::printf("handoff advance waiting message state=%u frame=%u\n",unsigned(data_0209d6bc),stageFrames);
   }
   if(spawned && !returnedMenu && data_0209f394[0] && !data_0209d660 && !data_0209fc48 && (data_0209caa0[2]&0x80) && !Stage::IsPauseDisabled()){
     if(!controlFrame){controlFrame=stageFrames;startX=data_0209f394[0]->mPosX;startZ=data_0209f394[0]->mPosZ;std::printf("handoff control enabled frame=%u\n",stageFrames);}
   }
   if(controlFrame && spawned && !returnedMenu && data_0209f394[0]){
     const unsigned elapsed=stageFrames-controlFrame;
     if(elapsed>=300 && elapsed<880 && data_0209f2f8==1 && !Stage::IsPauseDisabled())*rawKeys=routeInput(elapsed-300);
     if(data_0209d660 && elapsed%45==0)*rawKeys=1;
   }
   // Retail welcome dialogue is advanced by one A press with releases between.
   // Anchor at 45 updates after its closing press, exactly like DS collector.
   if(spawned && !returnedMenu && data_0209f2f8==2 && data_0209f394[0]){
     *rawKeys=0;
     if(data_0209d660 && !data_0209fc48 && (data_0209d6bc==7 || data_0209d6bc==8) && stageFrames%45==0){*rawKeys=1;lastInteriorAdvance=stageFrames;}
     if(!interiorControlFrame && !data_0209d660 && !data_0209fc48 && !Stage::IsPauseDisabled() && lastInteriorAdvance!=~0u && stageFrames>=lastInteriorAdvance+45){
       interiorControlFrame=stageFrames;std::printf("interior control frame=%u after natural A dismiss at %u\n",stageFrames,lastInteriorAdvance);
     }
     if(interiorControlFrame)*rawKeys=interiorInput(stageFrames-interiorControlFrame);
   }
   if(saveQuit && spawned && !returnedMenu && data_0209f2c4==1 && !data_0209f22c && !data_0209f300){
     if(data_0209f248==1 && !saveMenuTapped){touch[0]=128;touch[1]=168;touch[2]=1;saveMenuTapped=true;std::puts("handoff tap Save menu");}
     if(data_0209f248==0xd && !saveQuitTapped){touch[0]=128;touch[1]=96;touch[2]=1;saveQuitTapped=true;std::puts("handoff tap Save and Quit");}
   }
   CHECK(sm64ds_local_input_publish(*rawKeys,touch));
   func_02019390();func_0203bb60();func_0203bc7c();func_02023498();
   if(requested && data_02092660==0 && !unloaded){
     std::printf("menu lifecycle destroyed scene at frame=%u\n",frame);
     CHECK(sm64ds_try_unload_overlay(7)==OVERLAY_OK);unloaded=true;
     CHECK(data_02092664==3);CHECK(data_02092110==1 && data_0209f268==0);
     const int previous=sm64ds_set_overlay_scope(2);
     __sinit_ov002_02100560();__sinit_ov002_02107298();__sinit_ov002_021019d0();
     __sinit_ov002_02106e40();__sinit_ov002_021071f4();
     __sinit_ov002_02101738();__sinit_ov002_02100adc();__sinit_ov002_02100d44();
     __sinit_ov002_0210804c();
     sm64ds_set_overlay_scope(previous);sm64ds_init_level_scripts();
     spawned=reinterpret_cast<void*>(dScene_c::SpawnIfNecessary());
     std::printf("SpawnIfNecessary Stage=%p pending=%u live=%u\n",spawned,data_02092664,data_02092660);
     std::printf("render list after factory head=%08x tail=%08x callback=%08x/%08x\n",data_020a4b98[0],data_020a4b98[1],data_020a4b98[2],data_020a4b98[3]);
     std::printf("handoff common model records=%d (ROM capacity100)\n",data_0209cef8);
     CHECK(spawned);
   }
   // Intro requests the same level at entrance0 (star1) after its arrival sequence.
   // Run the original Stage destruction before constructing its replacement.
   if(spawned && data_02092660==0){
     std::printf("handoff Stage destroyed frame=%u stageFrame=%u pending=%u nextLevel=%d entrance=%u\n",frame,stageFrames,data_02092664,int(data_02092110),unsigned(data_0209f268));
     if(data_02092664==1){
       CHECK(saveQuitTapped);CHECK(sm64ds_try_load_overlay(7)==OVERLAY_OK);
       spawned=reinterpret_cast<void*>(dScene_c::SpawnIfNecessary());CHECK(spawned);returnedMenu=true;
       std::printf("handoff return menu=%p after original Stage cleanup\n",spawned);
     }else{
       CHECK(data_02092664==3);
       spawned=reinterpret_cast<void*>(dScene_c::SpawnIfNecessary());CHECK(spawned);
       std::printf("handoff replacement Stage=%p\n",spawned);
     }
   }
   CHECK(data_020a4b98[2]==renderCallback[0] && data_020a4b98[3]==renderCallback[1]);
   func_02044120();func_02019404();++data_020a0db0;func_020190b8();func_02019144();func_02019100();func_020132d8();
   if(!requested && data_02092664==3){requested=true;std::printf("menu requested Stage frame=%u\n",frame);}
   CHECK(func_0205b070(1));while(func_0205b274(0)){}
   const unsigned samples=((frame+1)*32768/30)-(frame*32768/30);
   CHECK(sm64ds_native_sound_render(pcm,samples));
   if(returnedMenu){
     if(++returnedFrames==returnLimit){
       for(unsigned engine=0;engine<2;++engine){static ntr::Framebuffer fb;ntr::ppu_scanout_scene(ntr::Engine(engine),fb);char name[80];std::snprintf(name,sizeof(name),"menu-handoff-return-%u.bmp",engine);CHECK(ntr::ppu_write_bmp(name,fb));}
       CHECK(moved);std::puts("smoke_menu_handoff: PASS native Save and Quit returned menu; overlay2 statically resident");return 0;
     }
   }else if(spawned && data_02092660!=0){
     size_t triangles=0;ntr::gx_polygons(triangles);
     CHECK(data_0209f5c0==spawned);
     auto* playerActor=data_0209f394[0];CHECK(playerActor);
     const int pausePhase=(int(data_0209f2c4)<<8)|data_0209f248;
     const bool routeCheckpoint=controlFrame && stageFrames>=controlFrame+300 && (stageFrames-controlFrame-300)%100==99;
     if(data_0209f2f8==2 && !interiorFrame)interiorFrame=stageFrames;
     const unsigned interiorElapsed=interiorControlFrame?stageFrames-interiorControlFrame:0;
     const char* interiorLabel=interiorControlFrame?interiorCheckpoint(interiorElapsed):nullptr;
     const bool keyRouteComplete=interiorControlFrame && interiorElapsed+1==playerRouteSettle+playerRouteLength && data_0209f2f8==2;
     if(interiorLabel){CHECK(data_0209f318);player_telemetry::collect(interiorLabel,interiorElapsed+1,*rawKeys,playerActor,data_0209f318,data_0209f2f8,data_0209d660,data_0209d6bc,unsigned(data_0209fc48),unsigned(data_0209caa0[1]),unsigned(data_0209caa0[2]));++interiorSamples;}
     const bool capture=interiorLabel || keyRouteComplete || routeCheckpoint || previousPause!=pausePhase || stageFrames==119 || stageFrames==0 || stageFrames%600==0 || stageFrames+1==limit || previousScript!=data_0209fc48 || (controlFrame && (stageFrames==controlFrame || stageFrames-controlFrame==30 || stageFrames-controlFrame==90 || stageFrames-controlFrame==121));
     if(controlFrame && stageFrames-controlFrame==880){moved=playerActor->mPosX!=startX || playerActor->mPosZ!=startZ;std::printf("handoff input displacement moved=%u dx=%d dz=%d\n",unsigned(moved),playerActor->mPosX-startX,playerActor->mPosZ-startZ);}
     previousScript=data_0209fc48;previousPause=pausePhase;
     if(capture){
     if(auto* rabbit=dActor_c::FindWithActorID(0xbb,nullptr)){
       auto* r=reinterpret_cast<unsigned char*>(rabbit);
       std::printf("key rabbit actor=%p pos=%d,%d,%d state=%08x eaten=%u disabled=%u path=%d node=%d\n",rabbit,rabbit->mPosX,rabbit->mPosY,rabbit->mPosZ,*reinterpret_cast<unsigned*>(r+0x364),unsigned(r[0x426]),unsigned(r[0x428]),*reinterpret_cast<int*>(r+0x438),*reinterpret_cast<int*>(r+0x448));
     }else std::puts("key rabbit absent");
     std::printf("key progression persistent=%08x transient=%08x message=%u\n",unsigned(data_0209caa0[1]),unsigned(data_0209caa0[2]),unsigned(data_0209d660));
     std::printf("handoff mapped held=%04x pressed=%04x magnitude=%d\n",unsigned(*reinterpret_cast<unsigned short*>(data_0209f498+4)),unsigned(*reinterpret_cast<unsigned short*>(data_0209f498+6)),int(*reinterpret_cast<short*>(data_0209f498+8)));
     std::printf("handoff phase script=%08x timer=%u saveflags=%08x pause=%u state=%u raw=%04x\n",data_0209fc48,unsigned(data_0209b274),unsigned(data_0209caa0[2]),unsigned(data_0209f2c4),unsigned(data_0209f248),unsigned(*rawKeys));
     std::printf("handoff Stage frame=%u triangles=%u current=%p level=%u player=%p pos=%d,%d,%d state=%p step=%u opacity=%u flags=%08x\n",stageFrames,unsigned(triangles),data_0209f5c0,data_0209f2f8,playerActor,playerActor->mPosX,playerActor->mPosY,playerActor->mPosZ,playerActor->mState,playerActor->mStateStep,playerActor->mOpacity,playerActor->mFlags);
     if(data_0209f318){auto*c=data_0209f318;std::printf("handoff camera look=%x,%x,%x eye=%x,%x,%x script=%08x powcnt=%04x\n",c[0x80/4],c[0x84/4],c[0x88/4],c[0x8c/4],c[0x90/4],c[0x94/4],data_0209fc48,unsigned(NTR_MMIO(unsigned short,0x04000304)));}
     if(data_0209f318){auto*c=data_0209f318;auto*half=reinterpret_cast<short*>(c);std::printf("handoff camera parameters scale=%d,%d mode=%08x flags=%08x distance=%d blend=%d angles=%d,%d,%d,%d\n",c[0x104/4],c[0x108/4],unsigned(c[0x13c/4]),unsigned(c[0x154/4]),c[0x12c/4],c[0x130/4],half[0x178/2],half[0x17a/2],half[0x17c/2],half[0x17e/2]);}
     unsigned positive=0,onscreen=0;size_t count=0;auto* tris=ntr::gx_polygons(count);
     for(size_t i=0;i<count;++i)for(const auto& v:tris[i].v){positive+=v.w>0;onscreen+=v.w>0 && v.x>=0 && v.x<256 && v.y>=0 && v.y<192;}
     std::printf("handoff geometry positive=%u onscreen=%u\n",positive,onscreen);
     for(unsigned engine=0;engine<2;++engine){
       static ntr::Framebuffer fb;ntr::ppu_scanout_scene(ntr::Engine(engine),fb);
       char name[80];std::snprintf(name,sizeof(name),"menu-handoff-stage-%u-%u.bmp",stageFrames,engine);
       CHECK(ntr::ppu_write_bmp(name,fb));
     }
     }
     if(++stageFrames==limit || keyRouteComplete){CHECK(keyRouteComplete);CHECK(!saveQuit);CHECK(moved);CHECK(data_0209caa0[1]&0x40);CHECK(data_0209f2f8==2);CHECK(!data_0209d660 && !data_0209fc48);CHECK(interiorSamples==1+sizeof(playerRouteSegments)/sizeof(playerRouteSegments[0]));std::printf("smoke_castle_interior_walk: PASS %u Stage updates; %u interior telemetry checkpoints; genuine earned key and natural input\n",stageFrames,interiorSamples);return 0;}
   }
 }
 std::puts("handoff FAILED timeout waiting for scene transition");return 1;
}
