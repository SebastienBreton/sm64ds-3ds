#include "native_mesh_api.h"
std::shared_ptr<const prepared_mesh::Frame> bmd_compiled_frame();
bool bmd_compiled_geometry();
void bmd_compiled_driver_refused();
void bmd_compiled_gpu_result(const std::shared_ptr<const prepared_mesh::Frame>&,bool,bool);
bool bmd_compiled_frame_held(const prepared_mesh::Frame*);
#include "native_mesh_frame_adapter.h"
#include "viewer_3ds.h"
#include "frame_profile.h"
#include "ntr/rt.h"
#ifdef SM64DS_GPU_COMPOSITE_CONFIG
#include "gpu_composite_build_config.h"
#endif
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
#include "gpu_composite_viewer.h"
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
#include "native_obj_input_views.h"
// minigames-30fps-min: NativeObjMerge::supported without the citro3d header
// (3ds.h must be included below with the u32/s32 renames).
namespace ds_gpu{bool native_obj_merge_supported(ntr::PpuPreparedCandidates);}
#endif
#endif
#include "ds_gpu_viewport.h"
#include "ntr/gx.h"
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include "ntr/display_capture.h"
#include "ntr/vram_bus.h"
#include "ntr/host_allocator.h"
#include <vector>
#include <optional>
#ifdef __3DS__
#define u32 CtrU32
#define s32 CtrS32
#define vu32 CtrVU32
#define vs32 CtrVS32
#include <3ds.h>
#undef vs32
#undef vu32
#undef s32
#undef u32
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
static bool resident_option_enabled(const char* name) {
    const char* value=std::getenv(name);
    return value && std::strcmp(value,"0")!=0;
}
#include "gpu_capture_plan.h"
// minigames-2d-gpu: DS display capture on the resident GPU path (production
// flat VRAM). The capture itself is a GPU render-to-texture; this side only
// decodes registers, tracks which bank surfaces hold a capture, and clears
// DISPCAPCNT's enable bit like the DS does after its captured frame.
namespace capture_gpu {
struct Shadow {bool valid=false;unsigned offset=0;uint64_t generation=0,written=0,frame=0;};
Shadow shadows[4];uint64_t last_capture=0,capture_generation=0;
struct Frame {ds_gpu::CaptureFramePlan plan;ntr::DisplayCaptureGpuRequest request;uint32_t control=0;int writeback=-1;uint32_t writeback_address=0;};
// Flat-VRAM window (PPU-sampled) of physical bank A..D for its VRAMCNT, or 0
// (LCDC/ARM7/texture/disabled: not read by the 2D engines' BG/OBJ samplers).
uint32_t bank_window(unsigned bank,uint8_t control){
    if(!(control&0x80))return 0;
    const unsigned mst=control&(bank<2?3:7),offset=(control>>3)&3;
    if(mst==1)return 0x06000000u+offset*0x20000u;          // engine A BG
    if(mst==2 && bank<2)return 0x06400000u+(offset&1)*0x20000u; // engine A OBJ
    if(mst==4 && bank==2)return 0x06200000u;               // engine B BG
    if(mst==4 && bank==3)return 0x06600000u;               // engine B OBJ
    return 0;
}
// True when an enabled BG2/BG3 of the engine owning that window is a
// direct-colour extended bitmap starting exactly at address (the captured
// image is what it displays).
bool bitmap_bg_at(uint32_t address){
    const bool a=address<0x06200000u;
    if(!a && (address<0x06200000u || address>=0x06220000u))return false; // OBJ windows: not handled
    const uint32_t reg=a?0x04000000u:0x04001000u,base=a?0x06000000u:0x06200000u;
    const uint32_t disp=uint32_t(ntr::io_read(reg,4));const unsigned mode=disp&7;
    for(unsigned bg=2;bg<4;++bg){
        if(!(disp&(0x100u<<bg)))continue;
        if(!(bg==3 ? mode>=3 && mode<=5 : mode==5))continue;
        const unsigned cnt=unsigned(ntr::io_read(reg+8+bg*2,2));
        if((cnt&0x84)==0x84 && base+((cnt&0x1f00)<<6)==address)return true;
    }
    return false;
}
// False: a capture is requested that the GPU path cannot perform.
bool plan(Frame& frame){
    frame=Frame{};
    if(ntr::io_display_capture())return false; // physical VRAM: CPU driver
    frame.control=uint32_t(ntr::io_read(0x04000064,4));
    uint8_t vram[4];for(unsigned b=0;b<4;++b)vram[b]=uint8_t(ntr::io_read(0x04000240+b,1));
    const bool request_ok=ntr::display_capture_gpu_request(frame.control,vram,uint32_t(ntr::io_read(0x04000000,4)),frame.request);
    if(std::getenv("PLAYER_CAPTURE_TRACE")){
        static uint64_t key=~0ull;static unsigned printed=0;
        const uint64_t k=(uint64_t(frame.control)<<32)^(uint64_t(vram[0])|vram[1]<<8|vram[2]<<16|uint64_t(vram[3])<<24)^
            (uint64_t(ntr::io_read(0x04000000,4))*0x9e3779b97f4a7c15ull)^ntr::io_read(0x04001000,4)^(uint64_t(request_ok)<<63);
        if(k!=key && printed<200){key=k;++printed;
            std::printf("CAPTURE_TRACE frame=%llu cap=%08x vram=%02x%02x%02x%02x/%02x%02x%02x%02x%02x dA=%08x dB=%08x pow=%04x bg=%04x,%04x,%04x,%04x bld=%04x/%04x ok=%u\n",
                (unsigned long long)ntr::rt_frame(),unsigned(frame.control),vram[0],vram[1],vram[2],vram[3],
                unsigned(ntr::io_read(0x04000244,1)),unsigned(ntr::io_read(0x04000245,1)),unsigned(ntr::io_read(0x04000246,1)),
                unsigned(ntr::io_read(0x04000248,1)),unsigned(ntr::io_read(0x04000249,1)),
                unsigned(ntr::io_read(0x04000000,4)),unsigned(ntr::io_read(0x04001000,4)),unsigned(ntr::io_read(0x04000304,2)),
                unsigned(ntr::io_read(0x04000008,2)),unsigned(ntr::io_read(0x0400000a,2)),unsigned(ntr::io_read(0x0400000c,2)),unsigned(ntr::io_read(0x0400000e,2)),
                unsigned(ntr::io_read(0x04000050,2)),unsigned(ntr::io_read(0x04000052,2)),unsigned(request_ok));}
    }
    if(!request_ok)return false;
    const uint64_t now=ntr::rt_frame();
    if(now>last_capture+600)for(auto& s:shadows)s.valid=false;
    ntr::PpuCaptureView view;
    if(ntr::ppu_capture_view(ntr::ENGINE_B,view) && shadows[view.bank].valid && shadows[view.bank].offset==view.byte_offset){
        if(frame.request.active && frame.request.bank==view.bank)return false; // same-bank feedback
        frame.plan.view=true;frame.plan.view_bank=view.bank;frame.plan.view_master=view.master;
    }
    if(frame.request.active){frame.plan.capture=true;frame.plan.capture_bank=frame.request.bank;}
    frame.plan.delay_3d=frame.plan.capture || frame.plan.view;
    // A GPU-held capture that a CPU-rasterized engine now samples (e.g. the
    // title face toy: bank D shown as engine A BG3 under the 3D face) must
    // reach the flat VRAM window of its current mapping, once per capture.
    static const bool writeback=std::getenv("PLAYER_NO_CAPTURE_WRITEBACK")==nullptr;
    for(unsigned bank=0;writeback && bank<4 && frame.writeback<0;++bank){
        const auto& s=shadows[bank];
        if(!s.valid || s.written==s.generation)continue;
        if(frame.plan.view && frame.plan.view_bank==bank)continue; // sampled on the GPU
        if(frame.request.active && frame.request.bank==bank)continue;
        // Only right after its capture and only when a bitmap BG displays
        // exactly that image: a scene that reloads the bank itself (DMA/CPU
        // after remapping, e.g. leaving a dual-3D mini-game) keeps its data.
        if(ntr::rt_frame()>s.frame+8)continue;
        const uint32_t window=bank_window(bank,vram[bank]);
        if(window && bitmap_bg_at(window+s.offset)){frame.writeback=int(bank);frame.writeback_address=window+s.offset;}
    }
    return true;
}
// Renderer idle: copy the bank's captured pixels into its mapped window.
void writeback(const Frame& frame){
    if(frame.writeback<0)return;
    auto& s=shadows[frame.writeback];s.written=s.generation;
    static uint16_t* pixels=nullptr;
    if(!pixels)pixels=static_cast<uint16_t*>(std::malloc(256*192*2));
    if(!pixels || !ds_gpu::capture_bank_readback_rgb555(unsigned(frame.writeback),pixels)){
        std::printf("Capture writeback failed bank=%d\n",frame.writeback);return;
    }
    std::memcpy(ntr::host_ptr(frame.writeback_address),pixels,256*192*2);
    std::printf("Capture writeback bank=%d address=%08x frame=%llu\n",frame.writeback,unsigned(frame.writeback_address),(unsigned long long)ntr::rt_frame());
}
// The frame is handed to the resident presentation: record the bank and end
// the one-frame capture (DS clears the enable bit at the captured VBlank).
void commit(const Frame& frame){
    if(!frame.plan.capture)return;
    shadows[frame.request.bank]={true,frame.request.byte_offset,++capture_generation,0,ntr::rt_frame()};
    last_capture=ntr::rt_frame();
    ntr::io_write(0x04000064,frame.control&~0x80000000u,4);
}
}
#endif
extern "C" volatile uint64_t sm64ds_frame_prof[48]={};
uint64_t frame_profile::ticks(){
#ifdef __3DS__
    return svcGetSystemTick();
#else
    return 0;
#endif
}
static void write_bmp400(const char *path, const uint32_t *px)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const uint32_t img = 400 * 240 * 3, total = 54 + img, off = 54, dib = 40;
    const int32_t w = 400, h = 240;
    const uint16_t planes = 1, bpp = 24;
    uint8_t hdr[54] = {'B', 'M'};
    memcpy(hdr + 2, &total, 4); memcpy(hdr + 10, &off, 4); memcpy(hdr + 14, &dib, 4);
    memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4); memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2); memcpy(hdr + 34, &img, 4);
    fwrite(hdr, 1, 54, f);
    static uint8_t row[400 * 3];
    for (int y = 239; y >= 0; --y) {
        for (int x = 0; x < 400; ++x) {
            const uint32_t c = px[y * 400 + x];
            row[x * 3] = c & 0xFF; row[x * 3 + 1] = (c >> 8) & 0xFF; row[x * 3 + 2] = (c >> 16) & 0xFF;
        }
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
}

