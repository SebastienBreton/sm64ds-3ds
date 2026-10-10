// Original main menu -> Luigi tab -> Memory Match -> rules -> Quit.
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "ntr/ppu.h"
#include "Heap.h"
#include "dScene_c.h"
#include "unmatched/native_overlay.h"
#include "audio/sound_native.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Sound{void LoadInitialGroup(int);}
extern "C" {
void func_02059bc0();void __sinit_02075154();void __sinit_0207501c();
void __sinit_02073a24();void __sinit_02074da8();void __sinit_02074e84();
void func_0201a4e4();void func_0201fe08();void func_020133bc();
void func_02019390();void func_02044120();void func_02019404();
void func_0203bb60();void func_0203bc7c();void func_02023498();
void func_020190b8();void func_02019144();void func_02019100();void func_020132d8();
int func_0205b070(int);void** func_0205b274(int);
int func_02043098(int,int,int,int);unsigned short* func_0203dabc(int);int func_0203da9c();
extern void* data_020a0ea0;extern void* data_020a0eac;
extern unsigned short data_02092664;extern unsigned char data_02092660;
extern unsigned char data_0209ee90[],data_020a1162[];
extern int data_020a0db0,data_0208a170;
extern unsigned char* data_ov004_020beb68;
extern unsigned data_ov006_021422dc[];
const char* sm64ds_sseq_error();
}
#define CHECK(c) do{if(!(c)){std::printf("minigame menu FAILED line%d: %s\n",__LINE__,#c);return 1;}}while(0)
static void capture(const char* label){
 for(unsigned e=0;e<2;++e){static ntr::Framebuffer fb;ntr::ppu_scanout_scene(ntr::Engine(e),fb);
  char name[96];std::snprintf(name,sizeof name,"memory-%s-%u.bmp",label,e);ntr::ppu_write_bmp(name,fb);}
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(!std::getenv("SM64DS_SAVE_PATH"))setenv("SM64DS_SAVE_PATH","sdmc:/sm64ds/memory-validation.sav",1);
 CHECK(ntr::io_init());CHECK(Heap::SetupRootHeap());
 *reinterpret_cast<unsigned*>(data_0209ee90+0x44)=0x100;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();data_020a0eac=data_020a0ea0;
 __sinit_02073a24();__sinit_02074da8();__sinit_02074e84();func_0201a4e4();
 func_020133bc();Sound::LoadInitialGroup(0);func_0201fe08();
 CHECK(sm64ds_try_load_overlay(7)==OVERLAY_OK);
 data_02092664=0x187;data_02092660=1;CHECK(func_02043098(1,0,0,2));
 unsigned scene=1,ov=7,local=0,visits=0,resultAt=0;bool entered=false,returned=false,played=false;
 
 const unsigned visitCount=std::getenv("SM64DS_MEMORY_RELOAD")?2:1;
 std::int16_t pcm[2186];
 for(unsigned frame=0;frame<6500;++frame){
  const char* captureLabel=nullptr;
  ntr::gx_begin_frame();auto*touch=func_0203dabc(func_0203da9c());std::memset(touch,0,8);
  *reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=0;
  if(scene==1 && !entered && local>=700 && local<705){touch[0]=222;touch[1]=166;touch[2]=1;}
  if(scene==5 && !played && local>=150 && local<155){touch[0]=85;touch[1]=65;touch[2]=1;}
  if(scene==5 && local>=100 && local<105){touch[0]=24;touch[1]=121;touch[2]=1;}
  if(scene==362 && data_ov004_020beb68){
   if(local==400){
    auto* game=data_ov004_020beb68;
    CHECK(*reinterpret_cast<int*>(game+0xa8)==15);
    CHECK(*reinterpret_cast<int*>(game+0xb4)==0);
    CHECK(*reinterpret_cast<int*>(game+0xb8)==0);
    std::printf("memory fresh save reload coins=15 round=0\n");captureLabel="active";
   }
   if(local==420)*reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=1;
   if(local==510)captureLabel="rules";
   if(local>=530 && local<535){touch[0]=128;touch[1]=96;touch[2]=1;resultAt=local;}
  }
  func_02019390();func_0203bb60();func_0203bc7c();func_02023498();
  if(!data_02092660){
   unsigned next=data_02092664;std::printf("minigame transition %u->%u frame=%u\n",scene,next,frame);
   CHECK((scene==1 && next==5 && !entered)||(scene==5 && next==362)||(scene==362 && next==5 && resultAt));
   if(scene==362){CHECK(sm64ds_try_unload_overlay(6)==OVERLAY_OK);CHECK(sm64ds_try_unload_overlay(4)==OVERLAY_OK);}
   else CHECK(sm64ds_try_unload_overlay(ov)==OVERLAY_OK);
   scene=next;
   if(scene==362){CHECK(sm64ds_try_load_overlay(4)==OVERLAY_OK);CHECK(sm64ds_try_load_overlay(6)==OVERLAY_OK);++visits;played=true;}
   else {ov=5;CHECK(sm64ds_try_load_overlay(ov)==OVERLAY_OK);if(played)returned=true;}
   if(scene==362){CHECK(data_ov006_021422dc[0]!=0);std::printf("memory original state table initialized=%08x\n",data_ov006_021422dc[0]);}
   CHECK(dScene_c::SpawnIfNecessary());local=0;entered=true;
  }
  func_02044120();func_02019404();++data_020a0db0;
  func_020190b8();func_02019144();func_02019100();func_020132d8();
  CHECK(func_0205b070(1));while(func_0205b274(0)){}
  if(!sm64ds_native_sound_render(pcm,((frame+1)*32768/30)-(frame*32768/30))){std::printf("sound refused: %s\n",sm64ds_sseq_error());return 1;}
  if(captureLabel){std::size_t count=0;ntr::gx_polygons(count);std::printf("memory capture %s triangles=%u\n",captureLabel,unsigned(count));capture(captureLabel);}
  if(returned && local==300){
   capture("returned");
   if(visits<visitCount){played=false;returned=false;local=0;resultAt=0;}
   else {std::printf("smoke_memory_reload: PASS native rules/return visits=%u\n",visits);return 0;}
  }
  ++local;
 }
 std::puts("smoke_memory FAILED timeout");return 1;
}
