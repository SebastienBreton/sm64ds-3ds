#include <cstring>
#include "native_obj_input_views.h"
#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include "ntr/ppu.h"

namespace ds_gpu {
namespace {
struct EngineInput {
    ObjPlan plan;
    // Admission result kept apart from the plan: admission runs before the
    // presentation worker is idle, the plan may still be read by that worker.
    ObjFallback admission=ObjFallback::None;
    ntr::HostVector<uint8_t> bytes;
    ntr::VramBanks* attached=nullptr;
    ntr::VramPublishedView published;
    explicit EngineInput(ntr::VramView view,unsigned size):published{view,nullptr,size}{}
    const uint8_t* view(ntr::VramBanks* bus,uint32_t address){
        if(!bus){attached=nullptr;return static_cast<const uint8_t*>(ntr::host_ptr(address));}
        if(attached!=bus){
            attached=bus;bytes.resize(published.bytes);
            published.output=bytes.data();published.invalidate();
        }
        published.publish(*bus);return bytes.data();
    }
};
EngineInput inputs[]={EngineInput(ntr::VramView::AObj,0x40000),
                      EngineInput(ntr::VramView::BObj,0x20000)};
}
// minigames-30fps-min: binary windows (scene wipes) are painted over the final
// panel by the window cut; the OBJ layer itself is then drawn without them.
static uint32_t binary_window_display(ntr::Engine engine,uint32_t display){
    return (display&0xe000) && ntr::ppu_windows_binary(engine)?display&~0xe000u:display;
}
bool native_obj_admit(ntr::Engine engine){
    auto& state=inputs[engine==ntr::ENGINE_B?1:0];
    if(!ntr::io_ready()){state.admission=ObjFallback::MissingViews;return false;}
    const bool b=engine==ntr::ENGINE_B;
    const auto display=binary_window_display(engine,static_cast<uint32_t>(ntr::io_read(b?0x04001000:0x04000000,4)));
    const auto* oam=static_cast<const uint8_t*>(ntr::host_ptr(b?0x07000400:0x07000000));
    state.admission=obj_admission(display,oam);
    return state.admission==ObjFallback::None;
}
static const ObjPlan* native_obj_prepare_impl(ntr::Engine engine);
const ObjPlan* native_obj_prepare(ntr::Engine engine){
    auto* clock=ntr::ppu_prepare_profile_clock_get();
    const double start=clock?clock():0;
    const auto* plan=native_obj_prepare_impl(engine);
    if(clock)ntr::ppu_prepare_profile_add_obj_plan(engine,clock()-start);
    return plan;
}
static const ObjPlan* native_obj_prepare_impl(ntr::Engine engine){
    auto& state=inputs[engine==ntr::ENGINE_B?1:0];
    if(!native_obj_admit(engine)){state.plan.count=0;state.plan.fallback=state.admission;return nullptr;}
    try{
    const bool b=engine==ntr::ENGINE_B;
    ObjInput input;
    input.display=binary_window_display(engine,static_cast<uint32_t>(ntr::io_read(b?0x04001000:0x04000000,4)));
    input.oam=static_cast<const uint8_t*>(ntr::host_ptr(b?0x07000400:0x07000000));
    input.palette=static_cast<const uint8_t*>(ntr::host_ptr(b?0x05000600:0x05000200));
    input.vram=state.view(ntr::io_vram_banks(),b?0x06600000:0x06400000);
    input.vram_mask=b?0x1ffff:0x3ffff;
    return state.plan.prepare(input)?&state.plan:nullptr;
    }catch(const std::bad_alloc&){state.plan.fallback=ObjFallback::MissingViews;return nullptr;}
}
// Read-only (castle-30fps): prepares a private plan from the current OAM,
// palette and VRAM and compares it with the last prepared plan, which the
// presentation worker may still be reading. Never touches that plan.
bool native_obj_unchanged(ntr::Engine engine){
    if(!ntr::io_ready() || ntr::io_vram_banks())return false;
    auto& state=inputs[engine==ntr::ENGINE_B?1:0];
    const auto& last=state.plan;
    if(last.fallback!=ObjFallback::None)return false;
    static ObjPlan scratch[2];auto& plan=scratch[engine==ntr::ENGINE_B?1:0];
    const bool b=engine==ntr::ENGINE_B;
    ObjInput input;
    input.display=binary_window_display(engine,static_cast<uint32_t>(ntr::io_read(b?0x04001000:0x04000000,4)));
    input.oam=static_cast<const uint8_t*>(ntr::host_ptr(b?0x07000400:0x07000000));
    input.palette=static_cast<const uint8_t*>(ntr::host_ptr(b?0x05000600:0x05000200));
    input.vram=static_cast<const uint8_t*>(ntr::host_ptr(b?0x06600000:0x06400000));
    input.vram_mask=b?0x1ffff:0x3ffff;
    try{if(!plan.prepare(input))return false;}catch(const std::bad_alloc&){return false;}
    if(plan.count!=last.count || std::memcmp(plan.order.data(),last.order.data(),plan.count))return false;
    for(unsigned i=0;i<plan.count;++i){
        const auto& x=plan.sprites[plan.order[i]];const auto& y=last.sprites[plan.order[i]];
        if(x.index!=y.index || x.priority!=y.priority || x.width!=y.width || x.height!=y.height || x.palette_bytes!=y.palette_bytes ||
           x.x!=y.x || x.y!=y.y || x.hflip!=y.hflip || x.vflip!=y.vflip || x.bpp8!=y.bpp8 || x.affine!=y.affine ||
           x.pa!=y.pa || x.pd!=y.pd || x.pb!=y.pb || x.pc!=y.pc || x.bound_width!=y.bound_width || x.bound_height!=y.bound_height ||
           x.clip_x0!=y.clip_x0 || x.clip_x1!=y.clip_x1 || x.clip_y0!=y.clip_y0 || x.clip_y1!=y.clip_y1 ||
           x.clip_y_second0!=y.clip_y_second0 || x.clip_y_second1!=y.clip_y_second1 ||
           x.encoded.size()!=y.encoded.size() || std::memcmp(x.encoded.data(),y.encoded.data(),x.encoded.size()) ||
           std::memcmp(x.palette.data(),y.palette.data(),x.palette_bytes))return false;
    }
    return true;
}
ObjFallback native_obj_fallback(ntr::Engine engine){const auto& s=inputs[engine==ntr::ENGINE_B?1:0];return s.admission!=ObjFallback::None?s.admission:s.plan.fallback;}
}
