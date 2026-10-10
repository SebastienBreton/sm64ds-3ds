#pragma once
// Read geometry policy once before original game/GX setup.
#include "ntr/gx_direct.h"
#include <cstdlib>
#include <cstring>
void player_direct_configure(bool reuse_between_presentations);
namespace player_geometry {
inline bool enabled(const char* value){return !value || std::strcmp(value,"0")!=0;}
inline void startup(){
    const bool direct=enabled(std::getenv("PLAYER_DIRECT_ARENA"));
    const bool retained=direct && enabled(std::getenv("PLAYER_DIRECT_ARENA_REUSE"));
    ntr::gx_direct_configure(direct);
    player_direct_configure(retained);
}
}
