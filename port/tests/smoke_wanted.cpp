// Bounded Wanted vertical slice: native menu, game factory and return.
// Uses the production overlay registry for the Wanted-only overlay6 closure.
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "ntr/ppu.h"
#include "Heap.h"
#include "unmatched/actor_registry.h"
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
extern void* data_ov004_020beb68;
extern void* data_020a0ea0;extern void* data_020a0eac;
extern unsigned short data_02092664;extern unsigned char data_02092660;
extern unsigned char data_0209ee90[],data_020a1162[];
extern int data_020a0db0,data_0208a170;
int func_02054d88();
}
#define CHECK(c) do{if(!(c)){std::printf("minigame menu FAILED line%d: %s\n",__LINE__,#c);return 1;}}while(0)
static void capture(const char* label){
 for(unsigned e=0;e<2;++e){static ntr::Framebuffer fb;ntr::ppu_scanout_scene(ntr::Engine(e),fb);
  char name[96];std::snprintf(name,sizeof name,"minigame-menu-%s-%u.bmp",label,e);ntr::ppu_write_bmp(name,fb);}
}
static void dumpRegion(const char* name,unsigned address,unsigned size){
 auto*f=std::fopen(name,"wb");if(!f)return;
 std::fwrite(ntr::host_ptr(address),1,size,f);std::fclose(f);
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(!std::getenv("SM64DS_SAVE_PATH"))setenv("SM64DS_SAVE_PATH","sdmc:/sm64ds/wanted-validation.sav",1);
 CHECK(ntr::io_init());setvbuf(stdout,nullptr,_IONBF,0);CHECK(Heap::SetupRootHeap());
 *reinterpret_cast<unsigned*>(data_0209ee90+0x44)=0x100;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();data_020a0eac=data_020a0ea0;
 __sinit_02073a24();__sinit_02074da8();__sinit_02074e84();func_0201a4e4();
 func_020133bc();Sound::LoadInitialGroup(0);func_0201fe08();
 CHECK(sm64ds_try_load_overlay(7)==OVERLAY_OK);
 data_02092664=0x187;data_02092660=1;CHECK(func_02043098(1,0,0,2));
 unsigned scene=1,ov=7,local=0;bool entered=false,returned=false,played=false;
 const bool earn=std::getenv("SM64DS_WANTED_HIT")!=nullptr;
 unsigned hitFrame=0,hitX=0,hitY=0,entries=0;bool earned=false;
 const unsigned visits=std::getenv("SM64DS_WANTED_RELOAD")?2:1;
 std::int16_t pcm[2186];
 for(unsigned frame=0;frame<5000;++frame){
  ntr::gx_begin_frame();auto*touch=func_0203dabc(func_0203da9c());std::memset(touch,0,8);
  *reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=0;
  if(scene==1 && !entered && local>=700 && local<705){touch[0]=222;touch[1]=166;touch[2]=1;}
  if(scene==5 && !played && local>=150 && local<155){touch[0]=85;touch[1]=65;touch[2]=1;}
  if(scene==366){
   if(earn && data_ov004_020beb68){
    auto* game=static_cast<unsigned char*>(data_ov004_020beb68);
    if(!hitFrame && *reinterpret_cast<int*>(game+0x4f78)==2){
     unsigned target=game[0x5456];CHECK(target>0 && target<=120);--target;
     hitX=*reinterpret_cast<int*>(game+0x47f8+target*4)>>12;
     hitY=(*reinterpret_cast<int*>(game+0x49d8+target*4)>>12)-4;
     hitFrame=local;std::printf("Wanted touch target=%u x=%u y=%u frame=%u\n",target,hitX,hitY,local);
    }
    if(hitFrame && local<hitFrame+5){touch[0]=hitX;touch[1]=hitY;touch[2]=1;}
    if(game[0x5457]==1)earned=true;
   }
   if(local==650)*reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=8;
   if(local>=720 && local<725){touch[0]=128;touch[1]=96;touch[2]=1;}
  }
  func_02019390();func_0203bb60();func_0203bc7c();func_02023498();
  if(!data_02092660){
   unsigned next=data_02092664;std::printf("minigame transition %u->%u frame=%u\n",scene,next,frame);
   CHECK((scene==1 && next==5 && !entered)||(scene==5 && next==366)||(scene==366 && next==5));
   if(scene==366){
    CHECK(sm64ds_try_unload_overlay(6)==OVERLAY_OK);
    CHECK(sm64ds_try_unload_overlay(4)==OVERLAY_OK);
   }else CHECK(sm64ds_try_unload_overlay(ov)==OVERLAY_OK);
   scene=next;
   if(scene==366){
    CHECK(sm64ds_try_load_overlay(4)==OVERLAY_OK);
    CHECK(sm64ds_try_load_overlay(6)==OVERLAY_OK);played=true;++entries;
   }else {ov=5;CHECK(sm64ds_try_load_overlay(ov)==OVERLAY_OK);if(played)returned=true;}
   CHECK(dScene_c::SpawnIfNecessary());local=0;entered=true;
  }
  func_02044120();func_02019404();++data_020a0db0;
  func_020190b8();func_02019144();func_02019100();func_020132d8();
  CHECK(func_0205b070(1));while(func_0205b274(0)){}
  CHECK(sm64ds_native_sound_render(pcm,((frame+1)*32768/30)-(frame*32768/30)));
  if((scene==5 || scene==366) && (local==120 || local==600 || local==700)){
   char name[40];std::snprintf(name,sizeof name,"scene-%u-%u",scene,local);capture(name);
   std::printf("minigame tab frame=%u group=%d\n",local,data_0208a170);
   if(scene==366)std::printf("Wanted display=%08x bg3=%04x chars=%08x WIN=%04x,%04x,%04x,%04x earned=%u\n",
    unsigned(NTR_MMIO(unsigned,0x04000000)),unsigned(NTR_MMIO(unsigned short,0x0400000e)),unsigned(func_02054d88()),
    unsigned(NTR_MMIO(unsigned short,0x04000040)),unsigned(NTR_MMIO(unsigned short,0x04000044)),
    unsigned(NTR_MMIO(unsigned short,0x04000048)),unsigned(NTR_MMIO(unsigned short,0x0400004a)),earned);
   if(scene==366 && local==600){
    dumpRegion("wanted-io.bin",0x04000000,0x2000);
    dumpRegion("wanted-vram-a.bin",0x06000000,0x40000);
    dumpRegion("wanted-palette.bin",0x05000000,0x800);
    dumpRegion("wanted-oam.bin",0x07000000,0x800);
    unsigned original=NTR_MMIO(unsigned,0x04000000);
    NTR_MMIO(unsigned,0x04000000)=original&~(1u<<10);
    capture("diagnostic-bg2-disabled");
    NTR_MMIO(unsigned,0x04000000)=original;
   }
  }
  if(scene==366 && local==120){
   CHECK(data_ov004_020beb68);
   CHECK(static_cast<unsigned char*>(data_ov004_020beb68)[0x5457]==0);
  }
  if(returned && local==300){
   CHECK(!earn || earned);capture("returned");
   if(entries<visits){
    std::printf("Wanted reload after visit=%u, prior earned=%u\n",entries,earned);
    played=false;returned=false;local=0;hitFrame=0;earned=false;
   }else {std::printf("smoke_wanted: PASS native Wanted visits=%u and Rec Room return\n",entries);return 0;}
  }
  ++local;
 }
 std::puts("minigame menu FAILED transition timeout");return 1;
}
