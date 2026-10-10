#include "native_mesh_api.h"
#include "ntr/gx_gpu_geometry.h"
extern "C" void native_mesh_driver_reset();
extern "C" void bmd_compiled_scene_reset();
extern "C" void native_mesh_camera_begin_update();
extern "C" void native_mesh_driver_stats();
extern "C" void native_mesh_print_stats();
extern "C" void native_mesh_camera_stats();
// Native interactive development entry. No scripted input or automatic save.
// Linked against the same original-game objects as the integration probes.
// Bounded original scene handoff; overlay2 remains statically resident.
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "platform/3ds/player_geometry.h"
#include "platform/3ds/observed_cadence.h"
#include "ntr/ppu.h"
#include "Heap.h"
#include "G2x.h"
#include "dScene_c.h"
#include "Stage.h"
#include "Player.h"
#include "unmatched/actor_registry.h"
#include "unmatched/native_overlay.h"
#include "audio/sound_native.h"
#include "menu/minigame_imports.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace Sound { void LoadInitialGroup(int); }
extern "C" {
void func_02059bc0();void __sinit_02075154();void __sinit_0207501c();
void __sinit_02073a24();void __sinit_02074da8();void __sinit_02074e0c();void __sinit_02074e84();
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
void sm64ds_init_common_level_actor_profiles();
void sm64ds_overlay2_snapshot();unsigned sm64ds_overlay2_reload();
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

#include "menu/menu.h"
#include "ntr/rt.h"
#include "platform/3ds/viewer_3ds.h"
// DS and libctru spell these typedefs differently.
#define u32 CtrU32
#define s32 CtrS32
#define vu32 CtrVU32
#define vs32 CtrVS32
#define Profile CtrProfile
#include <3ds.h>
#undef Profile
#undef vs32
#undef vu32
#undef s32
#undef u32
extern bool player_present(bool,bool);
extern "C" {
void func_020197b8();
void __real_func_02019390();
int __real__ZN8dScene_c16SpawnIfNecessaryEv();
unsigned char data_0209e64c=0; // HID is published directly before each original update.
unsigned char data_0208f274=1; // Original ARM9 ROM byte.
}
static bool failed;
static unsigned current_scene=1;
static bool stage_initialized;
static unsigned updates;
static unsigned input_events;
static bool trace_input;
static unsigned title_menu_phase;
extern "C" unsigned char* data_ov007_0210342c;
extern "C" int __real_func_ov007_020b7090(unsigned short,unsigned short,unsigned short,unsigned short,int);
extern "C" int __wrap_func_ov007_020b7090(unsigned short x,unsigned short y,unsigned short pressed,unsigned short invalid,int keys){
 if(trace_input){
  static unsigned previous_x,previous_y,previous_pressed,previous_invalid;static int previous_keys;
  if(x!=previous_x || y!=previous_y || pressed!=previous_pressed || invalid!=previous_invalid || keys!=previous_keys){
   std::printf("Game menu input update=%u touch=%u,%u,%u,%u keys=%04x\n",updates,unsigned(x),unsigned(y),unsigned(pressed),unsigned(invalid),unsigned(keys));
   previous_x=x;previous_y=y;previous_pressed=pressed;previous_invalid=invalid;previous_keys=keys;
  }
 }
 const int result=__real_func_ov007_020b7090(x,y,pressed,invalid,keys);
 if(trace_input && data_ov007_0210342c){
  // The original menu context stores a state-object pointer at +8.
  // The retail fresh-cart journey is star(11), logo transition(10), menu(0).
  const auto* state=*reinterpret_cast<short**>(data_ov007_0210342c+8);
  if(state){
   static int previous_state=-1;
   if(*state!=previous_state){
    std::printf("Game menu state update=%u state=%d\n",updates,int(*state));
    previous_state=*state;
   }
   if((title_menu_phase==0 && *state==11) ||
      (title_menu_phase==1 && *state==10) ||
      (title_menu_phase==2 && *state==0))++title_menu_phase;
  }
 }
 return result;
}
#define REQUIRE(c) do { if(!(c)){std::fprintf(stderr,"FATAL: game startup: %s\n",#c);return 1;} } while(0)
extern "C" void sm64ds_fs_prefetch_scene(unsigned scene); // hal/fs.cpp: background prefetch order hint
static void require_runtime(bool ok,const char* operation) {
 if(!ok){std::fprintf(stderr,"FATAL: game runtime: %s (scene=%u next=%u)\n",operation,current_scene,data_02092664);std::abort();}
}
static bool minigame(unsigned id){return sm64ds_minigame_imported(id);}
extern "C" int __wrap__ZN8dScene_c16SpawnIfNecessaryEv(){
 if(!data_02092660 && data_02092664!=0x187){
  unsigned next=data_02092664;
  if(!(next==1 || next==3 || next==4 || next==5 || next==8 || minigame(next))){
   // Scenes the 3DS port does not provide must not abort a player-reachable
   // path: VS / DS Download Play (6), the VS result scene (7), the boot logo
   // scene (2), and Rec Room mini-games whose overlay-6 code is not imported
   // yet (menu/minigame_imports.inc). A mini-game picked in the Rec Room
   // returns to the Rec Room; anything else returns to the title menu (as
   // the DS does when a VS session ends).
   const bool rec_room=current_scene==5 && next>=361 && next<=396;
   std::printf("Game scene %u not supported on 3DS (%s); returning to the %s\n",next,
    rec_room?"mini-game not imported yet":"VS/wireless",rec_room?"Rec Room":"title menu");
   data_02092664=next=rec_room?5:1;
   if(current_scene==next){ // fresh overlay image, like the DS x->6->x path
    const unsigned overlay=next==5?5:7;
    require_runtime(sm64ds_try_unload_overlay(overlay)==OVERLAY_OK,"unload scene overlay");
    require_runtime(sm64ds_try_load_overlay(overlay)==OVERLAY_OK,"load scene overlay");
   }
  }
  if(next!=current_scene){
   // Star Select (4) and Game Over (8) both live in overlay 3.
   const bool ov3_now=current_scene==4 || current_scene==8,ov3_next=next==4 || next==8;
   if(current_scene==1)require_runtime(sm64ds_try_unload_overlay(7)==OVERLAY_OK,"unload menu");
   if(ov3_now && !ov3_next)require_runtime(sm64ds_try_unload_overlay(3)==OVERLAY_OK,"unload star select");
   if(current_scene==5)require_runtime(sm64ds_try_unload_overlay(5)==OVERLAY_OK,"unload Rec Room");
   if(minigame(current_scene)){
    require_runtime(sm64ds_try_unload_overlay(6)==OVERLAY_OK,"unload mini-game");
    require_runtime(sm64ds_try_unload_overlay(4)==OVERLAY_OK,"unload mini-game base");
   }
   if(next==1)require_runtime(sm64ds_try_load_overlay(7)==OVERLAY_OK,"load menu");
   if(ov3_next && !ov3_now)require_runtime(sm64ds_try_load_overlay(3)==OVERLAY_OK,"load star select");
   if(next==5)require_runtime(sm64ds_try_load_overlay(5)==OVERLAY_OK,"load Rec Room");
   if(minigame(next)){
    require_runtime(sm64ds_try_load_overlay(4)==OVERLAY_OK,"load mini-game base");
    require_runtime(sm64ds_try_load_overlay(6)==OVERLAY_OK,"load mini-game");
   }
  }
  if(next==3 && !stage_initialized){
   const int previous=sm64ds_set_overlay_scope(2);
   __sinit_ov002_02100560();__sinit_ov002_02107298();__sinit_ov002_021019d0();
   __sinit_ov002_02106e40();__sinit_ov002_021071f4();sm64ds_init_common_level_actor_profiles();
   __sinit_ov002_02101738();__sinit_ov002_02100adc();__sinit_ov002_02100d44();
   __sinit_ov002_0210804c();sm64ds_set_overlay_scope(previous);
   sm64ds_init_level_scripts();stage_initialized=true;
   sm64ds_overlay2_snapshot(); // DS overlay-2 reload image (overlay_destructors.cpp)
  }else if(next==3 && current_scene!=3)sm64ds_overlay2_reload(); // DS reloads overlay 2 here
  std::printf("Game scene transition %u -> %u\n",current_scene,next);
  native_mesh_driver_reset();ntr::gx_native_mesh_camera_cut();
  bmd_compiled_scene_reset(); // previous scene's compiled display lists never hit again
  ntr::gx_texture_cache_scene_trim(); // and its decoded textures
  current_scene=next;
  sm64ds_fs_prefetch_scene(next);
  v3ds_set_menu_presentation(current_scene==1); // DS 4:3 dual-screen layout in the menu only.
 }
 return __real__ZN8dScene_c16SpawnIfNecessaryEv();
}
extern "C" void __wrap_func_02019390(){
 const char* mesh_option=std::getenv("PLAYER_NATIVE_MESH");
 native_mesh_camera_begin_update();
 // Explicit activation; Stage admission still checks every packet and display.
 ntr::gx_native_mesh_allow(mesh_option && std::strcmp(mesh_option,"0")!=0 && current_scene==3);
 // GPU geometry mode (gpu-geometry-menu): DS transform/lighting in the PICA
 // vertex shader for the title/main menu; SM64DS_GX_GPU=0 disables it.
 ntr::gx_gpu_configure(current_scene==1);
 ntr::gx_begin_frame();hidScanInput();
 require_runtime(sm64ds_menu_poll_input(),"local HID transfer");
 if(trace_input){
  touchPosition touch{};hidTouchRead(&touch);
  const unsigned held=hidKeysHeld();
  static unsigned previous_held,previous_x,previous_y;
  if(held!=previous_held || touch.px!=previous_x || touch.py!=previous_y){
   const auto* ds_touch=func_0203dabc(func_0203da9c());
   const auto keys=*reinterpret_cast<unsigned short*>(data_020a1162+func_0203da9c()*0x24);
   std::printf("Game HID update=%u held=%08x touch=%u,%u DS=%04x/%u,%u,%u\n",updates,held,
    unsigned(touch.px),unsigned(touch.py),unsigned(keys),unsigned(ds_touch[0]),unsigned(ds_touch[1]),unsigned(ds_touch[2]));
   if(held)++input_events;
   previous_held=held;previous_x=touch.px;previous_y=touch.py;
  }
 }
 // Direct menu entry omits the retail Nintendo-logo scene, which exits with
 // both engines faded to black (BLDCNT 0xff; melonDS reference). Restore it
 // after func_020197b8's initial +31 brighten, or the upper LCD stays white.
 if(!updates){
  G2x::SetBlendBrightness(reinterpret_cast<volatile unsigned short*>(0x04000050),0x3f,-16);
  G2x::SetBlendBrightness(reinterpret_cast<volatile unsigned short*>(0x04001050),0x3f,-16);
 }
 ++updates;__real_func_02019390();
}
extern "C" void __wrap_func_0203df40(){
 // A second transfer here would advance local input twice in one game tick.
 require_runtime(false,"unexpected duplicate input dispatcher");
}
extern "C" int __wrap_func_02030e4c(){
 require_runtime(false,"wireless/reset dispatcher not yet integrated");return 0;
}
// Core-1 worker: GPU submission of the previous frame overlaps the next game
// update, and the sound mixer runs beside the game like the DS ARM7.
bool player_present_pipeline_enable(bool (*)(),bool);void player_present_pipeline_stop_audio();
bool player_present_pipeline_audio_ok();void player_present_pipeline_drain();void player_present_pipeline_stats();
static bool pipeline;
static unsigned present_failures;
static bool frame_hook(uint64_t frame){
 if(!aptMainLoop())return false;
 // Sound maintenance must precede the next game update. If the mixer is
 // rendering a chunk, present first and wait afterwards (gpu-geometry-menu).
 bool sound_pending=false;
 if(pipeline){bool done=false;
  if(!(sm64ds_native_sound_try_maintenance(done) && player_present_pipeline_audio_ok())){
   std::fprintf(stderr,"FATAL: audio: %s\n",sm64ds_native_sound_output_error());failed=true;return false;
  }
  sound_pending=!done;
 }else if(!sm64ds_native_sound_output_pump()){
  std::fprintf(stderr,"FATAL: audio: %s\n",sm64ds_native_sound_output_error());failed=true;return false;
 }
 const char* capture=std::getenv("SM64DS_GAME_CAPTURE_FRAME");
 const bool capturing=capture && frame==std::strtoull(capture,nullptr,10);
 if(capturing){
  size_t count=0;const auto* triangles=ntr::gx_polygons(count);
  std::printf("Game capture polygons=%u powcnt=%04x display=%08x/%08x blend=%04x/%04x\n",
   unsigned(count),unsigned(ntr::io_read(0x04000304,2)),unsigned(ntr::io_read(0x04000000,4)),
   unsigned(ntr::io_read(0x04001000,4)),unsigned(ntr::io_read(0x04000050,2)),unsigned(ntr::io_read(0x04001050,2)));
  // Keep diagnostics bounded; small startup models fit entirely in this log.
  for(size_t i=0;i<count && i<64;++i){
   const auto& t=triangles[i];
   std::printf("Game polygon %u attr=%08x texture=%dx%d colors=%08x,%08x,%08x\n",
    unsigned(i),t.polygon_attr,t.tw,t.th,t.v[0].color,t.v[1].color,t.v[2].color);
  }
 }
 // Real-time catch-up: when presentation fell behind (slow GPU/CPU frames on
 // hardware), skip presenting up to 3 consecutive frames so the game logic and
 // music keep their speed instead of running in slow motion. Never skipped:
 // diagnostic captures and frames with a DS display capture request.
 static unsigned consecutive_skips=0;
 const bool skip_present=!capturing && consecutive_skips<3 && observed_cadence::behind() &&
  !(ntr::io_read(0x04000064,4)&0x80000000u) && !ntr::io_display_capture();
 if(skip_present){++consecutive_skips;observed_cadence::note_skipped();present_failures=0;}
 else if(consecutive_skips=0,player_present(capturing,false))present_failures=0;
 else{
  // A refused presentation drops this frame; the game keeps running (a hard
  // exit here quit the game when the title-screen face was touched, see
  // build3ds/validation/game-coverage). Persistent failure still stops.
  if(++present_failures%60==1)std::fprintf(stderr,"Present failed frame=%llu scene=%u consecutive=%u (frame dropped)\n",(unsigned long long)frame,current_scene,present_failures);
  if(present_failures>=600){std::fprintf(stderr,"FATAL: presentation failed for 600 consecutive frames\n");failed=true;return false;}
 }
 if(sound_pending){
  const bool sound_ok=sm64ds_native_sound_maintenance();sm64ds_native_sound_retract();
  if(!sound_ok){std::fprintf(stderr,"FATAL: audio: %s\n",sm64ds_native_sound_output_error());failed=true;return false;}
 }
 observed_cadence::after_frame(frame);
 if(frame%120==0)std::printf("Game vblank=%llu updates=%u scene=%u\n",(unsigned long long)frame,updates,current_scene);
 return true;
}
int main(){
 observed_cadence::configure();
 player_geometry::startup();
 setvbuf(stdout,nullptr,_IONBF,0);
 if(!std::getenv("SM64DS_SAVE_PATH"))setenv("SM64DS_SAVE_PATH","sdmc:/sm64ds/game.sav",1);
 setenv("PLAYER_NATIVE_COMPOSITE","1",1);setenv("PLAYER_PREPARED_PPU","1",1);
 // The renderer admits supported states and retains the CPU capture/effects
 // fallback. Explicit zero overrides remain available for comparison runs.
 if(!std::getenv("PLAYER_RESIDENT_COMPOSITE"))setenv("PLAYER_RESIDENT_COMPOSITE","1",1);
 if(!std::getenv("PLAYER_RESIDENT_OBJECTS"))setenv("PLAYER_RESIDENT_OBJECTS","1",1);
 trace_input=std::getenv("SM64DS_GAME_TRACE_INPUT") || std::getenv("SM64DS_GAME_REQUIRE_INPUT") || std::getenv("SM64DS_GAME_REQUIRE_TITLE_MENU");
 REQUIRE(ntr::io_init());REQUIRE(Heap::SetupRootHeap());
 // Original boot func0201a054 sets this graph/render baseline before scenes.
 *reinterpret_cast<unsigned*>(data_0209ee90+0x44)=0x100;
 // Retail CRT runs this guard constructor before any guarded sound/card work.
 // A zero-filled guard otherwise restores false and arms a zero-time alarm.
 func_02059bc0();__sinit_02074e0c();__sinit_02075154();__sinit_0207501c();data_020a0eac=data_020a0ea0;
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
 REQUIRE(sm64ds_try_load_overlay(7)==OVERLAY_OK);
 data_02092664=0x187;data_02092660=1;
 const auto menu=func_02043098(1,0,0,2);REQUIRE(menu);
 v3ds_set_menu_presentation(true);

 v3ds_renderer_init();
 REQUIRE(sm64ds_native_sound_output_open());
 {const char* option=std::getenv("SM64DS_PIPELINE");
  if(!option || std::strcmp(option,"0")!=0)pipeline=player_present_pipeline_enable(sm64ds_native_sound_output_pump,true);}
 const char* limit_text=std::getenv("SM64DS_GAME_FRAME_LIMIT");
 const uint64_t limit=limit_text?std::strtoull(limit_text,nullptr,10):0;
 std::printf("Native game started; save=%s; frame limit=%llu (0=unlimited)\n",std::getenv("SM64DS_SAVE_PATH"),(unsigned long long)limit);
 const auto frames=ntr::rt_run(func_020197b8,frame_hook,limit);
 observed_cadence::report();
 native_mesh_driver_stats();native_mesh_print_stats();native_mesh_camera_stats();
 if(pipeline){player_present_pipeline_stop_audio();player_present_pipeline_drain();player_present_pipeline_stats();}
 sm64ds_native_sound_output_close();v3ds_exit();
 if(std::getenv("SM64DS_GAME_REQUIRE_INPUT") && !input_events){std::puts("FATAL: no real HID input observed during validation");failed=true;}
 if(std::getenv("SM64DS_GAME_REQUIRE_TITLE_MENU") && title_menu_phase!=3){std::puts("FATAL: fresh-cart title/menu sequence 11->10->0 not observed");failed=true;}
 if(const char* expected=std::getenv("SM64DS_GAME_REQUIRE_FINAL_SCENE")){
  char* end=nullptr;const auto scene=std::strtoul(expected,&end,10);
  if(end==expected || *end || scene!=current_scene){
   std::fprintf(stderr,"FATAL: final scene=%u expected=%s\n",current_scene,expected);failed=true;
  }
 }
 std::printf("Native game stopped: frames=%llu updates=%u input_events=%u scene=%u failed=%u\n",(unsigned long long)frames,updates,input_events,current_scene,unsigned(failed));
 return failed || !frames;
}
