// overlay3 scenes: STAR_SELECT (4) and GAME_OVER (8). Compiler owns vtables.
#include "unmatched/actor_registry.h"
extern "C" {
extern unsigned char g_profile_STAR_SELECT[];
void* dScGameOver_c_classInit();
unsigned sm64ds_destroy_overlay_resources(int);
}
namespace {
bool loaded;void* previous;void* previous_game_over;
// Original ov3 GAME_OVER scene profile 0x020b1750: {classInit, behavior 8, render 11}.
struct SceneProfile{void*(*create)();unsigned short behavior,render;};
static_assert(sizeof(SceneProfile)==8,"original scene profile ABI");
SceneProfile game_over={dScGameOver_c_classInit,8,11};
}
extern "C" bool sm64ds_load_star_select_overlay(){
 if(loaded)return false;
 previous=data_020a4bb8[4];data_020a4bb8[4]=g_profile_STAR_SELECT;
 previous_game_over=data_020a4bb8[8];data_020a4bb8[8]=&game_over;
 loaded=true;return true;
}
extern "C" bool sm64ds_unload_star_select_overlay(){
 if(!loaded)return false;
 sm64ds_destroy_overlay_resources(3);
 data_020a4bb8[4]=previous;data_020a4bb8[8]=previous_game_over;loaded=false;return true;
}
