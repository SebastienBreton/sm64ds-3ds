// Overlay4 is the full retail base; overlay6 imports the shared manifest entries.
// The scene scheduler must finish object destruction before these unload calls.
#include "unmatched/actor_registry.h"
#include "menu/minigame_imports.h"
#include <cstdio>
extern "C" {
bool sm64ds_reset_minigame_storage(unsigned);
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
void __sinit_ov004_020b948c();void __sinit_ov004_020b955c();
void __sinit_ov004_020b9ad0();void __sinit_ov004_020b9b24();
#define SM64DS_MINIGAME(actor,factory,profile,rom_factory,vtable,name) void* factory();
#define SM64DS_MINIGAME_INIT(function,address) void function();
#include "menu/minigame_imports.inc"
#undef SM64DS_MINIGAME_INIT
#undef SM64DS_MINIGAME
extern unsigned short data_02092664;
}
namespace {
bool baseLoaded, wantedLoaded;
struct Profile {void*(*factory)();unsigned short behavior,render;};
static_assert(sizeof(Profile)==8,"retail actor profile ABI");
struct ImportedProfile {unsigned actor;const char* name;Profile profile;void* previous;};
ImportedProfile imported[]={
#define SM64DS_MINIGAME(actor,factory,profile,rom_factory,vtable,name) {actor,name,{factory,actor,actor},nullptr},
#define SM64DS_MINIGAME_INIT(function,address)
#include "menu/minigame_imports.inc"
#undef SM64DS_MINIGAME_INIT
#undef SM64DS_MINIGAME
};
void(*const initializers[])()={
#define SM64DS_MINIGAME(actor,factory,profile,rom_factory,vtable,name)
#define SM64DS_MINIGAME_INIT(function,address) function,
#include "menu/minigame_imports.inc"
#undef SM64DS_MINIGAME_INIT
#undef SM64DS_MINIGAME
};
}
extern "C" bool sm64ds_load_minigame_base_overlay(){
 if(baseLoaded || !sm64ds_reset_minigame_storage(4))return false;
 int previous=sm64ds_set_overlay_scope(4);
 // Exact constructor order verified against the ROM overlay4 table.
 __sinit_ov004_020b948c();__sinit_ov004_020b955c();
 __sinit_ov004_020b9ad0();__sinit_ov004_020b9b24();
 sm64ds_set_overlay_scope(previous);baseLoaded=true;return true;
}
extern "C" bool sm64ds_load_wanted_overlay(){
 if(!baseLoaded || wantedLoaded)return false;
 if(!sm64ds_minigame_imported(data_02092664)){
  std::fprintf(stderr,"overlay6: unsupported mini-game actor %u; imported:",data_02092664);
  for(const auto& item:imported)std::fprintf(stderr," %s(%u)",item.name,item.actor);
  std::fputc('\n',stderr);
  return false;
 }
 if(!sm64ds_reset_minigame_storage(6))return false;
 int previous=sm64ds_set_overlay_scope(6);
 // The manifest preserves retail constructor order; Flower initializes lazily.
 for(auto initialize:initializers)initialize();sm64ds_set_overlay_scope(previous);
 for(auto& item:imported){item.previous=data_020a4bb8[item.actor];data_020a4bb8[item.actor]=&item.profile;}
 wantedLoaded=true;return true;
}
extern "C" bool sm64ds_unload_wanted_overlay(){
 if(!wantedLoaded)return false;
 sm64ds_destroy_overlay_resources(6);
 for(auto& item:imported)data_020a4bb8[item.actor]=item.previous;
 wantedLoaded=false;return true;
}
extern "C" bool sm64ds_unload_minigame_base_overlay(){
 if(!baseLoaded || wantedLoaded)return false;
 sm64ds_destroy_overlay_resources(4);baseLoaded=false;return true;
}
