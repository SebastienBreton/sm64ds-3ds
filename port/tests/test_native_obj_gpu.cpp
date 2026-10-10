#include "native_obj_gpu.h"
#include "native_obj_fixtures.h"
#include "native_obj_merge.h"
#include "native_obj_merge_fixtures.h"
#include "cpu_mode.h"
#include "fps_gpu_composite_shader.h"
#include <cstdio>
#include <cstring>
#include <memory>

extern "C" const uint8_t native_obj_expected[];
extern "C" const uint8_t native_obj_merge_expected[];
static uint32_t actual[256*192],cpu[256*192];
static uint64_t now(){return svcGetSystemTick();}
static double ms(uint64_t ticks){return ticks*1000.0/SYSCLOCK_ARM11;}
int main(){
    gfxInitDefault();std::freopen("sdmc:/sm64ds/log.txt","w",stdout);
    std::setvbuf(stdout,nullptr,_IONBF,0);sm64ds_configure_cpu_mode();
    if(!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE*4)){std::puts("FAIL");return 1;}
    ds_gpu::NativeObjects renderer;ds_gpu::Surface output;
    auto fixture=std::make_unique<NativeObjFixture>();auto plan=std::make_unique<ds_gpu::ObjPlan>();
    auto* scratch=static_cast<uint32_t*>(linearAlloc(256*256*4));
    if(!scratch || !output.init() || !renderer.init(fps_gpu_composite_shader,sizeof(fps_gpu_composite_shader))){std::puts("FAIL init");return 1;}
    constexpr unsigned record_bytes=256*4+256*192;
    for(unsigned id=0;id<128;++id){
        fixture->setup(id%64);
        if(id>=64)for(unsigned i=0;i<128;++i)
            if(fixture->oam[i*8+1]&0x20)fixture->oam[i*8+4]^=1;
        if(!plan->prepare(fixture->input)){std::printf("FAIL planner fixture%u\n",id);return 1;}
        C3D_FrameBegin(0);
        if(!renderer.draw(output,*plan)){std::printf("FAIL GPU draw fixture%u\n",id);return 1;}
        C3D_FrameEnd(0);ds_gpu::Composite::readback(output,actual,scratch);
        for(auto& c:actual){const auto alpha=c>>24;if(alpha==16 || alpha==32 || alpha==48 || alpha==64)c=(c&0xffffff)|((alpha/16)<<24);else if(alpha)c=0xffbadbad;}
        const auto* reference=native_obj_expected+id*record_bytes;
        const auto* dictionary=reinterpret_cast<const uint32_t*>(reference);
        const auto* indices=reference+1024;unsigned failures=0;
        for(unsigned i=0;i<256*192;++i)if(actual[i]!=dictionary[indices[i]]){
            if(failures<4)std::printf("mismatch fixture%u xy%u/%u got%08lx DS%08lx\n",id,i%256,i/256,(unsigned long)actual[i],(unsigned long)dictionary[indices[i]]);
            ++failures;
        }
        std::printf("Native OBJ GPU fixture=%u engine=%u sprites=%u mismatches=%u decoded=%llu\n",id,fixture->engine,plan->count,failures,(unsigned long long)renderer.decoded_textures());
        if(failures){if(auto* dump=std::fopen("sdmc:/sm64ds/native_obj_failure.bin","wb")){std::fwrite(actual,1,sizeof actual,dump);std::fclose(dump);}std::puts("FAIL");return 1;}
    }
    std::puts("Native OBJ GPU oracle PASS128 whole-image RGB/priority/opaque-attributes DS fixtures including even/odd8bpp tiles and in-place changes/remaps");
    ds_gpu::NativeObjMerge merge;ds_gpu::Composite compositor;ds_gpu::Surface composed;
    auto background=std::make_unique<NativeObjBackground>();
    auto scene=std::make_unique<ntr::PpuPreparedScene>();
    if(!merge.init() || !composed.init() || !compositor.init(fps_gpu_composite_shader,sizeof(fps_gpu_composite_shader))){std::puts("FAIL merge init");return 1;}
    for(unsigned id=0;id<128;++id){
        fixture->setup(id%64);if(id>=64)for(unsigned i=0;i<128;++i)
            if(fixture->oam[i*8+1]&0x20)fixture->oam[i*8+4]^=1;
        background->setup(id,*scene);
        if(!plan->prepare(fixture->input) || !ds_gpu::NativeObjMerge::supported(*scene)){std::puts("FAIL merge preflight");return 1;}
        C3D_FrameBegin(0);merge.update(*scene);
        if(!renderer.draw(output,*plan) || !compositor.opaque(composed,{*scene,scene->front},output.texture,true)){std::puts("FAIL merge draw");return 1;}
        merge.compose(compositor,composed,output.texture,*scene);
        C3D_FrameEnd(0);ds_gpu::Composite::readback(composed,actual,scratch);
        const auto* reference=native_obj_merge_expected+id*record_bytes;
        const auto* dictionary=reinterpret_cast<const uint32_t*>(reference);
        const auto* indices=reference+1024;unsigned failures=0;
        for(unsigned i=0;i<256*192;++i)if(actual[i]!=dictionary[indices[i]]){
            if(failures<4)std::printf("merge mismatch id%u xy%u/%u got%08lx DS%08lx\n",id,i%256,i/256,(unsigned long)actual[i],(unsigned long)dictionary[indices[i]]);
            ++failures;
        }
        std::printf("Native OBJ merge fixture=%u engine=%u mismatches=%u\n",id,fixture->engine,failures);
        if(failures){std::puts("FAIL");return 1;}
    }
    std::puts("Native OBJ merge PASS128 independent DS full-frame BG/OBJ ties, holes/backdrop, first-target brighten/fade and master rounding");
    background->setup(1,*scene);
    scene->has_geometry=true;if(merge.supported(*scene)){std::puts("FAIL geometry guard");return 1;}scene->has_geometry=false;
    scene->has_windows=true;if(merge.supported(*scene)){std::puts("FAIL windows guard");return 1;}scene->has_windows=false;
    scene->has_obj_alpha=true;if(merge.supported(*scene)){std::puts("FAIL OBJ alpha guard");return 1;}scene->has_obj_alpha=false;
    scene->blend=1<<6;if(merge.supported(*scene)){std::puts("FAIL blend guard");return 1;}scene->blend=0;
    scene->front_layer[0]=4;if(merge.supported(*scene)){std::puts("FAIL BG-only guard");return 1;}
    scene->front_layer[0]=5;scene->front_priority[0]=5;if(merge.supported(*scene)){std::puts("FAIL priority guard");return 1;}
    std::puts("Native OBJ merge strict fallback guards PASS");
    // Citro3D still refers to the currently bound program. Keep its shader
    // alive through the switch back to NativeObjects for the benchmark.
    fixture->setup(6);if(!plan->prepare(fixture->input)){std::puts("FAIL benchmark plan");return 1;}
    std::array<ntr::HostVector<uint32_t>,128> decoded;
    for(unsigned i=0;i<plan->count;++i){const auto index=plan->order[i];ds_gpu::decode_obj_texture(plan->sprites[index],decoded[index]);}
    uint64_t cpu_ticks=0,gpu_ticks=0,prepare_ticks=0;
    for(unsigned frame=0;frame<80;++frame){
        const auto start=now();plan->prepare(fixture->input);const auto prepared=now();
        std::memset(cpu,0,sizeof cpu);
        for(unsigned i=0;i<plan->count;++i){
            const auto index=plan->order[i];const auto& s=plan->sprites[index];const auto& pixels=decoded[index];
            for(unsigned y=0;y<s.height;++y)for(unsigned x=0;x<s.width;++x){
                const int dx=s.x+x,dy=s.y+y;if(dx<0||dx>=256||dy<0||dy>=192)continue;
                const auto c=pixels[(s.vflip?s.height-1-y:y)*s.width+(s.hflip?s.width-1-x:x)];
                if(c>>24)cpu[dy*256+dx]=(c&0xffffff)|((s.priority+1)<<24);
            }
        }
        // Every measured raster must reach memory, even though only the final
        // frame is compared after the timing loop.
        asm volatile("" : : "r"(cpu) : "memory");
        const auto end=now();if(frame>=20){prepare_ticks+=prepared-start;cpu_ticks+=end-prepared;}
    }
    for(unsigned frame=0;frame<80;++frame){
        const auto start=now();C3D_FrameBegin(0);plan->prepare(fixture->input);
        if(!renderer.draw(output,*plan)){std::puts("FAIL benchmark GPU");return 1;}
        C3D_FrameEnd(0);const auto end=now();if(frame>=20)gpu_ticks+=end-start;
    }
    ds_gpu::Composite::readback(output,actual,scratch);
    for(auto& c:actual){const auto alpha=c>>24;if(alpha==16 || alpha==32 || alpha==48 || alpha==64)c=(c&0xffffff)|((alpha/16)<<24);else if(alpha)c=0xffbadbad;}
    if(std::memcmp(cpu,actual,sizeof cpu)){std::puts("FAIL benchmark equivalence");return 1;}
    std::printf("Native OBJ isolated benchmark60 ms: exact snapshot prepare=%.6f cached CPU layer raster=%.6f candidate prepare+GPUsubmit+previousWait=%.6f. No logic/audio/BG/LCD/readback in measured window; not game FPS.\n",ms(prepare_ticks)/60,ms(cpu_ticks)/60,ms(gpu_ticks)/60);
    merge.destroy();compositor.destroy();composed.destroy();
    renderer.destroy();output.destroy();linearFree(scratch);C3D_Fini();gfxExit();
    std::puts("PASS");return 0;
}
