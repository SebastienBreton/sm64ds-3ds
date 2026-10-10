#include "platform/3ds/player_geometry.h"
#include <cstdio>
static bool arena=false,reuse=false;
namespace ntr {void gx_direct_configure(bool value){arena=value;}}
void player_direct_configure(bool value){reuse=value;}
int main(){
    const char* values[]={nullptr,"0","1"};
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b){
        if(values[a])setenv("PLAYER_DIRECT_ARENA",values[a],1);else unsetenv("PLAYER_DIRECT_ARENA");
        if(values[b])setenv("PLAYER_DIRECT_ARENA_REUSE",values[b],1);else unsetenv("PLAYER_DIRECT_ARENA_REUSE");
        player_geometry::startup();
        const bool expected=a!=1,held=expected && b!=1;
        if(arena!=expected || reuse!=held)return 1;
    }
    std::puts("PASS startup absent/0/1 in 9 combinations; arena off disables retained");
}
