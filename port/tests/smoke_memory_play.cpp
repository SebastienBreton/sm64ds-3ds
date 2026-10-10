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
 unsigned playStage=0,attempt=0,phase=0,stageAt=0;unsigned chosen[2]={};
 
 const unsigned visitCount=std::getenv("SM64DS_MEMORY_RELOAD")?2:1;
 std::int16_t pcm[2186];
 for(unsigned frame=0;frame<10000;++frame){
  const char* captureLabel=nullptr;
  ntr::gx_begin_frame();auto*touch=func_0203dabc(func_0203da9c());std::memset(touch,0,8);
  *reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=0;
  if(scene==1 && !entered && local>=700 && local<705){touch[0]=222;touch[1]=166;touch[2]=1;}
  if(scene==5 && !played && local>=150 && local<155){touch[0]=85;touch[1]=65;touch[2]=1;}
  if(scene==5 && local>=100 && local<105){touch[0]=24;touch[1]=121;touch[2]=1;}
  if(scene==362 && data_ov004_020beb68){
   auto* game=data_ov004_020beb68;
   const int state=*reinterpret_cast<int*>(game+0x5314),sub=*reinterpret_cast<int*>(game+0x5318);
   const int coins=*reinterpret_cast<int*>(game+0xa8),score=*reinterpret_cast<int*>(game+0xb4);
   const unsigned pairs=game[0x5337],misses=game[0x533a];
   // Read current card values only to choose real touch inputs. No RNG or
   // game state is written; native and DS deals are deliberately independent.
   if(local>=400 && (playStage==0 || playStage==2)){
    if(phase==0){
     bool ready=state==2 && game[0x5338]==0;
     for(unsigned i=0;i<12;++i){auto* c=game+0x51a8+i*24;if(c[19] && c[20]!=2)ready=false;}
     if(!ready)goto memory_input_done;
     bool found=false;
     const bool same=playStage==0 && attempt!=0;
     for(unsigned i=0;i<12 && !found;++i){auto* c=game+0x51a8+i*24;if(!c[19])continue;
      for(unsigned j=i+1;j<12;++j){auto* d=game+0x51a8+j*24;
       if(d[19] && ((c[16]==d[16])==same)){chosen[0]=i;chosen[1]=j;found=true;break;}
      }
     }CHECK(found);
     std::printf("memory attempt stage=%u n=%u pair=%u,%u same=%u coins=%d\n",playStage,attempt,chosen[0],chosen[1],same,coins);
    }
    if(phase<3 || (phase>=38 && phase<41)){
     auto* c=game+0x51a8+chosen[phase<3?0:1]*24;
     touch[0]=*reinterpret_cast<int*>(c)>>12;touch[1]=*reinterpret_cast<int*>(c+4)>>12;touch[2]=1;
    }
    if(++phase==100){
     std::printf("memory outcome stage=%u n=%u pairs=%u misses=%u state=%d score=%d coins=%d\n",playStage,attempt,pairs,misses,state,score,coins);
     if(playStage==0){CHECK(misses==1);CHECK(pairs==attempt);}
     else {CHECK(pairs==0);CHECK(misses==attempt+1);}
     ++attempt;phase=0;
     if((playStage==0 && attempt==5)||(playStage==2 && attempt==3)){
      captureLabel=playStage==0?"won":"lost-start";++playStage;stageAt=local;attempt=0;
     }
    }
   }else if(playStage==1 && state==2){
    CHECK(pairs==0 && misses==0 && coins==18 && score==1);
    captureLabel="won-next-round";playStage=2;phase=0;
   }else if(playStage==3 && state==3 && sub==3 && *reinterpret_cast<unsigned short*>(game+0x5322)==0){
    CHECK(coins==18 && score==0 && misses==3);captureLabel="lost";
    playStage=4;stageAt=local;
   }else if(playStage==4){
    if(local<stageAt+4){touch[0]=128;touch[1]=96;touch[2]=1;}
    if(state==2){CHECK(coins==15 && score==0 && pairs==0);CHECK(*reinterpret_cast<int*>(game+0xb8)==1);
     captureLabel="continued";playStage=5;stageAt=local;
    }
   }else if(playStage==5){
    if(local==stageAt+1)*reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=1;
    if(local==stageAt+90)captureLabel="rules";
    if(local>=stageAt+110 && local<stageAt+115){touch[0]=128;touch[1]=96;touch[2]=1;resultAt=local;}
   }
  }
memory_input_done:
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
   else {std::printf("smoke_memory_play: PASS win/loss/gains/return visits=%u\n",visits);return 0;}
  }
  ++local;
 }
 std::puts("smoke_memory FAILED timeout");return 1;
}