// Eight disjoint panels exercise polygon/texture alpha and translucent depth.
// Direct readback is compared by reference-ds/gpu-alpha/check_capture.py.
static void alpha_probe(ntr::HostVector<V3dsVert>& vertices, ntr::HostVector<V3dsBatch>& batches) {
    vertices.clear(); batches.clear();
    static uint32_t half[64], mixed[64];
    for (int i=0;i<64;++i) {
        half[i]=0x7fff0000u;
        const unsigned alpha=(i%8)<2 ? 0 : (i%8)<5 ? 127 : 255;
        mixed[i]=(alpha<<24)|0x00ff0000u;
    }
    auto quad=[&](int panel,float z,float r,float g,float b,unsigned alpha,bool depth,const uint32_t* texture=nullptr) {
        const float x0=-1.f+(panel%4)*.5f, x1=x0+.5f;
        const float y1=1.f-(panel/4), y0=y1-1.f;
        const int first=vertices.size();
        const V3dsVert q[4]={{x0,y0,z,1,r,g,b,1,0,1},{x1,y0,z,1,r,g,b,1,1,1},
                            {x1,y1,z,1,r,g,b,1,1,0},{x0,y1,z,1,r,g,b,1,0,0}};
        for(int k:{0,1,2,0,2,3})vertices.push_back(q[k]);
        batches.push_back({texture,texture?8:0,texture?8:0,0,first,6,false,
                           (alpha<<16)|0xc0u|(depth?1u<<11:0)});
    };
    for(int panel=0;panel<8;++panel) {
        quad(panel,.8f,0,0,1,31,false);
        if(panel==0)quad(panel,-.5f,1,0,0,31,false);
        if(panel==1)quad(panel,-.5f,1,0,0,15,false);
        if(panel==2)quad(panel,-.5f,1,0,0,22,false);
        if(panel==3)quad(panel,-.5f,1,0,0,0,false);
        if(panel==4)quad(panel,-.5f,1,1,1,31,false,half);
        if(panel==5)quad(panel,-.5f,1,1,1,15,false,half);
        if(panel==6) { quad(panel,-.5f,1,0,0,15,true);quad(panel,0,0,1,0,31,false); }
        if(panel==7) { quad(panel,-.5f,1,1,1,31,false,mixed);quad(panel,0,0,1,0,31,false); }
    }
}

