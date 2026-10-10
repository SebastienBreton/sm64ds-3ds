#include "unmatched/actor_registry.h"
// Native lifecycle for the six overlay85 actor families. Actors must be gone
// before unloading; shared-handle destructors do not release live actor refs.
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_overlay85_bss_begin[],sm64ds_overlay85_bss_end[];
extern unsigned char g_profile_KINOPIO[],g_profile_PEACH_PRINCESS[],g_profile_MIP[];
extern unsigned char g_profile_OBJ_MIP_KEY[],g_profile_C_JUGEM[],g_profile_KANBAN[];
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
void __sinit_ov085_0212f2a8();
void __sinit_ov085_0212f3a0();
void __sinit_ov085_0212f5ec();
void __sinit_ov085_0212f9bc();
void __sinit_ov085_0212fa40();
void __sinit_ov085_0212fdb0();
}
namespace {
bool loaded=false;
const unsigned ids[]={185,186,187,229,235,183};
void* const profiles[]={g_profile_KINOPIO,g_profile_PEACH_PRINCESS,g_profile_MIP,
    g_profile_OBJ_MIP_KEY,g_profile_C_JUGEM,g_profile_KANBAN};
void* previous_profiles[6];
}
extern "C" bool sm64ds_load_overlay85() {
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay85_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay85_bss_begin);
    if(bytes!=1024)return false;
    std::memset(sm64ds_overlay85_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(85);
    __sinit_ov085_0212f2a8();__sinit_ov085_0212f3a0();__sinit_ov085_0212f5ec();
    __sinit_ov085_0212f9bc();__sinit_ov085_0212fa40();__sinit_ov085_0212fdb0();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<6;++i) {previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay85() {
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(85);
    for(unsigned i=0;i<6;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
