// Real menu -> Loves Me -> result -> Quit -> reload, through native profiles.
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
const char* sm64ds_sseq_error();
}
#define CHECK(c) do{if(!(c)){std::printf("minigame menu FAILED line%d: %s\n",__LINE__,#c);return 1;}}while(0)
static void capture(const char* label){
 for(unsigned e=0;e<2;++e){static ntr::Framebuffer fb;ntr::ppu_scanout_scene(ntr::Engine(e),fb);
  char name[96];std::snprintf(name,sizeof name,"flower-%s-%u.bmp",label,e);ntr::ppu_write_bmp(name,fb);}
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(!std::getenv("SM64DS_SAVE_PATH"))setenv("SM64DS_SAVE_PATH","sdmc:/sm64ds/flower-validation.sav",1);
 CHECK(ntr::io_init());CHECK(Heap::SetupRootHeap());
 *reinterpret_cast<unsigned*>(data_0209ee90+0x44)=0x100;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();data_020a0eac=data_020a0ea0;
 __sinit_02073a24();__sinit_02074da8();__sinit_02074e84();func_0201a4e4();
 func_020133bc();Sound::LoadInitialGroup(0);func_0201fe08();
 CHECK(sm64ds_try_load_overlay(7)==OVERLAY_OK);
 data_02092664=0x187;data_02092660=1;CHECK(func_02043098(1,0,0,2));
 unsigned scene=1,ov=7,local=0,visits=0,resultAt=0,petalX=0,petalY=0;bool entered=false,returned=false,played=false;
 const bool petalTrace=false;
 unsigned round=1,playStart=300;bool win=false,loss=false,lossStreak=false;

 const unsigned visitCount=1;
 std::int16_t pcm[2186];
 for(unsigned frame=0;frame<24000;++frame){
  const char* captureLabel=nullptr;
  ntr::gx_begin_frame();auto*touch=func_0203dabc(func_0203da9c());std::memset(touch,0,8);
  *reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=0;
  if(scene==1 && !entered && local>=700 && local<705){touch[0]=222;touch[1]=166;touch[2]=1;}
  if(scene==5 && !played && local>=150 && local<155){touch[0]=134;touch[1]=65;touch[2]=1;}
  if(scene==390 && data_ov004_020beb68){
   auto* game=data_ov004_020beb68;
   const int state=*reinterpret_cast<int*>(game+0x5fe8);
   const int left=*reinterpret_cast<int*>(game+0x5fd8);
   if(local==120){CHECK(state==0);CHECK(left>0 && left<=22);captureLabel="initial";}
   if(local==130)*reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=1;
   if(local==190)captureLabel="rules";
   if(local==210)*reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=2;
   if(local==270)captureLabel="rules-return";
   if(resultAt && state==0){++round;resultAt=0;playStart=local+60;CHECK(left>0);std::printf("flower restart round=%u petals=%d score=%d\n",round,left,*reinterpret_cast<int*>(game+0x5ff0));}
   if(local>=playStart && !resultAt && state==0 && left>0){
    const unsigned phase=(local-playStart)%35;
    if(phase==0){
     bool found=false;
     for(unsigned i=0;i<22;++i){auto* p=game+0x4f38+i*32;if(p[0] && !p[1] && !p[2]){
      petalX=*reinterpret_cast<int*>(p+4)>>12;petalY=*reinterpret_cast<int*>(p+8)>>12;
      std::printf("flower pull local=%u left=%d petal=%u xy=%u,%u\n",local,left,i,petalX,petalY);found=true;break;
     }}CHECK(found);CHECK(petalX<256 && petalY<192);
    }
    if(phase<5){touch[0]=petalX;touch[1]=petalY;touch[2]=1;}
    else if(phase<10){touch[0]=16;touch[1]=180;touch[2]=1;}
   }
   if(state==1 && !resultAt){
    CHECK(left==0);resultAt=local;
    const int toggle=game[0x5fcc];lossStreak|=*reinterpret_cast<int*>(game+0x5fe0)>=3;win|=toggle==1;loss|=toggle==0;
    std::printf("flower outcome round=%u toggle=%d wins=%d losses=%d\n",round,toggle,*reinterpret_cast<int*>(game+0x5fdc),*reinterpret_cast<int*>(game+0x5fe0));
    std::printf("flower result visit=%u local=%u score=%d\n",visits,local,*reinterpret_cast<int*>(game+0x5ff0));
   }
   if(resultAt){
    if(local==resultAt+200)captureLabel=round==1?"result1":round==2?"result2":round==3?"result3":round==4?"result4":"result5";
    if(!lossStreak && round<22 && local>=resultAt+240 && local<resultAt+245){touch[0]=58;touch[1]=162;touch[2]=1;}
    if((lossStreak || round>=22) && local==resultAt+240)*reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=8;
    if((lossStreak || round>=22) && local>=resultAt+300 && local<resultAt+305){touch[0]=128;touch[1]=96;touch[2]=1;}
   }
  }
  func_02019390();func_0203bb60();func_0203bc7c();func_02023498();
  if(!data_02092660){
   unsigned next=data_02092664;std::printf("minigame transition %u->%u frame=%u\n",scene,next,frame);
   CHECK((scene==1 && next==5 && !entered)||(scene==5 && next==390)||(scene==390 && next==5 && resultAt));
   if(scene==390){CHECK(sm64ds_try_unload_overlay(6)==OVERLAY_OK);CHECK(sm64ds_try_unload_overlay(4)==OVERLAY_OK);}
   else CHECK(sm64ds_try_unload_overlay(ov)==OVERLAY_OK);
   scene=next;
   if(scene==390){CHECK(sm64ds_try_load_overlay(4)==OVERLAY_OK);CHECK(sm64ds_try_load_overlay(6)==OVERLAY_OK);++visits;played=true;}
   else {ov=5;CHECK(sm64ds_try_load_overlay(ov)==OVERLAY_OK);if(played)returned=true;}
   CHECK(dScene_c::SpawnIfNecessary());local=0;entered=true;
  }
  func_02044120();func_02019404();++data_020a0db0;
  func_020190b8();func_02019144();func_02019100();func_020132d8();
  CHECK(func_0205b070(1));while(func_0205b274(0)){}
  if(!sm64ds_native_sound_render(pcm,((frame+1)*32768/30)-(frame*32768/30))){std::printf("sound refused: %s\n",sm64ds_sseq_error());return 1;}
  if(petalTrace && scene==390 && local>=160){
   const unsigned step=local-160;
   if(step==1 || step==10 || step==30 || step==60 || step==100 || step==150 || step==179 || step==181){
    char label[40];std::snprintf(label,sizeof label,"petal-%03u",step);capture(label);
    char file[64];std::snprintf(file,sizeof file,"flower-petal-%03u.bin",step);FILE* f=std::fopen(file,"wb");CHECK(f);std::fwrite(data_ov004_020beb68,1,0x6000,f);std::fclose(f);
   }
   if(step==185){std::puts("smoke_flower: PASS petal trace");return 0;}
  }
  if(captureLabel){std::size_t count=0;ntr::gx_polygons(count);std::printf("flower capture %s triangles=%u\n",captureLabel,unsigned(count));capture(captureLabel);}
  if(returned && local==300){
   capture("returned");
   if(visits<visitCount){played=false;returned=false;local=0;resultAt=0;}
   else {CHECK(win && loss && lossStreak);std::printf("smoke_flower_loss_streak: PASS native result/return visits=%u\n",visits);return 0;}
  }
  ++local;
 }
 std::puts("smoke_flower FAILED timeout");return 1;
}