static bool player_present_legacy(bool capture, bool perspective) {
    const double batch_start=v3ds_time_ms();
    size_t count=0; const auto *triangles=ntr::gx_polygons(count);
    if (count*3>0x8000) { std::fprintf(stderr,"FATAL: Player GPU vertex capacity exceeded\n"); return false; }
    static ntr::HostVector<V3dsVert> vertices;
    static ntr::HostVector<V3dsBatch> batches;
    static ntr::HostVector<float> visible_depths;
    vertices.clear(); batches.clear();visible_depths.clear();
    for (size_t i=0;i<count;++i) {
        const auto &t=triangles[i];
        if (batches.empty() || batches.back().tex!=t.tex || batches.back().wrap!=t.wrap ||
            batches.back().tw!=t.tw || batches.back().th!=t.th || batches.back().polygon_attr!=t.polygon_attr ||
            batches.back().texture_version!=t.texture_version)
            batches.push_back({t.tex,t.tw,t.th,t.wrap,(int)vertices.size(),0,false,t.polygon_attr,t.texture_version});
        DsGpuVertex polygon[8]; int n=0;
        for (const auto &v:t.v) polygon[n++]={{v.cx,v.cy,v.cz,v.cw,
            ((v.color>>16)&255)/255.f,((v.color>>8)&255)/255.f,(v.color&255)/255.f,(v.color>>24)/255.f,
            t.tw?v.u/t.tw:0.f,t.th?v.v/t.th:0.f},v.x,v.y};
        n=ds_gpu_clip_viewport(polygon);
        for(int k=0;k<n;++k)if(polygon[k].gpu.w>1e-5f &&
            polygon[k].screen_x>=0 && polygon[k].screen_x<256 &&
            polygon[k].screen_y>=0 && polygon[k].screen_y<192)
            visible_depths.push_back(polygon[k].gpu.w);
        for (int k=1;k+1<n;++k) {
            vertices.push_back(polygon[0].gpu);vertices.push_back(polygon[k].gpu);vertices.push_back(polygon[k+1].gpu);
            batches.back().count+=3;
        }
    }
    if (vertices.size()>0x8000) { std::fprintf(stderr,"FATAL: clipped GPU vertex capacity exceeded\n"); return false; }
    if (std::getenv("PLAYER_DEPTH_PROBE")) {
        vertices.clear(); batches.clear();
        for (int plane=0;plane<2;++plane) {
            float z=plane?0.5f:-0.5f;
            for (int k=0;k<3;++k) vertices.push_back({k==0?-0.8f:k==1?0.8f:0.f,k==2?0.8f:-0.8f,z,1.f,plane?0.f:1.f,plane?1.f:0.f,0.f,1.f,0.f,0.f});
        }
        batches.push_back({nullptr,0,0,0,0,6,false});
    }
    const bool probe = std::getenv("PLAYER_ALPHA_PROBE") != nullptr;
    if(probe) alpha_probe(vertices,batches);
    const double batch_ms=v3ds_time_ms()-batch_start;
    float convergence=1.f;
    if(!visible_depths.empty()){
        const auto middle=visible_depths.begin()+visible_depths.size()/2;
        std::nth_element(visible_depths.begin(),middle,visible_depths.end());
        convergence=*middle;
    }
    if(!probe && std::getenv("PLAYER_NATIVE_COMPOSITE")){
        static ntr::Framebuffer geometry_left,geometry_right,engine_left,engine_right,engine_b;
        static ntr::HostVector<ntr::Framebuffer> capture_frames;
        const bool a_upper=ntr::io_read(0x04000304,2)&0x8000;
        const double native_start=v3ds_time_ms();
        if(!ntr::rt_running())ntr::io_complete_unmapped_capture_frame();
        auto* ds_capture=ntr::io_display_capture();
        const uint32_t display_a=uint32_t(ntr::io_read(0x04000000,4));
        const unsigned display_mode=(display_a>>16)&3;
        ntr::DisplayCaptureMemory capture_memory{};
        ntr::DisplayCaptureSources capture_sources{};
        bool capture_pending=false;
        bool capture_centre_rendered=false;
        if(ds_capture){
            auto* banks=ntr::io_vram_banks();
            for(unsigned bank=0;bank<4;++bank)capture_memory.banks[bank]=banks->physical(bank);
            capture_memory.lcdc_mask=banks->lcdc_mask();capture_memory.context=banks;
            capture_memory.written=[](void* context,unsigned bank,unsigned at,unsigned bytes){
                static_cast<ntr::VramBanks*>(context)->changed(bank,at,bytes);
            };
            capture_pending=ds_capture->latched() || (ds_capture->control()&0x80000000u);
            capture_sources=ds_capture->sources(0,false,capture_memory.lcdc_mask);
            if(display_mode==3 || capture_sources.fifo){
                std::fprintf(stderr,"FATAL: physical VRAM display/capture FIFO requires an unimplemented queue/DMA backend\n");return false;
            }
            if(capture_sources.composite || capture_sources.geometry)capture_frames.resize(3);
            // Prepare/read back only requested source A. Render the DS centre
            // before the ordinary eye pass, which restores native stereo state.
            // Neither stereo eye is a valid replacement for the capture source.
            if(capture_sources.composite){
                auto& capture_geometry=capture_frames[0];auto& capture_scratch=capture_frames[1];
                auto& capture_composite=capture_frames[2];
                const auto candidates=ntr::ppu_prepare_capture_candidates();
                if(candidates.state.has_geometry){
                    if(!v3ds_gpu_geometry(vertices.data(),vertices.size(),batches.data(),batches.size(),
                        &capture_geometry.px[0][0],&capture_scratch.px[0][0],convergence,true,false))return false;
                    capture_centre_rendered=true;
                }
                ntr::ppu_compose_candidates(candidates,capture_geometry,capture_composite,false);
            }else if(capture_sources.geometry){
                if(!v3ds_gpu_geometry(vertices.data(),vertices.size(),batches.data(),batches.size(),
                    &capture_frames[0].px[0][0],&capture_frames[1].px[0][0],convergence,true,false))return false;
                capture_centre_rendered=true;
            }
        }
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
        // Physical capture consumes a separately rendered DS centre and may
        // feed the next scanline. Keep those cases in the proven CPU driver.
        // The resident wrapper rejects unsupported effects before drawing.
        if(resident_option_enabled("PLAYER_RESIDENT_COMPOSITE") && !capture_pending &&
           !capture_sources.active && !(ntr::io_read(0x04000064,4)&0x80000000u) && display_mode<2){
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
            const ds_gpu::ObjPlan* objects[2]{};
            const bool resident_objects=resident_option_enabled("PLAYER_RESIDENT_OBJECTS");
            auto prepare=[&](ntr::Engine engine){
                if(resident_objects && engine==ntr::ENGINE_B && ds_gpu::native_obj_admit(engine)){
                    const auto background=ntr::ppu_prepare_background_candidates_cached(engine);
                    if(background && !background->state.has_geometry){
                        objects[engine]=ds_gpu::native_obj_prepare(engine);
                        if(objects[engine])return *background;
                    }
                }
                return ntr::ppu_prepare_candidates_cached(engine);
            };
            const auto a=prepare(ntr::ENGINE_A),b=prepare(ntr::ENGINE_B);
#else
            const auto a=ntr::ppu_prepare_candidates_cached(ntr::ENGINE_A);
            const auto b=ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B);
#endif
            if(v3ds_gpu_present_candidates(vertices.data(),vertices.size(),batches.data(),batches.size(),
                a,b,a_upper,convergence,true,a_upper
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
                ,objects[0],objects[1]
#endif
                )){
                v3ds_profile(0,batch_ms);
                if(capture){
                    // Explicit diagnostics only. Presentation itself keeps
                    // geometry and both composed panels resident on the GPU.
                    v3ds_gpu_candidate_geometry(&geometry_left.px[0][0],&geometry_right.px[0][0]);
                    auto& upper_left=a_upper?engine_left:engine_b;
                    auto& upper_right=a_upper?engine_right:engine_b;
                    auto& lower=a_upper?engine_b:engine_left;
                    v3ds_gpu_candidate_panel(0,&upper_left.px[0][0]);
                    if(a_upper && v3ds_native_stereo())v3ds_gpu_candidate_panel(1,&upper_right.px[0][0]);
                    v3ds_gpu_candidate_panel(2,&lower.px[0][0]);
                    ntr::ppu_write_bmp("player-native-geometry.bmp",geometry_left);
                    ntr::ppu_write_bmp("player-native-geometry-right.bmp",geometry_right);
                    ntr::ppu_write_bmp("player-native-upper.bmp",upper_left);
                    ntr::ppu_write_bmp("player-native-bottom.bmp",lower);
                    static uint32_t pixels[400*240];
                    v3ds_read_top(pixels,false);write_bmp400("player-gpu-left.bmp",pixels);
                    v3ds_read_top(pixels,true);write_bmp400("player-gpu-right.bmp",pixels);
                }
                return true;
            }
        }
#endif
        // Resident OBJ preparation returns BG-only planes. Any refusal above
        // falls through to ordinary preparation below, including OBJ raster.
        if(!v3ds_gpu_geometry(vertices.data(),vertices.size(),batches.data(),batches.size(),
            &geometry_left.px[0][0],&geometry_right.px[0][0],convergence,true,a_upper))return false;
        const double geometry_end=v3ds_time_ms();
        if(std::getenv("PLAYER_PREPARED_PPU")){
            const bool profile_ppu=std::getenv("PLAYER_PROFILE_PPU")!=nullptr;
            ntr::ppu_prepare_profile_clock(profile_ppu?v3ds_time_ms:nullptr);
            double stamp[5]{};
            if(profile_ppu)stamp[0]=v3ds_time_ms();
            const auto& a=ntr::ppu_prepare_scene_cached(ntr::ENGINE_A);
            if(profile_ppu)stamp[1]=v3ds_time_ms();
            ntr::ppu_compose_prepared(a,geometry_left,engine_left);
            if(v3ds_native_stereo())ntr::ppu_compose_prepared(a,geometry_right,engine_right);
            if(profile_ppu)stamp[2]=v3ds_time_ms();
            const auto& b=ntr::ppu_prepare_scene_cached(ntr::ENGINE_B);
            if(profile_ppu)stamp[3]=v3ds_time_ms();
            ntr::ppu_compose_prepared(b,geometry_left,engine_b);
            if(profile_ppu){
                stamp[4]=v3ds_time_ms();
                static double totals[4]{};static unsigned count=0;
                for(unsigned i=0;i<4;++i)totals[i]+=stamp[i+1]-stamp[i];
                if(++count==60){
                    std::printf("Prepared PPU parts ms: prepareA=%.3f composeA=%.3f prepareB=%.3f composeB=%.3f\n",
                        totals[0]/count,totals[1]/count,totals[2]/count,totals[3]/count);
                    std::printf("Prepared PPU state: A blend=%04x master=%04x geometry=%u windows=%u; B blend=%04x master=%04x resolved=%u\n",
                        a.blend,a.master,unsigned(a.has_geometry),unsigned(a.has_windows),b.blend,b.master,unsigned(b.resolved));
                    for(auto eng:{ntr::ENGINE_A,ntr::ENGINE_B}){
                        const auto p=ntr::ppu_prepare_profile_read(eng,true);
                        std::printf("PPU preparation engine=%u calls=%llu total ms: windows=%.3f bg=%.3f objects=%.3f resolve=%.3f\n",
                            unsigned(eng),(unsigned long long)p.calls,p.windows,p.bg,p.objects,p.resolve);
                    }
                    count=0;for(auto& total:totals)total=0;
                }
            }
        }else{
            ntr::ppu_scanout_scene_with_geometry(ntr::ENGINE_A,engine_left,geometry_left);
            if(v3ds_native_stereo())ntr::ppu_scanout_scene_with_geometry(ntr::ENGINE_A,engine_right,geometry_right);
            ntr::ppu_scanout_scene_with_geometry(ntr::ENGINE_B,engine_b,geometry_left);
        }
        if(ds_capture && (capture_pending || display_mode==2)){
            // In display mode2 scan each physical VRAM row before capturing
            // that row. Ordinary mode1 retains the independent stereo images.
            const auto result=ds_capture->frame(display_a,uint16_t(ntr::io_read(0x0400006c,2)),capture_memory,
                capture_sources.composite?&capture_frames[2].px[0][0]:nullptr,
                capture_sources.geometry?&capture_frames[0].px[0][0]:nullptr,
                display_mode==2?&engine_left.px[0][0]:nullptr);
            if(result.status==ntr::DisplayCaptureStatus::MissingComposite ||
               result.status==ntr::DisplayCaptureStatus::MissingGeometry ||
               result.status==ntr::DisplayCaptureStatus::MissingFifo){
                std::fprintf(stderr,"FATAL: physical display capture lacks source status=%u\n",unsigned(result.status));return false;
            }
            if(display_mode==2)engine_right=engine_left;
            ntr::io_display_capture_vblank();
        }
        const auto& upper_left=a_upper?engine_left:engine_b;
        const auto& upper_right=a_upper?(v3ds_native_stereo()?engine_right:engine_left):engine_b;
        const auto& lower=a_upper?engine_b:engine_left;
        const double composition_end=v3ds_time_ms();
        v3ds_present_native(&upper_left.px[0][0],&upper_right.px[0][0],&lower.px[0][0],a_upper);
        const double present_end=v3ds_time_ms();
        static double timings[3]{};static unsigned samples=0;
        timings[0]+=geometry_end-native_start;timings[1]+=composition_end-geometry_end;timings[2]+=present_end-composition_end;
        if(++samples==60){
            std::printf("Native composite ms: GPU/readback=%.3f PPU=%.3f transfer=%.3f\n",timings[0]/60,timings[1]/60,timings[2]/60);
            uint64_t hits,misses;ntr::ppu_prepared_cache_stats(hits,misses);
            std::printf("Prepared PPU cache: hits=%llu misses=%llu\n",(unsigned long long)hits,(unsigned long long)misses);
            ntr::ppu_background_cache_stats(hits,misses);
            std::printf("PPU background cache: hits=%llu misses=%llu\n",(unsigned long long)hits,(unsigned long long)misses);
            samples=0;for(auto& time:timings)time=0;
        }
        v3ds_profile(0,batch_ms);
        if(capture){
            ntr::ppu_write_bmp("player-native-geometry.bmp",geometry_left);
            ntr::ppu_write_bmp("player-native-geometry-right.bmp",geometry_right);
            if(capture_centre_rendered)ntr::ppu_write_bmp("player-native-capture-centre.bmp",capture_frames[0]);
            if(capture_sources.composite)ntr::ppu_write_bmp("player-native-capture-source.bmp",capture_frames[2]);
            ntr::ppu_write_bmp("player-native-upper.bmp",upper_left);
            ntr::ppu_write_bmp("player-native-bottom.bmp",lower);
            static uint32_t pixels[400*240];
            v3ds_read_top(pixels,false);write_bmp400("player-gpu-left.bmp",pixels);
            v3ds_read_top(pixels,true);write_bmp400("player-gpu-right.bmp",pixels);
        }
        return true;
    }
    if(auto* ds_capture=ntr::io_display_capture()){
        const unsigned mode=(ntr::io_read(0x04000000,4)>>16)&3;
        if(ds_capture->latched() || (ds_capture->control()&0x80000000u) || mode>=2){
            std::fprintf(stderr,"FATAL: physical VRAM display/capture requires PLAYER_NATIVE_COMPOSITE\n");return false;
        }
    }
    v3ds_gpu_frame(vertices.data(),vertices.size(),batches.data(),batches.size(),
        probe?"GPU alpha probe":"Native Player","GPU",1.f,convergence,probe?false:perspective,!probe);
    v3ds_profile(0,batch_ms);
    if (capture) {
        static uint32_t pixels[400*240];
        v3ds_read_top(pixels,false); write_bmp400("player-gpu-left.bmp",pixels);
        v3ds_read_top(pixels,true); write_bmp400("player-gpu-right.bmp",pixels);
    }
    return true;
}

#include "platform/3ds/geometry_arena_viewer.h"
#include "ntr/gx_direct.h"
#include "platform/3ds/gx_geometry_resources.h"
#include "platform/3ds/gx_gpu_viewer.h"
namespace {
bool direct_reuse=false;
bool direct_ready=false;
const ntr::gx_geometry::Packet* direct_source=nullptr;
ntr::gx_geometry::Prepared direct_prepared{};
float direct_convergence=1.f;
ntr::HostVector<V3dsBatch> direct_batches;
ntr::HostVector<float> direct_depths;
ntr::gx_geometry::Builder direct_builder;
ntr::gx_geometry::Resources direct_resources;
struct DirectGuard {uint64_t token;bool keep=false;~DirectGuard(){if(!keep){v3ds_arena::abort(token);direct_ready=false;}}};
}
void player_direct_configure(bool reuse_between_presentations){direct_reuse=reuse_between_presentations;direct_ready=false;}


