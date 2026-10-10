// Retail wrench options through native touch input and scheduler.
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
extern unsigned char data_0209d4b4,data_0208ee3c;
extern unsigned char data_020a4c64[],data_020a4c70[];
const char* sm64ds_sseq_error();
}
static unsigned diagnosticFrame;
extern "C" void __real_Snd_SendCommand(int,int,int,int,int);
extern "C" void __wrap_Snd_SendCommand(int command,int a,int b,int c,int d){
 if(command==0 && diagnosticFrame>=1198){
  auto* bank=reinterpret_cast<unsigned*>(d);
  std::printf("sound start frame=%u player=%d events=%08x offset=%x bank=%08x wave=%08x,%08x,%08x,%08x\n",
    diagnosticFrame,a,unsigned(b),unsigned(c),unsigned(d),bank[6],bank[8],bank[10],bank[12]);
  unsigned size=bank[2];if(size>=0x38 && size<0x100000){auto*f=std::fopen("settings-sound-bank.bin","wb");if(f){std::fwrite(bank,1,size,f);std::fclose(f);}}
 }
 __real_Snd_SendCommand(command,a,b,c,d);
}
#define CHECK(c) do{if(!(c)){std::printf("minigame menu FAILED line%d: %s\n",__LINE__,#c);return 1;}}while(0)
static void capture(const char* label){
 for(unsigned e=0;e<2;++e){static ntr::Framebuffer fb;ntr::ppu_scanout_scene(ntr::Engine(e),fb);
  char name[96];std::snprintf(name,sizeof name,"menu-settings-%s-%u.bmp",label,e);ntr::ppu_write_bmp(name,fb);}
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(!std::getenv("SM64DS_SAVE_PATH"))setenv("SM64DS_SAVE_PATH","sdmc:/sm64ds/menu-settings-validation.sav",1);
 CHECK(ntr::io_init());CHECK(Heap::SetupRootHeap());
 CHECK(data_020a4c70==data_020a4c64+12);
 *reinterpret_cast<unsigned*>(data_0209ee90+0x44)=0x100;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();data_020a0eac=data_020a0ea0;
 __sinit_02073a24();__sinit_02074da8();__sinit_02074e84();func_0201a4e4();
 func_020133bc();Sound::LoadInitialGroup(0);func_0201fe08();
 CHECK(sm64ds_try_load_overlay(7)==OVERLAY_OK);
 data_02092664=0x187;data_02092660=1;CHECK(func_02043098(1,0,0,2));
 struct Action{unsigned frame,x,y;const char* label;unsigned mode;int light;};
 const Action actions[]={
  {700,236,20,"opened",0,-1},
  {900,240,40,"headphones",1,-1},
  {1050,240,40,"stereo",2,-1},
  {1200,204,90,"off",2,0},
  {1350,140,90,"on",2,1},
  {1500,124,163,"closed",2,1},
  {1750,236,20,"reopened",2,1}};
 std::int16_t pcm[2186];
 for(unsigned frame=0;frame<1950;++frame){
  diagnosticFrame=frame;
  ntr::gx_begin_frame();auto*touch=func_0203dabc(func_0203da9c());std::memset(touch,0,8);
  *reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24)=0;
  for(const auto&a:actions)if(frame>=a.frame && frame<a.frame+5){touch[0]=a.x;touch[1]=a.y;touch[2]=1;}
  func_02019390();func_0203bb60();func_0203bc7c();func_02023498();
  CHECK(data_02092660);
  func_02044120();func_02019404();++data_020a0db0;
  func_020190b8();func_02019144();func_02019100();func_020132d8();
  CHECK(func_0205b070(1));while(func_0205b274(0)){}
  if(!sm64ds_native_sound_render(pcm,((frame+1)*32768/30)-(frame*32768/30))){std::printf("sound render refused frame=%u: %s\n",frame,sm64ds_sseq_error());return 1;}
  for(const auto&a:actions)if(frame==a.frame+100){
   capture(a.label);
   std::printf("settings %s mode=%u backlight=%u\n",a.label,data_0209d4b4,data_0208ee3c);
   CHECK(data_0209d4b4==a.mode);if(a.light>=0)CHECK(data_0208ee3c==a.light);
  }
 }
 std::puts("smoke_menu_settings: PASS native sound/backlight options and reopen");return 0;
}
