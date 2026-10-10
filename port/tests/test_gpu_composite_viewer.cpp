#include "gpu_composite_viewer.h"
#include "cpu_mode.h"
#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static ntr::PpuPreparedScene a,b;
static ntr::Framebuffer front_a,front_b,gx_left,gx_right,upper_left,upper_right,lower;
static uint32_t diagnostic[256*192],native_left[256*192],native_right[256*192];
static uint32_t lcd_before[3][400*240],lcd_after[400*240];
static const V3dsVert vertices[]={
    {-1,-1,.2,1,1,.2,.1,1,0,0},{1,-1,.2,1,.1,1,.2,1,0,0},{0,1,.2,1,.2,.1,1,1,0,0},
    {-.8,-.4,-.2,1,1,1,.1,1,0,0},{.9,-.4,-.2,1,.1,1,1,1,0,0},{.2,.9,-.2,1,1,.1,1,1,0,0}};
static const V3dsBatch batches[]={
    {nullptr,0,0,0,0,3,false,0x001f00c0},
    {nullptr,0,0,0,3,3,false,0x001000c0}};
static bool compare(const char* stage,const uint32_t* got,const uint32_t* expected,unsigned count,bool rgb=false){
    unsigned errors=0;
    const uint32_t mask=rgb?0xffffffu:0xffffffffu;
    for(unsigned i=0;i<count;++i)if((got[i]&mask)!=(expected[i]&mask)){
        if(errors<4)std::printf("Mismatch %s pixel=%u got=%08lx expected=%08lx\n",stage,i,(unsigned long)got[i],(unsigned long)expected[i]);
        ++errors;
    }
    std::printf("Resident viewer oracle %s mismatches=%u\n",stage,errors);return !errors;
}
int main(){
    gfxInitDefault();std::freopen("sdmc:/sm64ds/log.txt","w",stdout);std::setvbuf(stdout,nullptr,_IONBF,0);
    v3ds_auto();sm64ds_configure_cpu_mode();v3ds_renderer_init();
    for(unsigned i=0;i<256*192;++i){
        const unsigned x=i%256,y=i/256;
        front_a.px[y][x]=0xff000000u|((x*13&255)<<16)|((y*7&255)<<8)|((x+y*17)&255);
        front_b.px[y][x]=0xff000000u|((y*19&255)<<16)|((x*7&255)<<8)|((x*11+y)&255);
        a.front_layer[i]=1+i%5;a.front_priority[i]=a.front_layer[i]==5?4:(i/5)%4;
        b.front_layer[i]=1+i%5;b.front_priority[i]=b.front_layer[i]==5?4:(i/5)%4;
        a.window_mask[i]=b.window_mask[i]=(i/23)&63;
    }
    const unsigned modes[]={0,2,3};
    for(unsigned configuration=0;configuration<12;++configuration){
        const bool stereo=configuration&1,a_upper=(configuration&2)!=0;
        setenv("SM64DS_FORCE_3D",stereo?"1":"0",1);unsetenv("PLAYER_GPU_BLIT");
        a.has_geometry=true;a.geometry_priority=configuration%4;
        a.blend=(modes[configuration%3]<<6)|((configuration&4)?63:17);
        a.brightness=configuration*7%32;a.master=((configuration%3)<<14)|(configuration*11%32);
        a.has_windows=configuration&4;
        b.blend=(modes[(configuration+1)%3]<<6)|63;b.brightness=configuration*13%32;
        b.master=(((configuration+1)%3)<<14)|(configuration*17%32);
        b.has_windows=configuration&4;b.has_geometry=false;
        if(!v3ds_gpu_geometry(vertices,6,batches,2,&gx_left.px[0][0],&gx_right.px[0][0],1,true,stereo&&a_upper)){std::puts("FAIL");return 1;}
        const auto upper=a_upper?ntr::PpuPreparedCandidates{a,front_a}:ntr::PpuPreparedCandidates{b,front_b};
        const auto bottom=a_upper?ntr::PpuPreparedCandidates{b,front_b}:ntr::PpuPreparedCandidates{a,front_a};
        ntr::ppu_compose_candidates(upper,gx_left,upper_left);
        ntr::ppu_compose_candidates(upper,gx_right,upper_right);
        ntr::ppu_compose_candidates(bottom,gx_left,lower);
        v3ds_present_native(&upper_left.px[0][0],&upper_right.px[0][0],&lower.px[0][0],stereo&&a_upper);
        // Compute the exact CPU nearest-neighbour pixel mapping. gfxGetFramebuffer
        // returns the next writable buffer after a swap, which can be older than
        // the displayed frame, so it is not the reference for this changing test.
        for(unsigned panel=0;panel<3;++panel){
            const unsigned width=panel==2?320:400;
            const auto* image=panel==0?&upper_left.px[0][0]:panel==1?&upper_right.px[0][0]:&lower.px[0][0];
            for(unsigned y=0;y<240;++y)for(unsigned x=0;x<width;++x)
                lcd_before[panel][y*width+x]=image[(y*192/240)*256+x*256/width]|0xff000000u;
        }
        if(!v3ds_gpu_present_candidates(vertices,6,batches,2,{a,front_a},{b,front_b},a_upper,1,true,stereo)){std::puts("FAIL");return 1;}
        v3ds_gpu_candidate_geometry(native_left,native_right);
        if(!compare("geometry-left",native_left,&gx_left.px[0][0],256*192) ||
           !compare("geometry-right",native_right,&gx_right.px[0][0],256*192)){std::puts("FAIL");return 1;}
        v3ds_gpu_candidate_panel(0,diagnostic);
        if(!compare("compose-upper-left",diagnostic,&upper_left.px[0][0],256*192,true)){std::puts("FAIL");return 1;}
        if(stereo&&a_upper){v3ds_gpu_candidate_panel(1,diagnostic);
            if(!compare("compose-upper-right",diagnostic,&upper_right.px[0][0],256*192,true)){std::puts("FAIL");return 1;}}
        v3ds_gpu_candidate_panel(2,diagnostic);
        if(!compare("compose-lower",diagnostic,&lower.px[0][0],256*192,true)){std::puts("FAIL");return 1;}
        // Fill both alternating LCD buffers with the same tested frame.
        if(!v3ds_gpu_present_candidates(vertices,6,batches,2,{a,front_a},{b,front_b},a_upper,1,true,stereo)){std::puts("FAIL");return 1;}
        v3ds_gpu_candidate_geometry(native_left,native_right);
        v3ds_read_top(lcd_after);
        if(!compare("LCD-upper-left",lcd_after,lcd_before[0],400*240)){std::puts("FAIL");return 1;}
        if(stereo&&a_upper){v3ds_read_top(lcd_after,true);
            if(!compare("LCD-upper-right",lcd_after,lcd_before[1],400*240)){std::puts("FAIL");return 1;}}
        v3ds_read_bottom(lcd_after);
        if(!compare("LCD-lower",lcd_after,lcd_before[2],320*240)){std::puts("FAIL");return 1;}
        std::printf("Resident viewer configuration=%u stereo=%d a_upper=%d PASS\n",configuration,stereo&&a_upper,a_upper);
    }
    // Keep three cases separate: ordinary opaque, uniform fade, and selective
    // brightness with windows. This measures presentation only, not game logic.
    for(unsigned variant=0;variant<3;++variant){
        setenv("SM64DS_FORCE_3D","1",1);
        a.geometry_priority=1;a.has_geometry=true;b.has_geometry=false;
        a.has_windows=b.has_windows=variant==2;
        a.blend=b.blend=variant==0?0:(variant==1?((2<<6)|63):((3<<6)|17));
        a.brightness=b.brightness=7;a.master=b.master=0;
        double phase[2][3]{};
        for(unsigned implementation=0;implementation<2;++implementation){
            for(unsigned frame=0;frame<70;++frame){
                const double t0=v3ds_time_ms();double t1=t0,t2=t0,t3=t0;
                if(implementation==0){
                    v3ds_gpu_geometry(vertices,6,batches,2,&gx_left.px[0][0],&gx_right.px[0][0],1,true,true);
                    t1=v3ds_time_ms();
                    ntr::ppu_compose_candidates({a,front_a},gx_left,upper_left);
                    ntr::ppu_compose_candidates({a,front_a},gx_right,upper_right);
                    ntr::ppu_compose_candidates({b,front_b},gx_left,lower);t2=v3ds_time_ms();
                    v3ds_present_native(&upper_left.px[0][0],&upper_right.px[0][0],&lower.px[0][0],true);t3=v3ds_time_ms();
                }else{
                    if(!v3ds_gpu_present_candidates(vertices,6,batches,2,{a,front_a},{b,front_b},true,1,true,true)){std::puts("FAIL");return 1;}
                    t1=t2=t3=v3ds_time_ms();
                }
                if(frame>=10){phase[implementation][0]+=t1-t0;phase[implementation][1]+=t2-t1;phase[implementation][2]+=t3-t2;}
            }
        }
        std::printf("Resident A/B presentation only case=%u 60samples: legacy geometry+read=%.6f PPU=%.6f LCD=%.6f total=%.6f candidate combined=%.6f ms/frame\n",variant,
            phase[0][0]/60,phase[0][1]/60,phase[0][2]/60,
            (phase[0][0]+phase[0][1]+phase[0][2])/60,phase[1][0]/60);
    }
    v3ds_exit();std::puts("PASS");return 0;
}
