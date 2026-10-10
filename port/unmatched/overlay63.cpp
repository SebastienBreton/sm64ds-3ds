// Native Boo/haunted actor overlay with original constructor/resource lifetime.
#include "unmatched/actor_registry.h"
#include <cstring>
#include <cstdint>
extern "C" {
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
extern unsigned char sm64ds_overlay63_bss_begin[],sm64ds_overlay63_bss_end[];
extern unsigned sm64ds_overlay63_ids[];
extern void* sm64ds_overlay63_profiles[];
void __sinit_ov063_0211e29c();void __sinit_ov063_0211e3cc();
void __sinit_ov063_0211e590();void __sinit_ov063_0211e5fc();
}
namespace {bool loaded=false;void* previous[10];}
extern "C" bool sm64ds_load_overlay63(){
 if(loaded)return false;
 if(reinterpret_cast<uintptr_t>(sm64ds_overlay63_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay63_bss_begin)!=544)return false;
 std::memset(sm64ds_overlay63_bss_begin,0,544);
 const int scope=sm64ds_set_overlay_scope(63);
 __sinit_ov063_0211e29c();__sinit_ov063_0211e3cc();__sinit_ov063_0211e590();__sinit_ov063_0211e5fc();
 sm64ds_set_overlay_scope(scope);
 for(unsigned i=0;i<10;++i){previous[i]=data_020a4bb8[sm64ds_overlay63_ids[i]];data_020a4bb8[sm64ds_overlay63_ids[i]]=sm64ds_overlay63_profiles[i];}
 loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay63(){
 if(!loaded)return false;
 sm64ds_destroy_overlay_resources(63);
 for(unsigned i=0;i<10;++i)data_020a4bb8[sm64ds_overlay63_ids[i]]=previous[i];
 loaded=false;return true;
}

// Cross-overlay dependency used by the King Boo state. Original ov55 helper
// 02111264 sets mirror-Luigi's fade request and plays the native appearance SFX.
// It does not make overlay55 loadable; that overlay still needs its full actors.
extern "C" {
extern unsigned data_ov055_02111b68,data_ov055_02111b6c;
unsigned short func_0201277c(int);
__attribute__((weak)) void func_ov055_02111264(){data_ov055_02111b68=2;func_0201277c(0x179);}
}
