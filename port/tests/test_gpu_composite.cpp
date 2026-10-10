#include "gpu_composite.h"
#include "cpu_mode.h"
#include "native_readback.h"
#include "fps_gpu_composite_shader.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

static uint32_t input[256*192],actual[256*192],expected[256*192];
static ntr::PpuPreparedScene scene;
static ntr::Framebuffer candidate_front,geometry_cpu,oracle;
static bool equal(const char* phase){
    unsigned failures=0;
    for(unsigned i=0;i<256*192;++i)if(actual[i]!=expected[i]){
        if(failures<8)std::printf("Mismatch %s x=%u y=%u got=%08lx expected=%08lx\n",phase,i%256,i/256,(unsigned long)actual[i],(unsigned long)expected[i]);
        ++failures;
    }
    std::printf("GPU oracle %s mismatches=%u\n",phase,failures);
    return !failures;
}
static unsigned brightness(unsigned q,unsigned mode,unsigned amount,bool master=false){
    const unsigned bias=master?(mode==2?0:15):(mode==2?8:7);
    if(mode==2)return q+(((63-q)*amount+bias)>>4);
    if(mode==3)return q-((q*amount+bias)>>4);
    return q;
}
static void apply_table(const uint8_t table[256]){
    for(unsigned i=0;i<256*192;++i){
        auto c=input[i];expected[i]=0xff000000u|(unsigned(table[(c>>16)&255])<<16)|
            (unsigned(table[(c>>8)&255])<<8)|table[c&255];
    }
}
int main(){
    gfxInitDefault();
    std::freopen("sdmc:/sm64ds/log.txt","w",stdout);
    std::setvbuf(stdout,nullptr,_IONBF,0);
    sm64ds_configure_cpu_mode();
    if(!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE*4)){std::puts("FAIL");return 1;}
    ds_gpu::Composite composite;
    ds_gpu::Surface first,second,third;
    C3D_Tex source{};
    auto* scratch=static_cast<uint32_t*>(linearAlloc(256*256*4));
    const bool a=first.init(),b=second.init(),c=third.init();
    const bool d=C3D_TexInit(&source,256,256,GPU_RGBA8);
    const bool e=composite.init(fps_gpu_composite_shader,sizeof(fps_gpu_composite_shader));
    std::printf("GPU init scratch=%d targets=%d/%d/%d source=%d shader=%d\n",scratch!=nullptr,a,b,c,d,e);
    if(!scratch || !a || !b || !c || !d || !e){std::puts("FAIL");return 1;}
    C3D_TexSetFilter(&source,GPU_NEAREST,GPU_NEAREST);
    C3D_TexSetWrap(&source,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x)
        input[y*256+x]=0xff000000u|((x&255)<<16)|(((x+y)&255)<<8)|((255-x+y)&255);
    ds_gpu::Composite::upload(source,input);
    C3D_FrameBegin(0);composite.copy(first,source);C3D_FrameEnd(0);
    ds_gpu::Composite::readback(first,actual,scratch);
    std::memcpy(expected,input,sizeof(input));
    if(!equal("upload-copy")){std::puts("FAIL");return 1;}
    C3D_FrameBegin(0);composite.copy(second,first.texture,true);C3D_FrameEnd(0);
    ds_gpu::Composite::readback(second,actual,scratch);
    if(!equal("resident-copy")){std::puts("FAIL");return 1;}
    uint8_t table[256];
    unsigned tests=0;
    const uint64_t started=svcGetSystemTick();
    for(unsigned mode: {0u,2u,3u})for(unsigned amount=0;amount<=16;++amount){
        // Includes a second DS master-brightness rounding, rather than folding
        // both adjustments into one approximate GPU multiplication.
        for(unsigned c=0;c<256;++c){
            unsigned q=brightness(c>>2,mode,amount);
            q=brightness(q,mode==2?3:2,(amount*7)%17,true);
            table[c]=(q<<2)|(q>>4);
        }
        composite.channel_table(table);apply_table(table);
        C3D_FrameBegin(0);composite.map(second,source);C3D_FrameEnd(0);
        ds_gpu::Composite::readback(second,actual,scratch);
        char phase[80];std::snprintf(phase,sizeof(phase),"DS-channel mode=%u amount=%u",mode,amount);
        if(!equal(phase)){std::puts("FAIL");return 1;}
        // Repeat the same table with a texture produced by GPU, no upload or
        // readback between original geometry texture and the mapping draw.
        C3D_FrameBegin(0);composite.map(third,first.texture,true);C3D_FrameEnd(0);
        ds_gpu::Composite::readback(third,actual,scratch);
        if(!equal("DS-resident-channel")){std::puts("FAIL");return 1;}
        tests+=2;
    }
    for(unsigned c=0;c<256;++c)table[c]=std::min(c,127u);
    composite.channel_table(table);apply_table(table);
    C3D_FrameBegin(0);composite.map(first,source);C3D_FrameEnd(0);
    ds_gpu::Composite::readback(first,actual,scratch);
    if(!equal("intermediate-integer-0-127")){std::puts("FAIL");return 1;}
    C3D_FrameBegin(0);composite.add(second,first.texture,first.texture);C3D_FrameEnd(0);
    for(auto& c:expected)c=0xff000000u|std::min<uint32_t>(((c>>16)&255)*2,255u)<<16|
        std::min<uint32_t>(((c>>8)&255)*2,255u)<<8|std::min<uint32_t>((c&255)*2,255u);
    ds_gpu::Composite::readback(second,actual,scratch);
    if(!equal("integer-add-0-254")){std::puts("FAIL");return 1;}
    std::printf("GPU channel oracle PASS %u full frames (includes CPU/GPU texture orientations, three independent channels, effect+master rounding, integer ADD); total diagnostic time=%.3f ms\n",tests+4,(svcGetSystemTick()-started)*1000.0/SYSCLOCK_ARM11);
    ds_gpu::Surface blend_scratch[3];
    for(auto& surface:blend_scratch)if(!surface.init()){std::puts("FAIL");return 1;}
    auto expand=[](unsigned q){return (q<<2)|(q>>4);};
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
        const unsigned a=x&63;
        input[y*256+x]=0xff000000u|(expand(a)<<16)|(expand((a*13)&63)<<8)|expand(63-a);
    }
    ds_gpu::Composite::upload(source,input);
    C3D_FrameBegin(0);composite.copy(first,source);C3D_FrameEnd(0);
    // Wait before reusing the upload texture while the previous draw reads it.
    C3D_FrameBegin(0);C3D_FrameEnd(0);
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
        const unsigned b=(y*4+(x>>6))&63;
        input[y*256+x]=0xff000000u|(expand(b)<<16)|(expand(63-b)<<8)|expand((b*17)&63);
    }
    ds_gpu::Composite::upload(source,input);
    C3D_FrameBegin(0);composite.copy(second,source);C3D_FrameEnd(0);
    for(unsigned eva=0;eva<=16;++eva)for(unsigned evb=0;evb<=16;++evb){
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
            unsigned a=x&63,b=(y*4+(x>>6))&63;
            auto value=[&](unsigned front,unsigned back){return expand(std::min(63u,(front*eva+back*evb+8)>>4));};
            expected[y*256+x]=0xff000000u|(value(a,b)<<16)|(value((a*13)&63,63-b)<<8)|value(63-a,(b*17)&63);
        }
        C3D_FrameBegin(0);composite.blend(third,first.texture,second.texture,eva,evb,blend_scratch);C3D_FrameEnd(0);
        ds_gpu::Composite::readback(third,actual,scratch);
        char phase[80];std::snprintf(phase,sizeof(phase),"DS-alpha eva=%u evb=%u",eva,evb);
        if(!equal(phase)){std::puts("FAIL");return 1;}
    }
    std::puts("GPU alpha oracle PASS all4096six-bit pairs x289coefficients x3channels (nine resident GPU passes, one diagnostic readback)");
    for(auto& surface:blend_scratch)surface.destroy();
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
        const unsigned i=y*256+x;
        input[i]=(x<<24)|(((x*13+y)&255)<<16)|(((x+y*7)&255)<<8)|((x*17+y*11)&255);
        geometry_cpu.px[y][x]=decode_native_pixel((input[i]<<8)|(input[i]>>24));
        candidate_front.px[y][x]=0xff000000u|(((x*7+y*3)&255)<<16)|(((x*19+y)&255)<<8)|((x+y*23)&255);
        scene.front.px[y][x]=0xffbaddadu; // Explicit candidate view must win.
        scene.front_layer[i]=1+(i%5);
        scene.front_priority[i]=scene.front_layer[i]==5?4:(i/5)%4;
        scene.window_mask[i]=(i/17)&63;
    }
    ds_gpu::Composite::upload(source,input);
    unsigned opaque_tests=0;
    for(unsigned configuration=0;configuration<240;++configuration){
        const unsigned mode_list[]={0,2,3};
        const unsigned targets[]={0,1,8,16,32,63};
        scene.geometry_priority=configuration%4;
        const unsigned mode=mode_list[(configuration/4)%3];
        scene.blend=(mode<<6)|targets[(configuration/12)%6];
        scene.master=(((configuration/12)%4)<<14)|((configuration*7)%32);
        scene.brightness=(configuration*13)%32;
        scene.has_windows=(configuration/24)%2;
        scene.has_geometry=configuration%13!=0;
        scene.blank=configuration%47==0;
        scene.resolved=true; // Explicit view ignores legacy cache resolution.
        const bool apply_master=configuration%7!=0;
        ntr::ppu_compose_candidates({scene,candidate_front},geometry_cpu,oracle,apply_master);
        for(unsigned i=0;i<256*192;++i)expected[i]=reinterpret_cast<uint32_t*>(&oracle)[i]|0xff000000u;
        C3D_FrameBegin(0);
        if(!composite.opaque(third,{scene,candidate_front},source,false,apply_master)){std::puts("FAIL");return 1;}
        C3D_FrameEnd(0);ds_gpu::Composite::readback(third,actual,scratch);
        char phase[80];std::snprintf(phase,sizeof(phase),"PPU-opaque configuration=%u",configuration);
        if(!equal(phase)){std::puts("FAIL");return 1;}
        ++opaque_tests;
    }
    scene.has_obj_alpha=true;
    if(composite.opaque(third,{scene,candidate_front},source)){std::puts("FAIL: OBJ-alpha fallback missing");return 1;}
    scene.has_obj_alpha=false;scene.blend=1<<6;
    if(composite.opaque(third,{scene,candidate_front},source)){std::puts("FAIL: alpha mode fallback missing");return 1;}
    scene.has_geometry=true;scene.has_windows=true;
    for(unsigned mode=0;mode<4;++mode)for(unsigned target=0;target<6;++target){
        scene.blend=(mode<<6)|(1u<<(target+8));
        if(composite.opaque(third,{scene,candidate_front},source)){std::puts("FAIL: forced native-3D alpha fallback missing");return 1;}
    }
    std::printf("GPU opaque oracle PASS %u frames against production PPU RGB, windows/OBJ tie/geometry alpha threshold/all channels/layer and master brightness/raw blank/explicit candidate view, fallback guards\n",opaque_tests);
    scene.blank=false;scene.has_obj_alpha=false;scene.has_geometry=true;
    scene.has_windows=true;scene.master=0;scene.blend=(2<<6)|17;scene.brightness=7;
    for(unsigned mutation=0;mutation<192;++mutation){
        const unsigned i=(mutation*7919)%(256*192);
        switch(mutation%6){
        case 0: reinterpret_cast<uint32_t*>(&candidate_front)[i]^=0x00f703a1;break;
        case 1: scene.front_layer[i]=1+(scene.front_layer[i]%5);break;
        case 2: scene.front_priority[i]=(scene.front_priority[i]+1)%5;break;
        case 3: scene.window_mask[i]^=33;break;
        case 4: scene.geometry_priority=(scene.geometry_priority+1)%4;break;
        case 5: scene.blend^=16;scene.master^=0x4007;break;
        }
        ntr::ppu_compose_candidates({scene,candidate_front},geometry_cpu,oracle);
        for(unsigned pixel=0;pixel<256*192;++pixel)expected[pixel]=reinterpret_cast<uint32_t*>(&oracle)[pixel]|0xff000000u;
        C3D_FrameBegin(0);composite.opaque(third,{scene,candidate_front},source);C3D_FrameEnd(0);
        ds_gpu::Composite::readback(third,actual,scratch);
        char phase[80];std::snprintf(phase,sizeof(phase),"exact-cache mutation=%u",mutation);
        if(!equal(phase)){std::puts("FAIL");return 1;}
    }
    std::puts("GPU exact input cache PASS 192 in-place color/layer/priority/window/blend/master mutations against production PPU");
    composite.destroy();first.destroy();second.destroy();third.destroy();
    C3D_TexDelete(&source);linearFree(scratch);C3D_Fini();gfxExit();
    std::puts("PASS");return 0;
}