#if defined(__3DS__) && defined(SM64DS_GPU_COMPOSITE_CANDIDATE) && defined(SM64DS_NATIVE_OBJ_CANDIDATE)
#define SM64DS_PRESENT_PIPELINE 1
// Core-1 presentation worker. While the game thread is parked at VBlank the
// worker runs the audio pump (audio state shares the game heap), then, after
// the main thread has read every DS input (PPU planes, OBJ plans, a copy of
// the GX packet), it builds and submits the GPU frame while the game computes
// the next update. All citro3d work of a pipelined frame happens on the worker
// and the main thread always waits for it before touching renderer state.
namespace present_pipeline {
struct Job {
    bool audio=false,present=false;
    // Native mesh frames are immutable snapshots; compiled ones report back.
    std::shared_ptr<const prepared_mesh::Frame> mesh;
    bool mesh_compiled=false;
    ntr::gx_geometry::Packet packet;
    const ntr::gx_gpu::Frame* gpu=nullptr; // GPU geometry mode frame (gpu-geometry-menu)
    std::optional<ntr::PpuPreparedCandidates> a,b;
    const ds_gpu::ObjPlan* objects[2]{};
    bool a_upper=false;float convergence=1.f;
    ds_gpu::CaptureFramePlan capture; // minigames-2d-gpu
};
Job job;
Thread worker=nullptr;
LightEvent go,done;
bool busy=false,enabled=false;
volatile bool pending=false,audio_async=false,audio_stopped=false;
volatile bool audio_ok=true,present_ok=true;
bool (*audio_pump)()=nullptr;
unsigned cooldown=0;
uint64_t pipelined=0,synchronous=0,refused=0,held_skips=0;
uint64_t audio_ticks=0;double start_ms=0;
bool run_present();
bool run_mesh_present();
// Result of the last pipelined mesh job, consumed by the game side.
bool mesh_job_pending=false,mesh_job_ok=true;
__thread bool on_worker=false;
Thread audio_thread=nullptr;
// Asynchronous audio: a separate core-1 thread with a higher priority than
// the presentation worker pumps the NDSP queue every 3 ms, so long GPU jobs
// (scene changes) cannot starve the 3x512-sample output queue. It shares the
// worker's thread-local identity (heap routing, deferred alarms).
void audio_loop(void*){
    on_worker=true;
    while(!audio_stopped){
        if(audio_pump && audio_ok){
            const uint64_t t0=svcGetSystemTick();audio_ok=audio_pump();audio_ticks+=svcGetSystemTick()-t0;
        }
        svcSleepThread(3000000LL);
    }
}
// castle-min30: core-1 helper preparing engine B's GPU-affine candidates
// (ntr::ppu_prepare_background_affine) while the main thread prepares engine
// A: menu/pause frames rasterize new BG layers on both engines at once.
// SM64DS_PPU_HELPER=0 disables it (sequential preparation as before).
Thread helper=nullptr;
LightEvent helper_go,helper_done;
volatile bool helper_pending=false;
std::optional<ntr::PpuPreparedCandidates> helper_result;
void helper_loop(void*){
    on_worker=true;
    for(;;){
        LightEvent_Wait(&helper_go);
        if(helper_pending){
            __sync_synchronize();helper_pending=false;
            helper_result.reset();if(auto r=ntr::ppu_prepare_background_affine(ntr::ENGINE_B))helper_result.emplace(*r);
            __sync_synchronize();LightEvent_Signal(&helper_done);
        }
    }
}
void main_loop(void*){
    on_worker=true;
    for(;;){
        LightEvent_Wait(&go);
        if(pending){
            __sync_synchronize();pending=false;
            if(job.audio){job.audio=false;audio_ok=audio_pump?audio_pump():true;}
            if(job.present){frame_profile::Scope s(frame_profile::WorkerMesh);job.present=false;present_ok=job.mesh?run_mesh_present():run_present();}
            __sync_synchronize();LightEvent_Signal(&done);
        }
    }
}
// minigames-30fps-min: a posted presentation job pins the PPU cache ways of
// its candidate views until the worker is idle again (early preparation).
void wait_idle(){if(busy){LightEvent_Wait(&done);busy=false;ntr::ppu_pin_candidate_ways(false);}}
void post(){busy=true;if(job.present)ntr::ppu_pin_candidate_ways(true);__sync_synchronize();pending=true;LightEvent_Signal(&go);}
}
extern "C" bool sm64ds_on_present_worker(){return present_pipeline::on_worker;}
// The core-1 worker and the game threads all print diagnostics; concurrent
// SD writes lost log bytes. Linked with --wrap=_write_r (build_game.py).
extern "C" ssize_t __real__write_r(struct _reent*,int,const void*,size_t);
extern "C" ssize_t __wrap__write_r(struct _reent* r,int fd,const void* buffer,size_t bytes){
    static RecursiveLock lock;static volatile int state=0;
    if(state!=2){
        if(__sync_bool_compare_and_swap(&state,0,1)){RecursiveLock_Init(&lock);__sync_synchronize();state=2;}
        else while(state!=2)svcSleepThread(10000);
    }
    RecursiveLock_Lock(&lock);
    const ssize_t written=__real__write_r(r,fd,buffer,bytes);
    RecursiveLock_Unlock(&lock);
    return written;
}
bool player_present_pipeline_enable(bool (*audio)(),bool asynchronous_audio){
    using namespace present_pipeline;
    if(enabled)return true;
    // Worker core (hardware bring-up): SM64DS_PIPELINE_CORE, default 1.
    int core=1;
    if(const char* c=std::getenv("SM64DS_PIPELINE_CORE"))core=std::atoi(c);
    if(core==1){
        // SM64DS_APP_CPU_LIMIT (default 80): share of core 1 given to the application;
        // the GSP/system modules also run there (hardware performance experiment).
        static const unsigned percent=[](){const char* v=std::getenv("SM64DS_APP_CPU_LIMIT");const int n=v?std::atoi(v):80;return unsigned(n<5?5:n>80?80:n);}();
        const Result limit=APT_SetAppCpuTimeLimit(percent);
        if(R_FAILED(limit))std::printf("Present pipeline: APT_SetAppCpuTimeLimit failed %08lx\n",(unsigned long)limit);
    }
    LightEvent_Init(&go,RESET_ONESHOT);LightEvent_Init(&done,RESET_ONESHOT);
    CtrS32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    audio_pump=audio;audio_async=asynchronous_audio;start_ms=v3ds_time_ms();
    worker=threadCreate(main_loop,nullptr,64*1024,priority-1,core,false);
    enabled=worker!=nullptr;
    {const char* h=std::getenv("SM64DS_PPU_HELPER");
     if(enabled && !(h && std::strcmp(h,"0")==0)){
        LightEvent_Init(&helper_go,RESET_ONESHOT);LightEvent_Init(&helper_done,RESET_ONESHOT);
        helper=threadCreate(helper_loop,nullptr,64*1024,priority-1,core,false);
     }}
    if(enabled && audio_async){
        audio_thread=threadCreate(audio_loop,nullptr,64*1024,priority-2,core,false);
        // Hardware without core-1 time: synchronous presentation, audio on
        // the main thread (the idle worker stays parked on its event).
        if(!audio_thread){std::fputs("Present pipeline: audio thread unavailable, synchronous presentation\n",stderr);enabled=false;}
    }
    std::printf("Present pipeline core%d worker=%u asyncAudio=%u\n",core,unsigned(enabled),unsigned(asynchronous_audio));
    return enabled;
}
// Stop worker audio before the NDSP output closes.
void player_present_pipeline_stop_audio(){
    using namespace present_pipeline;
    if(!enabled)return;
    wait_idle();audio_stopped=true;
    if(audio_thread){threadJoin(audio_thread,U64_MAX);threadFree(audio_thread);audio_thread=nullptr;}
}
// Start the audio pump on the worker. The game thread must be parked.
void player_present_pipeline_audio(){
    using namespace present_pipeline;
    wait_idle();job.audio=true;post();
}
// After player_present the audio job has completed; the GPU job may still run.
bool player_present_pipeline_audio_ok(){return present_pipeline::audio_ok;}
void player_present_pipeline_drain(){present_pipeline::wait_idle();}
void player_present_pipeline_stats(){
    using namespace present_pipeline;
    std::printf("Present pipeline frames pipelined=%llu synchronous=%llu refused=%llu heldSkips=%llu audioLoad=%.1f%%\n",
        (unsigned long long)pipelined,(unsigned long long)synchronous,(unsigned long long)refused,(unsigned long long)held_skips,
        100.0*(audio_ticks/double(SYSCLOCK_ARM11/1000))/std::max(1.0,v3ds_time_ms()-start_ms));
    gx_gpu_viewer::print_stats();
}
#endif

