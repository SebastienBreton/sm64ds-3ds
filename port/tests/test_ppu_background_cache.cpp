// Host-only full-frame checks against the uncached PPU scanout.
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace ntr {
uint8_t* g_region[8]{};bool g_region_identity[8]{};
bool io_ready(){return true;}
uint64_t io_read(uint32_t a,unsigned n){uint64_t v=0;std::memcpy(&v,host_ptr(a),n);return v;}
void gx_render(Framebuffer&){std::abort();}
void ppu_background_cache_stats(uint64_t&,uint64_t&);
}
static void w16(unsigned a,unsigned value){*static_cast<uint16_t*>(ntr::host_ptr(a))=value;}
static void w32(unsigned a,unsigned value){*static_cast<uint32_t*>(ntr::host_ptr(a))=value;}
static ntr::Framebuffer geometry,direct,cached;
static unsigned cases;
static void compare(ntr::Engine engine){
    ntr::ppu_scanout_scene_with_geometry(engine,direct,geometry);
    ntr::ppu_compose_prepared(ntr::ppu_prepare_scene_cached(engine),geometry,cached);
    assert(!std::memcmp(&direct,&cached,sizeof direct));++cases;
}
int main(){
    for(unsigned i=2;i<8;++i)ntr::g_region[i]=static_cast<uint8_t*>(std::calloc(1,0x1000000));
    for(auto engine:{ntr::ENGINE_A,ntr::ENGINE_B}){
        const unsigned base=engine==ntr::ENGINE_A?0x04000000:0x04001000;
        const unsigned bg=engine==ntr::ENGINE_A?0x06000000:0x06200000;
        const unsigned obj=engine==ntr::ENGINE_A?0x06400000:0x06600000;
        const unsigned pal=engine==ntr::ENGINE_A?0x05000000:0x05000400;
        const unsigned oam=engine==ntr::ENGINE_A?0x07000000:0x07000400;
        for(unsigned i=0;i<128;++i)w16(oam+i*8,0x200);
        w32(base,0x11310);w16(base+8,(24<<8)|1);w16(base+10,(25<<8)|(1<<2)|2);
        std::memset(ntr::host_ptr(bg),0x11,32);
        std::memset(ntr::host_ptr(bg+0x4000),0x22,32);
        std::memset(ntr::host_ptr(obj),0x11,32);
        w16(pal,0x7c00);w16(pal+2,0x001f);w16(pal+4,0x03e0);w16(pal+0x202,0x7fff);
        w16(oam,40);w16(oam+2,10);w16(oam+4,0);
        compare(engine);
        for(unsigned frame=0;frame<96;++frame){
            uint64_t h0,m0,h1,m1;ntr::ppu_background_cache_stats(h0,m0);
            switch(frame%4){
                case 0:w16(oam+2,11+frame);break;
                case 1:w16(pal+0x202,frame*53);break;
                case 2:*static_cast<uint8_t*>(ntr::host_ptr(obj))^=0x11;break;
                case 3:w16(base+6,frame);break; // VCOUNT is not a BG sampling input.
            }
            compare(engine);ntr::ppu_background_cache_stats(h1,m1);
            if(frame%4==3)assert(h1==h0 && m1==m0); // Entire scene reused.
            else assert(h1==h0+1 && m1==m0);
        }
        // Each category read by the BG sampler must invalidate its planes.
        const unsigned mutations[]={base+0x10,base+8,pal+2,bg,bg+(24<<11),0x04000240};
        for(auto address:mutations){
            uint64_t h0,m0,h1,m1;ntr::ppu_background_cache_stats(h0,m0);
            *static_cast<uint8_t*>(ntr::host_ptr(address))^=1;
            compare(engine);ntr::ppu_background_cache_stats(h1,m1);
            assert(m1==m0+1);
        }
        // OBJ-window motion changes BG visibility and cannot reuse old planes.
        w32(base,0x19310);w16(oam,0x0800|40);w16(base+0x4a,0x0201);compare(engine);
        for(unsigned x=20;x<28;++x){
            uint64_t h0,m0,h1,m1;ntr::ppu_background_cache_stats(h0,m0);
            w16(oam+2,x);compare(engine);ntr::ppu_background_cache_stats(h1,m1);
            assert(m1==m0+1);
        }
        w32(base,0x11310);w16(oam,40);compare(engine);
        auto time=[&](bool uncached){
            if(uncached)setenv("PLAYER_UNCACHED_BACKGROUNDS","1",1);
            else unsetenv("PLAYER_UNCACHED_BACKGROUNDS");
            const auto start=std::chrono::steady_clock::now();
            for(unsigned frame=0;frame<200;++frame){
                w16(oam+2,frame%200);ntr::ppu_prepare_scene_cached(engine);
            }
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;
        };
        const double without=time(true),with=time(false);
        std::printf("Host preparation engine=%u ms/frame: uncached=%.6f BGcache=%.6f\n",unsigned(engine),without,with);
    }
    std::printf("PASS %u full frames: moving OBJ/palette/tiles/VCOUNT reuse BG; BG inputs and OBJ-window masks invalidate\n",cases);
}
