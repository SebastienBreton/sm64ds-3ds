#include "gpu_composite_viewer.h"
#include "native_obj_merge_fixtures.h"
#include "cpu_mode.h"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {u32 __stacksize__=1024*1024;extern const uint8_t native_obj_merge_expected[];}
static NativeObjFixture fixture;
static NativeObjBackground background;
static ds_gpu::ObjPlan plan;
static ntr::PpuPreparedScene a,b;
static ntr::Framebuffer gx_left,gx_right,a_left,a_right;
static uint32_t expected[256*192],got[400*240],reference[400*240];
static const V3dsVert vertices[]={
    {-1,-1,.2,1,1,.2,.1,1,0,0},{1,-1,.2,1,.1,1,.2,1,0,0},{0,1,.2,1,.2,.1,1,1,0,0}};
static const V3dsBatch batch[]={ {nullptr,0,0,0,0,3,false,0x001f00c0} };
static bool equal(const char* name,const uint32_t* pixels,const uint32_t* wanted,unsigned count){
    unsigned errors=0;
    for(unsigned i=0;i<count;++i)if((pixels[i]&0xffffff)!=(wanted[i]&0xffffff)){
        if(errors<3)std::printf("OBJ viewer %s mismatch=%u got=%08lx expected=%08lx\n",name,i,
            static_cast<unsigned long>(pixels[i]),static_cast<unsigned long>(wanted[i]));
        ++errors;
    }
    if(errors)std::printf("OBJ viewer %s errors=%u\n",name,errors);
    return errors==0;
}
static void unpack(unsigned id){
    const auto* packed=native_obj_merge_expected+id*(1024+256*192);
    const auto* colors=reinterpret_cast<const uint32_t*>(packed);
    for(unsigned i=0;i<256*192;++i)expected[i]=colors[packed[1024+i]];
}
int main(){
    gfxInitDefault();std::freopen("sdmc:/sm64ds/log.txt","w",stdout);std::setvbuf(stdout,nullptr,_IONBF,0);
    v3ds_auto();sm64ds_configure_cpu_mode();v3ds_renderer_init();
    a.has_geometry=true;a.geometry_priority=1;
    for(unsigned i=0;i<256*192;++i){
        (&a.front.px[0][0])[i]=0xff285068;a.front_layer[i]=5;a.front_priority[i]=4;a.window_mask[i]=63;
    }
    for(unsigned id=0;id<128;++id){
        fixture.setup(id%64);
        if(id>=64)for(unsigned i=0;i<128;++i)
            if(fixture.oam[i*8+1]&0x20)fixture.oam[i*8+4]^=1;
        if(!plan.prepare(fixture.input)){std::puts("FAIL");return 1;}
        background.setup(id,b);unpack(id);
        const bool a_upper=(id&2)!=0,stereo=(id&1)!=0;
        setenv("SM64DS_FORCE_3D",stereo?"1":"0",1);
        if(!v3ds_gpu_geometry(vertices,3,batch,1,&gx_left.px[0][0],&gx_right.px[0][0],1,true,stereo&&a_upper)){
            std::puts("FAIL");return 1;
        }
        ntr::ppu_compose_candidates({a,a.front},gx_left,a_left);
        ntr::ppu_compose_candidates({a,a.front},gx_right,a_right);
        // Reject and use the full CPU fallback, then return to resident. The
        // expected B pixels already include all DS OBJ/fade/master effects.
        if(id%8==0){
            b.has_windows=true;
            if(v3ds_gpu_present_candidates(vertices,3,batch,1,{a,a.front},{b,b.front},a_upper,1,true,stereo,nullptr,&plan)){
                std::puts("FAIL windows admitted");return 1;
            }
            b.has_windows=false;
            v3ds_present_native(a_upper?&a_left.px[0][0]:expected,
                a_upper?&a_right.px[0][0]:expected,a_upper?expected:&a_left.px[0][0],stereo&&a_upper);
        }
        for(unsigned repeat=0;repeat<2;++repeat){
            if(!v3ds_gpu_present_candidates(vertices,3,batch,1,{a,a.front},{b,b.front},a_upper,1,true,stereo,nullptr,&plan)){
                std::printf("FAIL viewer fixture=%u\n",id);return 1;
            }
            // Explicit diagnostic readback drains the queued frame. Normal
            // presentation has no readback; this is solely a fidelity gate.
            v3ds_gpu_candidate_geometry(&gx_left.px[0][0],&gx_right.px[0][0]);
        }
        for(unsigned panel=0;panel<3;++panel){
            if(panel==1 && !(stereo&&a_upper))continue;
            const bool objects=panel==2?a_upper:!a_upper;
            const auto* pixels=objects?expected:(panel==1?&a_right.px[0][0]:&a_left.px[0][0]);
            v3ds_gpu_candidate_panel(panel,got);
            if(!equal("panel",got,pixels,256*192)){std::printf("FAIL id=%u panel=%u\n",id,panel);return 1;}
            const unsigned width=panel==2?320:400;
            for(unsigned y=0;y<240;++y)for(unsigned x=0;x<width;++x)
                reference[y*width+x]=pixels[(y*192/240)*256+x*256/width];
            if(panel==2)v3ds_read_bottom(got);else v3ds_read_top(got,panel==1);
            if(!equal("LCD",got,reference,width*240)){std::printf("FAIL id=%u LCD=%u\n",id,panel);return 1;}
        }
        if(id%32==31)v3ds_gpu_candidate_reset();
        std::printf("OBJ viewer fixture=%u a_upper=%u stereo=%u PASS\n",id,a_upper,stereo&&a_upper);
    }
    // Stop directly after a native presentation, while GPU resources exist.
    fixture.setup(32);plan.prepare(fixture.input);background.setup(32,b);
    if(!v3ds_gpu_present_candidates(vertices,3,batch,1,{a,a.front},{b,b.front},true,1,true,true,nullptr,&plan)){
        std::puts("FAIL");return 1;
    }
    v3ds_exit();std::puts("OBJ viewer128 panel+LCD, fallback16, bound-shader reset4, queued stop PASS");
    std::puts("PASS");return 0;
}