namespace {
native_mesh::FrameAdapter mesh_adapter;
uint64_t mesh_driver_admitted=0,mesh_driver_fallback=0,mesh_driver_refusal[10]{};
bool mesh_player_try(bool capture){
 const auto compiled=bmd_compiled_frame();
 struct CompiledAttempt {
  bool active,success=false;
  ~CompiledAttempt(){if(active&&!success)bmd_compiled_driver_refused();}
 } attempt{bmd_compiled_geometry()};
 const auto& packet=ntr::gx_native_mesh_packet();
 if(!compiled && bmd_compiled_geometry()){mesh_adapter.reset();++mesh_driver_fallback;return false;}
 if(!packet.requested()){mesh_adapter.reset();return false;}
 if(!ntr::gx_direct_enabled() || !std::getenv("PLAYER_NATIVE_COMPOSITE") ||
    std::getenv("PLAYER_DEPTH_PROBE") || std::getenv("PLAYER_ALPHA_PROBE")){
  mesh_adapter.reset();++mesh_driver_fallback;return false;
 }
 if(!ntr::gx_native_mesh_camera_valid()){mesh_adapter.reset();++mesh_driver_fallback;return false;}
 if(!compiled && !packet.admitted()){mesh_adapter.reset();++mesh_driver_fallback;++mesh_driver_refusal[unsigned(packet.refusal())];return false;}
 const auto* controller=ntr::io_display_capture();
 const uint32_t dispcnt=uint32_t(ntr::io_read(0x04000000,4));
 const bool a_upper=ntr::io_read(0x04000304,2)&0x8000;
 if(!resident_option_enabled("PLAYER_RESIDENT_COMPOSITE")||!a_upper||((dispcnt>>16)&3)>=2||
   (ntr::io_read(0x04000064,4)&0x80000000u)||
   (controller&&(controller->latched()||(controller->control()&0x80000000u)||
     controller->sources(0,false,ntr::io_vram_banks()->lcdc_mask()).active))){mesh_adapter.reset();++mesh_driver_fallback;return false;}
 const ds_gpu::ObjPlan* objects[2]{};
 auto prepare=[&](ntr::Engine engine){
  if(resident_option_enabled("PLAYER_RESIDENT_OBJECTS")&&engine==ntr::ENGINE_B&&ds_gpu::native_obj_admit(engine)){
   const auto bg=ntr::ppu_prepare_background_candidates_cached(engine);
   if(bg&&!bg->state.has_geometry){objects[engine]=ds_gpu::native_obj_prepare(engine);if(objects[engine])return *bg;}
  }
  return ntr::ppu_prepare_candidates_cached(engine);
 };
 const auto a=prepare(ntr::ENGINE_A),b=prepare(ntr::ENGINE_B);
 const auto publication=compiled?native_mesh::FrameAdapter::Publication{}:mesh_adapter.capture(packet,ntr::gx_native_mesh_convergence());
 const auto frame=compiled?compiled:publication.frame;
 const float convergence=compiled?ntr::gx_native_mesh_convergence():publication.convergence;
 const bool gpu_ok=frame && v3ds_prepared_mesh_present(frame,a,b,a_upper,convergence,true,objects[0],objects[1]);
 if(compiled && frame)bmd_compiled_gpu_result(frame,false,gpu_ok);
 if(!gpu_ok){
  mesh_adapter.reset();++mesh_driver_fallback;return false;
 }
 attempt.success=true;++mesh_driver_admitted;
 if(capture){
  static ntr::Framebuffer left,right,upper,bottom;
  v3ds_gpu_candidate_geometry(&left.px[0][0],&right.px[0][0]);
  v3ds_gpu_candidate_panel(0,&upper.px[0][0]);v3ds_gpu_candidate_panel(2,&bottom.px[0][0]);
  ntr::ppu_write_bmp("player-native-geometry.bmp",left);ntr::ppu_write_bmp("player-native-geometry-right.bmp",right);
  ntr::ppu_write_bmp("player-native-upper.bmp",upper);ntr::ppu_write_bmp("player-native-bottom.bmp",bottom);
  static uint32_t pixels[400*240];v3ds_read_top(pixels,false);write_bmp400("player-gpu-left.bmp",pixels);
  v3ds_read_top(pixels,true);write_bmp400("player-gpu-right.bmp",pixels);
 }
 return true;
}
}
extern "C" void native_mesh_driver_reset(){mesh_adapter.reset();}
extern "C" void native_mesh_driver_stats(){
 std::printf("Native Frame adapter builds=%llu held=%llu refused=%llu\n",
  (unsigned long long)mesh_adapter.builds,(unsigned long long)mesh_adapter.held_hits,(unsigned long long)mesh_adapter.refused);
 std::printf("Native mesh driver admitted=%llu fallback=%llu convergence=%.9f reasons=",(unsigned long long)mesh_driver_admitted,(unsigned long long)mesh_driver_fallback,ntr::gx_native_mesh_convergence());
 for(auto n:mesh_driver_refusal)std::printf("%llu,",(unsigned long long)n);std::puts("");
}


