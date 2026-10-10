#include "viewer_3ds.h"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
static uint32_t left[256*192],right[256*192];
static const V3dsVert triangle[]={
    {-.8f,-.8f,0,1,1,0,0,1,0,0},{.8f,-.8f,0,1,1,0,0,1,0,0},{0,.8f,0,1,1,0,0,1,0,0}};
static const V3dsBatch batch={nullptr,0,0,0,0,3,false};
int main(){
    gfxInitDefault();v3ds_auto();v3ds_renderer_init();
    for(const char* slider:{"0","1"}){
        setenv("SM64DS_FORCE_3D",slider,1);
        if(!v3ds_gpu_geometry(triangle,3,&batch,1,left,right,1,true,true))return 1;
        if(!left[96*256+128]){std::puts("FAIL: setup geometry not visible");return 1;}
        double time[2]{};
        for(unsigned mode=0;mode<2;++mode){
            if(mode==0)setenv("PLAYER_EMPTY_GEOMETRY_GPU","1",1);
            else unsetenv("PLAYER_EMPTY_GEOMETRY_GPU");
            const double start=v3ds_time_ms();
            for(unsigned frame=0;frame<30;++frame){
                if(!v3ds_gpu_geometry(nullptr,0,nullptr,0,left,right,1,true,true))return 1;
                for(unsigned pixel=0;pixel<256*192;++pixel)
                    if(left[pixel] || right[pixel]){std::puts("FAIL: empty geometry differs from transparent clear");return 1;}
                if(v3ds_native_stereo()!=(slider[0]=='1')){std::puts("FAIL: stereo gating changed");return 1;}
            }
            time[mode]=(v3ds_time_ms()-start)/30;
        }
        std::printf("Empty geometry slider=%s ms/frame: GPU=%.3f transparent=%.3f\n",slider,time[0],time[1]);
    }
    v3ds_exit();gfxExit();std::puts("PASS");
}