#ifdef SM64DS_PRESENT_PIPELINE
bool present_pipeline::run_present(){
    V3dsDirectLease lease{};
    if(!v3ds_arena::acquire(lease))return false;
    DirectGuard guard{lease.frame_token};
    direct_ready=false;
    const auto& packet=job.packet;
    ntr::gx_geometry::Prepared prepared;
    if(job.gpu){
        if(!gx_gpu_viewer::build(*job.gpu,lease,direct_batches,prepared,direct_convergence,job.a_upper))return false;
        if(!direct_resources.freeze(direct_batches.data(),prepared.batch_count))return false;
    }else{
    direct_builder.reserve(packet.vertices().size());
    const auto nb=packet.triangles().size()+1,nd=packet.triangles().size()*5+1;
    if(direct_batches.size()<nb)direct_batches.resize(nb);
    if(direct_depths.size()<nd)direct_depths.resize(nd);
    if(!direct_builder.build(packet,{lease.vertices,lease.vertex_capacity,
        direct_batches.data(),direct_batches.size(),direct_depths.data(),direct_depths.size(),lease.frame_token}))return false;
    if(!direct_resources.freeze(direct_batches.data(),direct_builder.batch_count()))return false;
    prepared=direct_builder.prepared();
    direct_convergence=direct_builder.convergence();
    }
    ds_gpu::g_capture_plan=job.capture;
    if(!v3ds_arena::present_candidates(prepared,*job.a,*job.b,job.a_upper,job.convergence,true,job.a_upper,
        job.objects[0],job.objects[1]))return false;
    guard.keep=true;return true;
}
bool present_pipeline::run_mesh_present(){
    ds_gpu::g_capture_plan={};
    return v3ds_prepared_mesh_present(job.mesh,*job.a,*job.b,job.a_upper,job.convergence,true,job.objects[0],job.objects[1]);
}
// Game side: report the previous mesh job like mesh_player_try reports a
// synchronous one. Must run after the worker drained, before new geometry.
static void mesh_pipeline_settle(){
    using namespace present_pipeline;
    if(!mesh_job_pending)return;
    mesh_job_pending=false;
    const bool ok=present_ok;
    if(job.mesh_compiled)bmd_compiled_gpu_result(job.mesh,false,ok);
    if(ok)++mesh_driver_admitted;
    else{
        if(job.mesh_compiled)bmd_compiled_driver_refused();
        mesh_adapter.reset();++mesh_driver_fallback;
        // That frame showed nothing new. The synchronous path falls back to
        // direct geometry within the frame, so use it for a while.
        ++refused;cooldown=60;
        extern const char* g_present_refusal;
        std::printf("Present pipeline refused mesh frame=%llu compiled=%u reason=%s (60 synchronous frames follow)\n",(unsigned long long)ntr::rt_frame(),unsigned(job.mesh_compiled),g_present_refusal);
    }
    job.mesh.reset();
}
// Engine B background planes are double-buffered in the PPU cache, so they
// may be rasterized while the worker still presents the previous frame. All
// other inputs (OBJ plans, engine A scene, clear colour, GX packet) are
// single-buffered and are only touched after wait_idle().
struct EngineBInput {std::optional<ntr::PpuPreparedCandidates> background;bool objects=false;};
static EngineBInput prepare_engine_b_background(){
    EngineBInput input;
    if(!resident_option_enabled("PLAYER_RESIDENT_OBJECTS") || !ds_gpu::native_obj_admit(ntr::ENGINE_B))return input;
    if(auto background=ntr::ppu_prepare_background_candidates_cached(ntr::ENGINE_B))input.background.emplace(*background);
    input.objects=input.background && !input.background->state.has_geometry;
    return input;
}
// minigames-30fps-min: engine A without 3D (2D-only top screens, e.g. Puzzle
// Panic's picture made of 36 sprites: 20 ms of CPU OBJ raster + merge) uses
// the same GPU-resident OBJ path as engine B: BG-only planes (double-buffered,
// may be prepared before wait_idle) + an OBJ plan prepared after it. Not when
// engine A is a display-capture source (the capture is taken before the OBJ
// merge). PLAYER_NO_A_RESIDENT_OBJECTS=1 keeps the CPU OBJ raster.
static bool engine_a_objects_enabled(){
    static const bool value=std::getenv("PLAYER_NO_A_RESIDENT_OBJECTS")==nullptr;
    return value && resident_option_enabled("PLAYER_RESIDENT_OBJECTS");
}
static std::optional<ntr::PpuPreparedCandidates> engine_a_background(bool capture_planned){
    if(capture_planned || !engine_a_objects_enabled() || !ds_gpu::native_obj_admit(ntr::ENGINE_A))return std::nullopt;
    auto background=ntr::ppu_prepare_background_candidates_cached(ntr::ENGINE_A);
    if(!background || background->state.has_geometry || !ds_gpu::native_obj_merge_supported(*background))return std::nullopt;
    return background;
}
static EngineBInput prepare_engine_a_background(bool capture_planned){
    EngineBInput input;
    if(auto background=engine_a_background(capture_planned)){input.background.emplace(*background);input.objects=true;}
    return input;
}
// castle-min30: same result as prepare_engine_b_background(), split around
// engine A's preparation: begin() posts the GPU-affine attempt to the core-1
// helper, end() joins it and completes on the main thread (window cut, or the
// full BG-only path when the affine view is refused).
struct EngineBParallel {bool posted=false,admitted=false;};
static EngineBParallel prepare_engine_b_parallel_begin(){
    using namespace present_pipeline;
    EngineBParallel p;
    if(!resident_option_enabled("PLAYER_RESIDENT_OBJECTS") || !ds_gpu::native_obj_admit(ntr::ENGINE_B))return p;
    p.admitted=true;
    ntr::ppu_sync_vram_views();
    helper_result.reset();__sync_synchronize();helper_pending=true;LightEvent_Signal(&helper_go);p.posted=true;
    return p;
}
static void prepare_engine_b_parallel_end(EngineBParallel& p,EngineBInput& input){
    using namespace present_pipeline;
    input.background.reset();input.objects=false;
    if(!p.admitted)return;
    if(p.posted){LightEvent_Wait(&helper_done);__sync_synchronize();p.posted=false;}
    if(helper_result){ntr::ppu_apply_window_cut(ntr::ENGINE_B,*helper_result);input.background.emplace(*helper_result);helper_result.reset();}
    else if(auto background=ntr::ppu_prepare_background_candidates_cached(ntr::ENGINE_B))input.background.emplace(*background);
    input.objects=input.background && !input.background->state.has_geometry;
}
// After wait_idle(): fill the job's PPU inputs exactly like the synchronous
// prepare lambda (OBJ-resident engine B, otherwise full candidates).
// Early candidates (minigames-30fps-min) were prepared before wait_idle()
// into cache ways the previous job does not read.
struct EarlyCandidates {std::optional<ntr::PpuPreparedCandidates> a,b;};
static void prepare_job_ppu(EngineBInput& input,bool b_capture_view=false,const EarlyCandidates* early=nullptr,EngineBInput* a_input=nullptr){
    using namespace present_pipeline;
    job.a.reset();job.b.reset();job.objects[0]=job.objects[1]=nullptr;
    auto prepare_a=[&]{
        frame_profile::Scope s(frame_profile::MainPpuA);
        if(a_input && a_input->objects){
            {frame_profile::Scope o(frame_profile::MainObjPrepare);job.objects[0]=ds_gpu::native_obj_prepare(ntr::ENGINE_A);}
            if(job.objects[0]){job.a.emplace(*a_input->background);return;}
            a_input->objects=false;
        }
        if(early && early->a)job.a.emplace(*early->a);else job.a.emplace(ntr::ppu_prepare_candidates_cached(ntr::ENGINE_A));
    };
    if(b_capture_view){
        // Engine B shows a GPU capture bank: its PPU planes are not used.
        prepare_a();
        job.b.emplace(*job.a);return;
    }
    if(input.objects){
        {frame_profile::Scope s(frame_profile::MainObjPrepare);job.objects[1]=ds_gpu::native_obj_prepare(ntr::ENGINE_B);}
        if(!job.objects[1])input.objects=false;
    }
    prepare_a();
    if(input.objects)job.b.emplace(*input.background);
    else if(early && early->b)job.b.emplace(*early->b);
    else job.b.emplace(ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B));
}
static void set_native_clear_from_ds(){
    v3ds_set_native_clear(ds_clear::State::capture([](uint32_t a,unsigned w){return ntr::io_read(a,w);}));
}
// Held-frame skip (castle-30fps). Stages update at 30 Hz: the VBlank between
// two game updates re-presents the previous inputs. When every input of the
// last posted frame is provably unchanged (same immutable mesh frame, same
// engine A/B PPU inputs and OBJ plan, clear/fog, convergence, slider) the LCD
// already shows that exact image, so nothing is rebuilt or submitted. All
// checks are read-only and run before waiting for the worker, which keeps
// the second VBlank free of GPU work. SM64DS_PRESENT_HELD_SKIP=0 disables it.
namespace held_skip {
struct Signature {
    std::shared_ptr<const prepared_mesh::Frame> mesh; // keeps the identity alive (no address reuse)
    uint64_t background_generation=0;bool objects=false,a_upper=false;
    ds_clear::State clear;float convergence=0,slider=0;
};
bool valid=false;Signature last;
bool enabled(){static const bool value=[](){const char* s=std::getenv("SM64DS_PRESENT_HELD_SKIP");return !(s&&std::strcmp(s,"0")==0);}();return value;}
float slider(){
    float value=osGet3DSliderState();
    if(const char* forced=std::getenv("SM64DS_FORCE_3D"))value=float(std::atof(forced));
    return value<0?0:value>1?1:value;
}
void invalidate(){valid=false;last.mesh.reset();}
}
// Mesh-path counterpart of the direct path below: same admission as
// mesh_player_try, GPU submission on the worker.
static bool mesh_present_pipelined(){
    using namespace present_pipeline;
    const auto& packet=ntr::gx_native_mesh_packet();
    if(!packet.requested())return false;
    if(!ntr::gx_native_mesh_camera_valid())return false;
    if(ntr::io_display_capture())return false;
    const uint32_t dispcnt=uint32_t(ntr::io_read(0x04000000,4));
    const bool a_upper=ntr::io_read(0x04000304,2)&0x8000;
    if(!resident_option_enabled("PLAYER_RESIDENT_COMPOSITE")||!a_upper||((dispcnt>>16)&3)>=2||
       (ntr::io_read(0x04000064,4)&0x80000000u))return false;
    // castle-min30: on a new game frame (the compiled mesh is not the held
    // one, so the held-frame skip cannot apply) engine B's GPU-affine view is
    // prepared on the core-1 helper in parallel with engine A below.
    const bool new_frame=present_pipeline::helper && !(held_skip::valid && held_skip::enabled() && bmd_compiled_frame_held(held_skip::last.mesh.get()));
    EngineBParallel parallel_b;
    struct JoinGuard {EngineBParallel& p;~JoinGuard(){if(p.posted){LightEvent_Wait(&present_pipeline::helper_done);p.posted=false;present_pipeline::helper_result.reset();}}} join_guard{parallel_b};
    if(new_frame){frame_profile::Scope s(frame_profile::MainPrepareB);parallel_b=prepare_engine_b_parallel_begin();}
    auto engine_b=[&]{frame_profile::Scope s(frame_profile::MainPrepareB);return new_frame?EngineBInput{}:prepare_engine_b_background();}();
    if(!new_frame && held_skip::valid && held_skip::enabled()){
        frame_profile::Scope s(frame_profile::MainHeldCheck);
        const auto& l=held_skip::last;
        const bool same=bmd_compiled_frame_held(l.mesh.get()) && engine_b.objects==l.objects && a_upper==l.a_upper &&
            (engine_b.objects?engine_b.background->background_generation==l.background_generation && ds_gpu::native_obj_unchanged(ntr::ENGINE_B)
                             :ntr::ppu_prepared_inputs_unchanged(ntr::ENGINE_B)) &&
            ntr::ppu_prepared_inputs_unchanged(ntr::ENGINE_A) &&
            ntr::ppu_window_cut_unchanged(ntr::ENGINE_A) && ntr::ppu_window_cut_unchanged(ntr::ENGINE_B) && // minigames-30fps-min
            ds_clear::State::capture([](uint32_t a,unsigned w){return ntr::io_read(a,w);})==l.clear &&
            ntr::gx_native_mesh_convergence()==l.convergence && held_skip::slider()==l.slider;
        if(same){++held_skips;return true;}
    }
    held_skip::invalidate();
    // castle-min30: while the worker still presents the previous frame, prepare
    // engine A into a cache way that job does not read (same rule as the direct
    // path's early preparation, minigames-30fps-min): dialog frames re-raster
    // engine A text while the worker is busy with a heavy scene.
    EarlyCandidates early;
    static const bool early_enabled=std::getenv("PLAYER_NO_EARLY_PREPARE")==nullptr;
    if(new_frame && early_enabled && busy && ntr::ppu_unpinned_way_available(ntr::ENGINE_A)){
        frame_profile::Scope s(frame_profile::MainPpuA);early.a.emplace(ntr::ppu_prepare_candidates_cached(ntr::ENGINE_A));
    }
    {frame_profile::Scope s(frame_profile::MainWaitIdle);wait_idle();}mesh_pipeline_settle();
    if(cooldown)return false; // the previous job was just refused
    const auto compiled=[&]{frame_profile::Scope s(frame_profile::MainCompiledFrame);return bmd_compiled_frame();}();
    if(!compiled && bmd_compiled_geometry())return false;
    if(!compiled && !packet.admitted())return false;
    set_native_clear_from_ds();
    if(new_frame){
        frame_profile::Scope s(frame_profile::MainPreparePpu);
        // Engine A first (overlaps the helper), then the joined engine B.
        if(!early.a){frame_profile::Scope s(frame_profile::MainPpuA);early.a.emplace(ntr::ppu_prepare_candidates_cached(ntr::ENGINE_A));}
        {frame_profile::Scope s(frame_profile::MainPrepareB);prepare_engine_b_parallel_end(parallel_b,engine_b);}
        prepare_job_ppu(engine_b,false,&early);
    }else{frame_profile::Scope s(frame_profile::MainPreparePpu);prepare_job_ppu(engine_b);}
    job.capture={};
    if(!v3ds_prepared_mesh_admits(*job.a,*job.b,a_upper))return false; // synchronous path falls back within the frame
    const auto publication=compiled?native_mesh::FrameAdapter::Publication{}:mesh_adapter.capture(packet,ntr::gx_native_mesh_convergence());
    const auto frame=compiled?compiled:publication.frame;
    if(!frame){
        if(compiled)bmd_compiled_driver_refused();
        mesh_adapter.reset();++mesh_driver_fallback;return false;
    }
    job.mesh=frame;job.mesh_compiled=bool(compiled);
    job.convergence=compiled?ntr::gx_native_mesh_convergence():publication.convergence;
    job.a_upper=a_upper;
    if(!audio_ok)return true; // reported by the caller's audio check
    if(compiled && engine_b.background){
        // Only compiled (immutable, store-held) frames are skip candidates.
        auto& l=held_skip::last;l.mesh=frame;l.objects=engine_b.objects;l.a_upper=a_upper;
        l.background_generation=engine_b.background->background_generation;
        l.clear=ds_clear::State::capture([](uint32_t a,unsigned w){return ntr::io_read(a,w);});
        l.convergence=job.convergence;l.slider=held_skip::slider();held_skip::valid=true;
    }
    mesh_job_pending=true;job.present=true;post();++pipelined;
    return true;
}
// Main-thread half of a pipelined frame. Returns false when this frame needs
// the ordinary synchronous presentation (the caller then drains the worker).
static bool player_present_pipelined(){
    using namespace present_pipeline;
    if(!enabled)return false;
    if(cooldown){--cooldown;return false;}
    if(!ntr::gx_direct_enabled() || !std::getenv("PLAYER_NATIVE_COMPOSITE") ||
       std::getenv("PLAYER_DEPTH_PROBE") || std::getenv("PLAYER_ALPHA_PROBE") || !ntr::rt_running())return false;
    if(ntr::gx_native_mesh_packet().requested() || bmd_compiled_geometry())return mesh_present_pipelined();
    const auto& packet=ntr::gx_direct_packet();
    if(packet.triangles().size()>0x8000/3 || packet.vertices().size()>0x8000)return false;
    if(ntr::io_display_capture())return false;
    const uint32_t display_a=uint32_t(ntr::io_read(0x04000000,4));
    capture_gpu::Frame capture_frame;
    if(!resident_option_enabled("PLAYER_RESIDENT_COMPOSITE") || !capture_gpu::plan(capture_frame) ||
       ((display_a>>16)&3)>=2)return false;
    const bool a_upper=ntr::io_read(0x04000304,2)&0x8000;
    if(capture_frame.writeback>=0){wait_idle();mesh_pipeline_settle();capture_gpu::writeback(capture_frame);}
    auto engine_b=capture_frame.plan.view?EngineBInput{}:prepare_engine_b_background();
    auto engine_a=prepare_engine_a_background(capture_frame.plan.capture);
    // minigames-30fps-min: while the worker still presents the previous
    // frame, prepare this frame's engine A (and full engine B) candidates
    // into PPU cache ways that job does not read, instead of after waiting.
    // PLAYER_NO_EARLY_PREPARE=1 keeps the preparation after wait_idle().
    EarlyCandidates early;
    static const bool early_enabled=std::getenv("PLAYER_NO_EARLY_PREPARE")==nullptr;
    if(early_enabled && busy && ntr::ppu_unpinned_way_available(ntr::ENGINE_A)){
        const bool need_b=!capture_frame.plan.view && !engine_b.objects;
        if(!need_b || ntr::ppu_unpinned_way_available(ntr::ENGINE_B)){
            frame_profile::Scope s(frame_profile::MainPpuA);
            if(!engine_a.objects)early.a.emplace(ntr::ppu_prepare_candidates_cached(ntr::ENGINE_A));
            if(need_b)early.b.emplace(ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B));
        }
    }
    wait_idle();mesh_pipeline_settle();
    if(cooldown)return false; // the previous job was just refused
    if(!present_ok){
        present_ok=true;++refused;cooldown=60;
        std::printf("Present pipeline refused direct frame=%llu (60 synchronous frames follow)\n",(unsigned long long)ntr::rt_frame());
        return false;
    }
    set_native_clear_from_ds();
    prepare_job_ppu(engine_b,capture_frame.plan.view,&early,&engine_a);
    job.capture=capture_frame.plan;
    job.mesh.reset();
    job.gpu=ntr::gx_gpu_publish();
    if(job.gpu)job.packet.clear();else job.packet=packet;
    job.a_upper=a_upper;
    job.convergence=ntr::gx_native_mesh_convergence_or(direct_convergence);
    if(!audio_ok)return true; // reported by the caller's audio check
    capture_gpu::commit(capture_frame);
    job.present=true;post();++pipelined;
    return true;
}
#endif

bool player_present(bool capture,bool perspective){
    frame_profile::Scope total_scope(frame_profile::MainPresentTotal);
#ifdef SM64DS_PRESENT_PIPELINE
    if(!capture && player_present_pipelined())return true;
    frame_profile::Scope sync_scope(frame_profile::MainSynchronous);
    held_skip::invalidate();
    present_pipeline::wait_idle();
    mesh_pipeline_settle();
    if(present_pipeline::enabled)++present_pipeline::synchronous;
#endif
    if(capture&&std::getenv("PLAYER_NATIVE_MESH_RAW_WITNESS"))
        ntr::gx_native_mesh_capture_raw("raw-matrix-capture.bin");
    v3ds_set_native_clear(ds_clear::State::capture([](uint32_t a,unsigned w){return ntr::io_read(a,w);}));
    if(mesh_player_try(capture))return true;
    if(!ntr::gx_direct_enabled() || !std::getenv("PLAYER_NATIVE_COMPOSITE") ||
       std::getenv("PLAYER_DEPTH_PROBE") || std::getenv("PLAYER_ALPHA_PROBE")){
        direct_ready=false;return player_present_legacy(capture,perspective);
    }
    const double batch_start=v3ds_time_ms();
    const auto& packet=ntr::gx_direct_packet();
    if(packet.triangles().size()>0x8000/3){std::fprintf(stderr,"FATAL: Player GPU vertex capacity exceeded\n");return false;}
    if(packet.vertices().size()>0x8000){
        // Many discarded/unfinished primitives can have little output. Keep
        // exact legacy handling rather than imposing a new silent truncation.
        direct_ready=false;return player_present_legacy(capture,perspective);
    }
    const ntr::gx_gpu::Frame* gpu_frame=ntr::gx_gpu_publish();
    if(gpu_frame){
        // GPU geometry mode: index the recorded frame (no reuse across calls).
        V3dsDirectLease lease{};
        if(!v3ds_arena::acquire(lease))return false;
        DirectGuard preparation_guard{lease.frame_token};
        direct_ready=false;
        if(!gx_gpu_viewer::build(*gpu_frame,lease,direct_batches,direct_prepared,direct_convergence,
                                 (ntr::io_read(0x04000304,2)&0x8000)!=0) || // menu-stereo: 3D on the upper LCD
           !direct_resources.freeze(direct_batches.data(),direct_prepared.batch_count)){
            v3ds_arena::abort(lease.frame_token);preparation_guard.keep=true;
            std::fprintf(stderr,"GX GPU geometry frame refused (synchronous)\n");return false;
        }
        direct_source=nullptr;preparation_guard.keep=true;
    }else
    if(!direct_reuse || !direct_ready || direct_source!=&packet ||
       direct_prepared.geometry_generation!=packet.generation() ||
       !v3ds_arena::valid(direct_prepared) || !direct_resources.external_unchanged()){
        V3dsDirectLease lease{};
        if(!v3ds_arena::acquire(lease))return false;
        DirectGuard preparation_guard{lease.frame_token};
        direct_ready=false;
        direct_builder.reserve(packet.vertices().size());
        const auto nb=packet.triangles().size()+1,nd=packet.triangles().size()*5+1;
        if(direct_batches.size()<nb)direct_batches.resize(nb);
        if(direct_depths.size()<nd)direct_depths.resize(nd);
        if(!direct_builder.build(packet,{lease.vertices,lease.vertex_capacity,
            direct_batches.data(),direct_batches.size(),direct_depths.data(),direct_depths.size(),lease.frame_token})){
            std::fprintf(stderr,"FATAL: clipped GPU vertex capacity exceeded\n");return false;
        }
        if(!direct_resources.freeze(direct_batches.data(),direct_builder.batch_count())){
            // Explicit bounded resources refusal: retire the acquired list
            // before the unchanged legacy driver reconstructs polygons.
            v3ds_arena::abort(lease.frame_token);preparation_guard.keep=true;
            return player_present_legacy(capture,perspective);
        }
        direct_prepared=direct_builder.prepared();direct_source=&packet;
        direct_convergence=direct_builder.convergence();direct_ready=true;
        preparation_guard.keep=true;
    }
    const auto prepared=direct_prepared;
    // The original camera is the shared CPU/GPU stereo authority.
    // The legacy median is still built; no matrix/viewport/raster is changed.
    const float convergence=ntr::gx_native_mesh_convergence_or(direct_convergence);
    if(capture)std::printf("CPU stereo convergence camera=%.9f legacyMedian=%.9f\n",convergence,direct_convergence);
    const bool probe=false;
    const double batch_ms=v3ds_time_ms()-batch_start;
    DirectGuard guard{prepared.frame_token};
    if(!probe && std::getenv("PLAYER_NATIVE_COMPOSITE")){
        static ntr::Framebuffer geometry_left,geometry_right,engine_left,engine_right,engine_b;
        static ntr::HostVector<ntr::Framebuffer> capture_frames;
        const bool a_upper=ntr::io_read(0x04000304,2)&0x8000;
        const double native_start=v3ds_time_ms();
        if(!ntr::rt_running())ntr::io_complete_unmapped_capture_frame();
        auto* ds_capture=ntr::io_display_capture();
        const uint32_t display_a=uint32_t(ntr::io_read(0x04000000,4));
        const unsigned display_mode=(display_a>>16)&3;
        ntr::DisplayCaptureMemory capture_memory{};
        ntr::DisplayCaptureSources capture_sources{};
        bool capture_pending=false;
        bool capture_centre_rendered=false;
        if(ds_capture){
            auto* banks=ntr::io_vram_banks();
            for(unsigned bank=0;bank<4;++bank)capture_memory.banks[bank]=banks->physical(bank);
            capture_memory.lcdc_mask=banks->lcdc_mask();capture_memory.context=banks;
            capture_memory.written=[](void* context,unsigned bank,unsigned at,unsigned bytes){
                static_cast<ntr::VramBanks*>(context)->changed(bank,at,bytes);
            };
            capture_pending=ds_capture->latched() || (ds_capture->control()&0x80000000u);
            capture_sources=ds_capture->sources(0,false,capture_memory.lcdc_mask);
            if(display_mode==3 || capture_sources.fifo){
                std::fprintf(stderr,"FATAL: physical VRAM display/capture FIFO requires an unimplemented queue/DMA backend\n");return false;
            }
            if(capture_sources.composite || capture_sources.geometry)capture_frames.resize(3);
            // Prepare/read back only requested source A. Render the DS centre
            // before the ordinary eye pass, which restores native stereo state.
            // Neither stereo eye is a valid replacement for the capture source.
            if(capture_sources.composite){
                auto& capture_geometry=capture_frames[0];auto& capture_scratch=capture_frames[1];
                auto& capture_composite=capture_frames[2];
                const auto candidates=ntr::ppu_prepare_capture_candidates();
                if(candidates.state.has_geometry){
                    if(!v3ds_arena::geometry(prepared,
                        &capture_geometry.px[0][0],&capture_scratch.px[0][0],convergence,true,false))return false;
                    capture_centre_rendered=true;
                }
                ntr::ppu_compose_candidates(candidates,capture_geometry,capture_composite,false);
            }else if(capture_sources.geometry){
                if(!v3ds_arena::geometry(prepared,
                    &capture_frames[0].px[0][0],&capture_frames[1].px[0][0],convergence,true,false))return false;
                capture_centre_rendered=true;
            }
        }
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
        // Physical capture consumes a separately rendered DS centre and may
        // feed the next scanline. Keep those cases in the proven CPU driver.
        // The resident wrapper rejects unsupported effects before drawing.
        capture_gpu::Frame capture_frame;
        const bool capture_planned=capture_gpu::plan(capture_frame);
        {   // Report why the resident composite is not even attempted (once per change).
            static unsigned last=~0u;
            const unsigned why=(capture_pending?1u:0u)|(capture_sources.active?2u:0u)|(capture_planned?0u:4u)|(display_mode>=2?8u:0u);
            if(why!=last){last=why;if(why)std::printf("Resident composite skipped frame=%llu pending=%u sourcesActive=%u planRefused=%u displayMode=%u\n",
                (unsigned long long)ntr::rt_frame(),unsigned(capture_pending),unsigned(capture_sources.active),unsigned(!capture_planned),display_mode);}
        }
        if(resident_option_enabled("PLAYER_RESIDENT_COMPOSITE") && !capture_pending &&
           !capture_sources.active && capture_planned && display_mode<2){
            capture_gpu::writeback(capture_frame);
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
            const ds_gpu::ObjPlan* objects[2]{};
            const bool resident_objects=resident_option_enabled("PLAYER_RESIDENT_OBJECTS");
            std::optional<ntr::PpuPreparedCandidates> engine_a_view;
            auto prepare=[&](ntr::Engine engine){
                // minigames-2d-gpu: B shows a GPU capture bank (planes unused).
                if(engine==ntr::ENGINE_B && capture_frame.plan.view && engine_a_view)return *engine_a_view;
                if(engine==ntr::ENGINE_A)if(auto background=engine_a_background(capture_frame.plan.capture)){ // minigames-30fps-min
                    objects[engine]=ds_gpu::native_obj_prepare(engine);
                    if(objects[engine])return *background;
                }
                if(resident_objects && engine==ntr::ENGINE_B && ds_gpu::native_obj_admit(engine)){
                    const auto background=ntr::ppu_prepare_background_candidates_cached(engine);
                    if(background && !background->state.has_geometry){
                        objects[engine]=ds_gpu::native_obj_prepare(engine);
                        if(objects[engine])return *background;
                    }
                }
                return ntr::ppu_prepare_candidates_cached(engine);
            };
            const auto a=prepare(ntr::ENGINE_A);engine_a_view.emplace(a);const auto b=prepare(ntr::ENGINE_B);
#else
            const auto a=ntr::ppu_prepare_candidates_cached(ntr::ENGINE_A);
            const auto b=ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B);
#endif
            ds_gpu::g_capture_plan=capture_frame.plan;
            if(v3ds_arena::present_candidates(prepared,
                a,b,a_upper,convergence,true,a_upper
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
                ,objects[0],objects[1]
#endif
                )){
                v3ds_profile(0,batch_ms);
                if(capture){
                    // Explicit diagnostics only. Presentation itself keeps
                    // geometry and both composed panels resident on the GPU.
                    v3ds_gpu_candidate_geometry(&geometry_left.px[0][0],&geometry_right.px[0][0]);
                    auto& upper_left=a_upper?engine_left:engine_b;
                    auto& upper_right=a_upper?engine_right:engine_b;
                    auto& lower=a_upper?engine_b:engine_left;
                    v3ds_gpu_candidate_panel(0,&upper_left.px[0][0]);
                    if(a_upper && v3ds_native_stereo())v3ds_gpu_candidate_panel(1,&upper_right.px[0][0]);
                    v3ds_gpu_candidate_panel(2,&lower.px[0][0]);
                    ntr::ppu_write_bmp("player-native-geometry.bmp",geometry_left);
                    ntr::ppu_write_bmp("player-native-geometry-right.bmp",geometry_right);
                    ntr::ppu_write_bmp("player-native-upper.bmp",upper_left);
                    ntr::ppu_write_bmp("player-native-bottom.bmp",lower);
                    static uint32_t pixels[400*240];
                    v3ds_read_top(pixels,false);write_bmp400("player-gpu-left.bmp",pixels);
                    v3ds_read_top(pixels,true);write_bmp400("player-gpu-right.bmp",pixels);
                }
                capture_gpu::commit(capture_frame);
                guard.keep=true;return true;
            }
            if(capture_frame.plan.capture || capture_frame.plan.view || std::getenv("PLAYER_PRESENT_TRACE")){
                extern const char* g_present_refusal;
                std::printf("Resident capture frame refused reason=%s view=%u capture=%u\n",g_present_refusal,
                    unsigned(capture_frame.plan.view),unsigned(capture_frame.plan.capture));
            }
        }
#endif
        // Resident OBJ preparation returns BG-only planes. Any refusal above
        // falls through to ordinary preparation below, including OBJ raster.
        if(!v3ds_arena::geometry(prepared,
            &geometry_left.px[0][0],&geometry_right.px[0][0],convergence,true,a_upper))return false;
        const double geometry_end=v3ds_time_ms();
        if(std::getenv("PLAYER_PREPARED_PPU")){
            const bool profile_ppu=std::getenv("PLAYER_PROFILE_PPU")!=nullptr;
            ntr::ppu_prepare_profile_clock(profile_ppu?v3ds_time_ms:nullptr);
            double stamp[5]{};
            if(profile_ppu)stamp[0]=v3ds_time_ms();
            const auto& a=ntr::ppu_prepare_scene_cached(ntr::ENGINE_A);
            if(profile_ppu)stamp[1]=v3ds_time_ms();
            ntr::ppu_compose_prepared(a,geometry_left,engine_left);
            if(v3ds_native_stereo())ntr::ppu_compose_prepared(a,geometry_right,engine_right);
            if(profile_ppu)stamp[2]=v3ds_time_ms();
            const auto& b=ntr::ppu_prepare_scene_cached(ntr::ENGINE_B);
            if(profile_ppu)stamp[3]=v3ds_time_ms();
            ntr::ppu_compose_prepared(b,geometry_left,engine_b);
            if(profile_ppu){
                stamp[4]=v3ds_time_ms();
                static double totals[4]{};static unsigned count=0;
                for(unsigned i=0;i<4;++i)totals[i]+=stamp[i+1]-stamp[i];
                if(++count==60){
                    std::printf("Prepared PPU parts ms: prepareA=%.3f composeA=%.3f prepareB=%.3f composeB=%.3f\n",
                        totals[0]/count,totals[1]/count,totals[2]/count,totals[3]/count);
                    std::printf("Prepared PPU state: A blend=%04x master=%04x geometry=%u windows=%u; B blend=%04x master=%04x resolved=%u\n",
                        a.blend,a.master,unsigned(a.has_geometry),unsigned(a.has_windows),b.blend,b.master,unsigned(b.resolved));
                    for(auto eng:{ntr::ENGINE_A,ntr::ENGINE_B}){
                        const auto p=ntr::ppu_prepare_profile_read(eng,true);
                        std::printf("PPU preparation engine=%u calls=%llu total ms: windows=%.3f bg=%.3f objects=%.3f resolve=%.3f\n",
                            unsigned(eng),(unsigned long long)p.calls,p.windows,p.bg,p.objects,p.resolve);
                    }
                    count=0;for(auto& total:totals)total=0;
                }
            }
        }else{
            ntr::ppu_scanout_scene_with_geometry(ntr::ENGINE_A,engine_left,geometry_left);
            if(v3ds_native_stereo())ntr::ppu_scanout_scene_with_geometry(ntr::ENGINE_A,engine_right,geometry_right);
            ntr::ppu_scanout_scene_with_geometry(ntr::ENGINE_B,engine_b,geometry_left);
        }
        if(ds_capture && (capture_pending || display_mode==2)){
            // In display mode2 scan each physical VRAM row before capturing
            // that row. Ordinary mode1 retains the independent stereo images.
            const auto result=ds_capture->frame(display_a,uint16_t(ntr::io_read(0x0400006c,2)),capture_memory,
                capture_sources.composite?&capture_frames[2].px[0][0]:nullptr,
                capture_sources.geometry?&capture_frames[0].px[0][0]:nullptr,
                display_mode==2?&engine_left.px[0][0]:nullptr);
            if(result.status==ntr::DisplayCaptureStatus::MissingComposite ||
               result.status==ntr::DisplayCaptureStatus::MissingGeometry ||
               result.status==ntr::DisplayCaptureStatus::MissingFifo){
                std::fprintf(stderr,"FATAL: physical display capture lacks source status=%u\n",unsigned(result.status));return false;
            }
            if(display_mode==2)engine_right=engine_left;
            ntr::io_display_capture_vblank();
        }
        const auto& upper_left=a_upper?engine_left:engine_b;
        const auto& upper_right=a_upper?(v3ds_native_stereo()?engine_right:engine_left):engine_b;
        const auto& lower=a_upper?engine_b:engine_left;
        const double composition_end=v3ds_time_ms();
        v3ds_present_native(&upper_left.px[0][0],&upper_right.px[0][0],&lower.px[0][0],a_upper);
        const double present_end=v3ds_time_ms();
        static double timings[3]{};static unsigned samples=0;
        timings[0]+=geometry_end-native_start;timings[1]+=composition_end-geometry_end;timings[2]+=present_end-composition_end;
        if(++samples==60){
            std::printf("Native composite ms: GPU/readback=%.3f PPU=%.3f transfer=%.3f\n",timings[0]/60,timings[1]/60,timings[2]/60);
            uint64_t hits,misses;ntr::ppu_prepared_cache_stats(hits,misses);
            std::printf("Prepared PPU cache: hits=%llu misses=%llu\n",(unsigned long long)hits,(unsigned long long)misses);
            ntr::ppu_background_cache_stats(hits,misses);
            std::printf("PPU background cache: hits=%llu misses=%llu\n",(unsigned long long)hits,(unsigned long long)misses);
            samples=0;for(auto& time:timings)time=0;
        }
        v3ds_profile(0,batch_ms);
        if(capture){
            ntr::ppu_write_bmp("player-native-geometry.bmp",geometry_left);
            ntr::ppu_write_bmp("player-native-geometry-right.bmp",geometry_right);
            if(capture_centre_rendered)ntr::ppu_write_bmp("player-native-capture-centre.bmp",capture_frames[0]);
            if(capture_sources.composite)ntr::ppu_write_bmp("player-native-capture-source.bmp",capture_frames[2]);
            ntr::ppu_write_bmp("player-native-upper.bmp",upper_left);
            ntr::ppu_write_bmp("player-native-bottom.bmp",lower);
            static uint32_t pixels[400*240];
            v3ds_read_top(pixels,false);write_bmp400("player-gpu-left.bmp",pixels);
            v3ds_read_top(pixels,true);write_bmp400("player-gpu-right.bmp",pixels);
        }
        guard.keep=true;return true;
    }
    return false;
}
