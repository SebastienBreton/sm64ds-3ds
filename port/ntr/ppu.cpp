// DS 2D engine, text-mode backgrounds.
//
// Register layout from GBATEK. The address arithmetic is cross-checked against
// the decomp's own G2::GetBG0ScrPtr, which computes
//
//     0x6000000 + ((DISPCNT & 0x38000000) >> 27 << 16) + ((BG0CNT & 0x1f00) >> 8 << 11)
//
// i.e. DISPCNT bits 27-29 select a 64K screen-base offset and BGxCNT bits 8-12 a
// 2K screen block. Those two shifts are the ones most likely to be wrong, so they
// are taken from code that is byte-verified against the ROM rather than from docs.

#include "ntr/scanline_windows.h"
#include "ntr/ppu.h"
#include "ntr/ppu_plane_version.h"
#include "ntr/color_effects.h"
#include "ntr/bitmap.h"
#include "ntr/vram_bus.h"

#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "ntr/host_allocator.h"
#include <algorithm>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <atomic>

// castle-min30: handshake with the presentation side (gpu_composite.cpp): the
// GPU affine path is used only once its VRAM surface exists. Absent in builds
// without the composite (weak): the old unconditional use.
extern "C" volatile int sm64ds_gpu_affine_ready __attribute__((weak));
extern "C" volatile int sm64ds_gpu_affine_wanted __attribute__((weak));
namespace ntr {
namespace {

// Engine A registers start at 0x4000000, engine B at 0x4001000. Engine B has no
// DISPCNT char/screen base offset -- those bits are engine A only -- and its BG
// VRAM and palette live elsewhere.
struct EngineMap {
    uint32_t reg_base;
    uint32_t vram_base;
    uint32_t pltt_base;
    bool has_base_offset;
};

constexpr EngineMap kEngines[2] = {
    {0x04000000u, 0x06000000u, 0x05000000u, true},
    {0x04001000u, 0x06200000u, 0x05000400u, false},
};

// SDK CPU writes and capture update physical banks. Keep the old contiguous
// raster windows as read-only exports so existing samplers and exact snapshot
// caches remain coherent without copying domains whose contents did not change.
void sync_vram_views(){
    auto* bus=io_vram_banks();if(!bus)return;
    static VramBanks* previous=nullptr;
    static VramPublishedView views[4]{
        {VramView::ABg,nullptr,0x80000},{VramView::AObj,nullptr,0x40000},
        {VramView::BBg,nullptr,0x20000},{VramView::BObj,nullptr,0x20000}};
    static uint64_t palettes[5]{UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX};
    const unsigned addresses[4]{0x06000000,0x06400000,0x06200000,0x06600000};
    const unsigned physical[5]{0x06880000,0x06890000,0x06894000,0x06898000,0x068a0000};
    if(bus!=previous){for(auto& v:views)v.invalidate();for(auto& v:palettes)v=UINT64_MAX;previous=bus;}
    for(unsigned i=0;i<4;++i){views[i].output=static_cast<uint8_t*>(host_ptr(addresses[i]));views[i].publish(*bus);}
    for(unsigned i=0;i<5;++i)if(palettes[i]!=bus->bank_generation(i+4)){
        auto* output=host_ptr(physical[i]);const auto* input=bus->physical(i+4);
        if(input)std::memcpy(output,input,VramBanks::bank_size(i+4));else std::memset(output,0,VramBanks::bank_size(i+4));
        palettes[i]=bus->bank_generation(i+4);
    }
}

inline uint16_t rd16(uint32_t a) { return *static_cast<volatile uint16_t *>(host_ptr(a)); }
inline uint32_t rd32(uint32_t a) { return *static_cast<volatile uint32_t *>(host_ptr(a)); }

// DS palettes are BGR555.
inline uint32_t bgr555(uint16_t c) {
    const uint32_t r = (c >> 0) & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
    return 0xFF000000u | ((r << 3 | r >> 3) << 16) | ((g << 3 | g >> 3) << 8)
           | (b << 3 | b >> 3);
}

// Text-mode screen sizes, in tiles.
void text_size(unsigned sz, int &tw, int &th) {
    tw = (sz & 1) ? 64 : 32;
    th = (sz & 2) ? 64 : 32;
}

struct BgConfig {
    bool enabled;
    int priority;
    uint32_t screen;      // tilemap address
    uint32_t chars;       // tile data address
    bool bpp8;
    int tiles_w, tiles_h;
    int hofs, vofs;
    bool extended_tiled, wrap, ext_palette;
    bool extended_bitmap;
    uint16_t control;
    uint32_t bitmap_base;
    unsigned bitmap_mask;
    const uint16_t* bitmap_palette;
    int palette_slot;
    const uint32_t* colors;
    int32_t ref_x, ref_y;
    int16_t pa, pb, pc, pd;
    // Local to this scanout: neighbouring pixels usually share a tile row.
    // Memory/palette state cannot change inside synchronous scanout_bg.
    bool row_valid;
    int row_y, row_tx;
    uint32_t row_colors[8];
};

BgConfig read_bg(const EngineMap &em, int bg, uint32_t dispcnt) {
    BgConfig c{};
    c.enabled = (dispcnt >> (8 + bg)) & 1;
    if(em.has_base_offset && bg==0 && (dispcnt&8))c.enabled=false;
    if (!c.enabled) return c;

    const uint16_t cnt = rd16(em.reg_base + 0x08 + bg * 2);
    c.priority = cnt & 3;
    const unsigned mode = dispcnt & 7;
    c.extended_tiled = (bg == 3 && mode >= 3 && mode <= 5) || (bg == 2 && mode == 5);
    c.extended_bitmap=c.extended_tiled && (cnt&0x80);
    c.control=cnt;c.bitmap_base=em.vram_base;
    c.bitmap_mask=em.has_base_offset?0x7ffff:0x1ffff;
    c.bitmap_palette=static_cast<const uint16_t*>(host_ptr(em.pltt_base));
    c.ext_palette = (dispcnt & (1u << 30)) != 0;
    c.palette_slot = bg < 2 && (cnt & 0x2000) ? bg + 2 : bg;
    c.bpp8 = (cnt >> 7) & 1;

    const uint32_t char_off = em.has_base_offset ? ((dispcnt >> 24) & 7) << 16 : 0;
    const uint32_t scr_off = em.has_base_offset ? ((dispcnt >> 27) & 7) << 16 : 0;

    c.screen = em.vram_base + scr_off + (((cnt >> 8) & 0x1F) << 11);
    c.chars = em.vram_base + char_off + (((cnt >> 2) & 15) << 14);

    if (c.extended_tiled) {
        c.tiles_w = c.tiles_h = 16 << (cnt >> 14);
        if(c.extended_bitmap){
            const unsigned size=cnt>>14;
            c.tiles_w=size<2?16<<size:64;
            c.tiles_h=size==0?16:size==3?64:32;
            c.screen=em.vram_base+((cnt&0x1f00)<<6);
            c.ext_palette=false;
        }
        c.bpp8 = true;
        c.wrap = (cnt & 0x2000) != 0;
        const uint32_t affine = em.reg_base + 0x20 + (bg - 2) * 0x10;
        c.pa = static_cast<int16_t>(rd16(affine));
        c.pb = static_cast<int16_t>(rd16(affine + 2));
        c.pc = static_cast<int16_t>(rd16(affine + 4));
        c.pd = static_cast<int16_t>(rd16(affine + 6));
        // BG reference coordinates are signed 28-bit values with 8 fractions.
        c.ref_x = static_cast<int32_t>(rd32(affine + 8) << 4) >> 4;
        c.ref_y = static_cast<int32_t>(rd32(affine + 12) << 4) >> 4;
    } else {
        text_size((cnt >> 14) & 3, c.tiles_w, c.tiles_h);
    }

    // BGxHOFS/BGxVOFS are write-only 9-bit scroll registers, 4 bytes per BG.
    c.hofs = rd16(em.reg_base + 0x10 + bg * 4) & 0x1FF;
    c.vofs = rd16(em.reg_base + 0x12 + bg * 4) & 0x1FF;
    return c;
}

// One text-mode screen entry: tile 0-9, hflip 10, vflip 11, palette 12-15.
inline uint32_t screen_entry_addr(const BgConfig &c, int tx, int ty) {
    if (c.extended_tiled) return c.screen + (ty * c.tiles_w + tx) * 2;
    // 512-wide/tall maps are stored as consecutive 32x32 blocks of 2KB.
    const int bx = tx >> 5, by = ty >> 5;
    const int blocks_w = c.tiles_w >> 5;
    const int block = by * blocks_w + bx;
    return c.screen + block * 0x800 + (((ty & 31) * 32 + (tx & 31)) * 2);
}

// Extended palettes remain in the LCDC bank backing where the SDK upload
// functions write them. VRAMCNT determines which bank supplies each 8K slot.
uint16_t bg_ext_color(const EngineMap &em, unsigned offset) {
    auto rd8 = [](uint32_t a) { return *static_cast<volatile uint8_t *>(host_ptr(a)); };
    if (!em.has_base_offset)
        return (rd8(0x04000248) & 0x83) == 0x82 ? rd16(0x06898000 + offset) : 0;
    uint16_t color = 0;
    if ((rd8(0x04000244) & 0x87) == 0x84) color |= rd16(0x06880000 + offset);
    for (unsigned bank = 0; bank < 2; ++bank) {
        const unsigned cnt = rd8(0x04000245 + bank);
        if ((cnt & 0x87) == 0x84 && (offset >> 14) == ((cnt >> 3) & 1))
            color |= rd16(0x06890000 + bank * 0x4000 + (offset & 0x3fff));
    }
    return color;
}

// Returns false where the background is transparent at this pixel.
bool sample_bg(BgConfig &c, int x, int y, uint32_t &out) {
    if(c.extended_bitmap){
        const auto p=sample_extended_bitmap({static_cast<const uint8_t*>(host_ptr(c.bitmap_base)),c.bitmap_mask},
            c.bitmap_palette,c.control,c.ref_x+c.pa*x+c.pb*y,c.ref_y+c.pc*x+c.pd*y);
        if(!p)return false;out=bgr555(p.color);return true;
    }
    int px, py;
    if (c.extended_tiled) {
        px = (c.ref_x + c.pa * x + c.pb * y) >> 8;
        py = (c.ref_y + c.pc * x + c.pd * y) >> 8;
        if (!c.wrap && (px < 0 || py < 0 || px >= c.tiles_w * 8 || py >= c.tiles_h * 8))
            return false;
        px &= c.tiles_w * 8 - 1;
        py &= c.tiles_h * 8 - 1;
    } else {
        px = (x + c.hofs) & (c.tiles_w * 8 - 1);
        py = (y + c.vofs) & (c.tiles_h * 8 - 1);
    }

    if(!c.row_valid || c.row_y!=py || c.row_tx!=(px>>3)){
        c.row_valid=true;c.row_y=py;c.row_tx=px>>3;
        const uint16_t se=rd16(screen_entry_addr(c,px>>3,py>>3));
        const unsigned tile=se&0x3ff;
        const unsigned fy=(se&0x800)?7-(py&7):(py&7);
        const auto* row=static_cast<volatile uint8_t*>(host_ptr(
            c.chars+tile*(c.bpp8?64:32)+fy*(c.bpp8?8:4)));
        const unsigned palette_base=c.bpp8?(c.ext_palette?(se>>12)*256:0):(se>>12)*16;
        for(unsigned x=0;x<8;++x){
            const unsigned fx=(se&0x400)?7-x:x;
            const unsigned value=row[c.bpp8?fx:fx>>1];
            const unsigned index=c.bpp8?value:(value>>((fx&1)*4))&15;
            c.row_colors[x]=index?c.colors[palette_base+index]:0;
        }
    }
    out=c.row_colors[px&7];
    return out!=0;
}

}  // namespace

static bool build_window_masks(Engine eng,uint8_t* masks,bool capture_source=false);
// minigames-30fps-min: binary window cut (ppu.h PpuWindowCut). The scope flag
// is set only while candidate views are prepared for the GPU composition.
static bool g_window_cut_scope=false;
static bool windows_binary(Engine eng,uint32_t* empty_color=nullptr);
struct WindowCutScope {bool saved;WindowCutScope():saved(g_window_cut_scope){g_window_cut_scope=true;}~WindowCutScope(){g_window_cut_scope=saved;}};
static uint64_t g_window_cut_issued[2]{}; // signature of the cut last handed out per engine
// DISPCNT as the plane caches key it. Inside the cut scope, a binary-window
// (or window-free) DISPCNT is keyed without its window enables (bits 13-15)
// through a shadow word in unused I/O space (reg+0xF08), so a scene wipe
// starting or ending keeps the BG/OBJ caches (minigames-30fps-min).
static constexpr uint32_t DISPCNT_KEY_SHADOW=0xF08u;
static uint32_t dispcnt_key_address(Engine eng);
static uint32_t dispcnt_key_value(Engine eng);

// Whole-map raster of one text BG, keyed by an exact snapshot of its map,
// character block and resolved colours (never by its scroll registers). A
// text-BG pixel depends only on (map x, map y), so a scrolled line is a copy
// of the cached map row: identical to the tile walk in scanout_bg's fill_line.
struct TextLayerRaster {
    HostVector<uint32_t> px;
    HostVector<uint8_t> map,chars;
    HostVector<uint32_t> colors;
    uint32_t screen=0,char_base=0;int tiles_w=0,tiles_h=0;
    bool bpp8=false,ext_palette=false,valid=false,raster_valid=false;
    uint64_t generation=0; // bumped on every raster write (GPU snapshot key)
};
static uint64_t layer_raster_full,layer_raster_tiles,layer_raster_hits,layer_raster_direct;
static void raster_text_tile_reference(const BgConfig& c,TextLayerRaster& r,int tx,int ty){
    ++r.generation;
    const int width=c.tiles_w*8;
    const uint16_t se=rd16(screen_entry_addr(c,tx,ty));
    const unsigned tile=se&0x3ff;
    const uint32_t* palette=c.colors+(c.bpp8?(c.ext_palette?(se>>12)*256:0):(se>>12)*16);
    for(unsigned fy=0;fy<8;++fy){
        const unsigned row_y=(se&0x800)?7-fy:fy;
        const auto* row=static_cast<const uint8_t*>(host_ptr(c.chars+tile*(c.bpp8?64:32)+row_y*(c.bpp8?8:4)));
        uint32_t texels[8];
        if(c.bpp8){
            for(unsigned k=0;k<8;++k){const unsigned index=row[k];texels[k]=index?palette[index]:0;}
        }else{
            for(unsigned k=0;k<4;++k){
                const unsigned pair=row[k],low=pair&15,high=pair>>4;
                texels[2*k]=low?palette[low]:0;texels[2*k+1]=high?palette[high]:0;
            }
        }
        if(se&0x400)std::reverse(texels,texels+8);
        std::memcpy(&r.px[size_t(ty*8+fy)*width+tx*8],texels,sizeof texels);
    }
}
// castle-min30: same texels as raster_text_tile_reference (pause menus,
// dialogs, room arrivals and bottom-screen maps re-raster whole layers).
// Azahar guest time follows the instruction count: rows are emitted fully
// unrolled (constant shifts fold into the table loads), one host pointer per
// tile (a tile's rows are contiguous in the flat VRAM region, as the
// snapshots of update_text_layer already assume), zero-transparent colour
// tables instead of per-texel tests. PPU_REFERENCE_TILE_RASTER=1 restores
// the reference loops for A/B checks.
static bool tile_raster_reference(){static const bool value=std::getenv("PPU_REFERENCE_TILE_RASTER")!=nullptr;return value;}
template<bool HFLIP> static inline void emit_row4(uint32_t* out,uint32_t w,const uint32_t* col){
    const uint32_t a=col[w&15],b=col[(w>>4)&15],c=col[(w>>8)&15],d=col[(w>>12)&15],
                   e=col[(w>>16)&15],f=col[(w>>20)&15],g=col[(w>>24)&15],h=col[w>>28];
    if(HFLIP){out[0]=h;out[1]=g;out[2]=f;out[3]=e;out[4]=d;out[5]=c;out[6]=b;out[7]=a;}
    else{out[0]=a;out[1]=b;out[2]=c;out[3]=d;out[4]=e;out[5]=f;out[6]=g;out[7]=h;}
}
// pal[0] must be 0 (transparent).
template<bool HFLIP> static inline void emit_row8(uint32_t* out,uint32_t w0,uint32_t w1,const uint32_t* pal){
    const uint32_t a=pal[w0&255],b=pal[(w0>>8)&255],c=pal[(w0>>16)&255],d=pal[w0>>24],
                   e=pal[w1&255],f=pal[(w1>>8)&255],g=pal[(w1>>16)&255],h=pal[w1>>24];
    if(HFLIP){out[0]=h;out[1]=g;out[2]=f;out[3]=e;out[4]=d;out[5]=c;out[6]=b;out[7]=a;}
    else{out[0]=a;out[1]=b;out[2]=c;out[3]=d;out[4]=e;out[5]=f;out[6]=g;out[7]=h;}
}
template<bool BPP8,bool HFLIP> static void emit_tile_rows(uint32_t* out,int width,const uint8_t* base,int step,const uint32_t* table){
    // step: +row bytes, or -row bytes with base at the last row (vertical flip).
    for(unsigned fy=0;fy<8;++fy,out+=width,base+=step){
        if(BPP8){uint32_t w0,w1;std::memcpy(&w0,base,4);std::memcpy(&w1,base+4,4);emit_row8<HFLIP>(out,w0,w1,table);}
        else{uint32_t w;std::memcpy(&w,base,4);emit_row4<HFLIP>(out,w,table);}
    }
}
static inline void emit_tile(uint32_t* out,int width,const uint8_t* base,bool bpp8,bool hflip,bool vflip,const uint32_t* table){
    const int row=bpp8?8:4;const int step=vflip?-row:row;if(vflip)base+=7*row;
    if(bpp8){if(hflip)emit_tile_rows<true,true>(out,width,base,step,table);else emit_tile_rows<true,false>(out,width,base,step,table);}
    else{if(hflip)emit_tile_rows<false,true>(out,width,base,step,table);else emit_tile_rows<false,false>(out,width,base,step,table);}
}
static void raster_text_tile(const BgConfig& c,TextLayerRaster& r,int tx,int ty){
    if(tile_raster_reference()){raster_text_tile_reference(c,r,tx,ty);return;}
    ++r.generation;
    const int width=c.tiles_w*8;
    const uint16_t se=rd16(screen_entry_addr(c,tx,ty));
    const unsigned tile=se&0x3ff;
    const auto* base=static_cast<const uint8_t*>(host_ptr(c.chars+tile*(c.bpp8?64:32)));
    uint32_t* out=&r.px[size_t(ty*8)*width+tx*8];
    if(c.bpp8){
        const uint32_t* palette=c.colors+(c.ext_palette?(se>>12)*256:0);
        const bool hflip=se&0x400,vflip=se&0x800;
        for(unsigned fy=0;fy<8;++fy,out+=width){
            const uint8_t* row=base+(vflip?7-fy:fy)*8;
            if(hflip)for(unsigned k=0;k<8;++k){const unsigned i=row[7-k];const uint32_t v=palette[i];out[k]=i?v:0u;}
            else for(unsigned k=0;k<8;++k){const unsigned i=row[k];const uint32_t v=palette[i];out[k]=i?v:0u;}
        }
        return;
    }
    const uint32_t* p=c.colors+(se>>12)*16;
    const uint32_t color[16]={0u,p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]};
    emit_tile(out,width,base,false,se&0x400,se&0x800,color);
}
// Whole-map raster (new layer layouts, character/palette reloads): same
// texels as raster_text_tile for every map entry.
static void raster_text_layer(const BgConfig& c,TextLayerRaster& r){
    if(tile_raster_reference()){
        for(int ty=0;ty<c.tiles_h;++ty)for(int tx=0;tx<c.tiles_w;++tx)raster_text_tile_reference(c,r,tx,ty);
        return;
    }
    ++r.generation;
    const int width=c.tiles_w*8;
    const auto* chars=static_cast<const uint8_t*>(host_ptr(c.chars));
    const auto* map=static_cast<const uint8_t*>(host_ptr(c.screen));
    // Zero-transparent tables: 16 banks of 16 colours (4bpp) or lazily built
    // 256-colour banks (8bpp; one bank unless extended palettes are used).
    // Two banks: the engine-B affine attempt may run on the presentation
    // core while engine A prepares (castle-min30 ppu_prepare_background_affine).
    static uint32_t banks[2][16][256];static std::atomic_flag busy[2]={ATOMIC_FLAG_INIT,ATOMIC_FLAG_INIT};
    unsigned bank_index=0;while(busy[bank_index].test_and_set(std::memory_order_acquire))bank_index^=1;
    uint32_t (&tables)[16][256]=banks[bank_index];
    unsigned built=0;
    if(!c.bpp8)for(unsigned bank=0;bank<16;++bank){tables[bank][0]=0;for(unsigned i=1;i<16;++i)tables[bank][i]=c.colors[bank*16+i];}
    const unsigned tile_bytes=c.bpp8?64:32;
    for(int ty=0;ty<c.tiles_h;++ty){
        uint32_t* out=&r.px[size_t(ty*8)*width];
        for(int tx=0;tx<c.tiles_w;++tx,out+=8){
            uint16_t se;std::memcpy(&se,map+(screen_entry_addr(c,tx,ty)-c.screen),2);
            const unsigned bank=c.bpp8?(c.ext_palette?se>>12:0u):se>>12;
            if(c.bpp8 && !((built>>bank)&1)){
                const uint32_t* source=c.colors+(c.ext_palette?bank*256:0);
                tables[bank][0]=0;for(unsigned i=1;i<256;++i)tables[bank][i]=source[i];
                built|=1u<<bank;
            }
            emit_tile(out,width,chars+(se&0x3ff)*tile_bytes,c.bpp8,se&0x400,se&0x800,tables[bank]);
        }
    }
    busy[bank_index].clear(std::memory_order_release);
}
// Brings r up to date with c; returns false when this scanout must use the
// ordinary tile walk. Character/colour changes only refresh the snapshot: the
// map is rasterized once those inputs were stable across two scanouts, so a
// layer whose tiles animate every frame never pays for a whole-map raster.
static bool update_text_layer(const BgConfig& c,TextLayerRaster& r,bool raster_on_colour_change=false){
    // Extended (affine) tiled BGs share the text map raster: their screen
    // entries/tiles are sampled identically (sample_bg); only the per-pixel
    // map coordinate differs (castle-30fps: rotating bottom-screen maps).
    static const bool affine_cache=std::getenv("PPU_NO_AFFINE_LAYER_CACHE")==nullptr;
    if(c.extended_bitmap || !c.enabled || (c.extended_tiled && (!affine_cache || c.tiles_w*c.tiles_h>64*64)))return false;
    const size_t map_bytes=size_t(c.tiles_w)*c.tiles_h*2,char_bytes=c.bpp8?65536:32768;
    const size_t color_count=c.bpp8 && c.ext_palette?4096:256;
    const auto* map=static_cast<const uint8_t*>(host_ptr(c.screen));
    const auto* chars=static_cast<const uint8_t*>(host_ptr(c.chars));
    const bool layout=r.valid && r.screen==c.screen && r.char_base==c.chars && r.tiles_w==c.tiles_w &&
        r.tiles_h==c.tiles_h && r.bpp8==c.bpp8 && r.ext_palette==c.ext_palette;
    const bool colors_same=layout && !std::memcmp(r.colors.data(),c.colors,color_count*4);
    const bool chars_same=colors_same && !std::memcmp(r.chars.data(),chars,char_bytes);
    static const bool tile_update=std::getenv("PPU_NO_TILE_INCREMENTAL")==nullptr;
    if(tile_update && colors_same && !chars_same && r.raster_valid){
        // castle-30fps: only some character tiles changed (touch-button
        // highlights, animated icons). Re-raster exactly the map entries
        // that use a changed tile or whose entry changed; the rest of the
        // whole-map raster is still exact.
        const unsigned tile_bytes=c.bpp8?64:32;
        uint8_t changed[1024]; // local: see ppu_prepare_background_affine
        for(unsigned tile=0;tile<1024;++tile)
            changed[tile]=std::memcmp(r.chars.data()+tile*tile_bytes,chars+tile*tile_bytes,tile_bytes)!=0;
        const auto* old_map=reinterpret_cast<const uint16_t*>(r.map.data());
        for(int ty=0;ty<c.tiles_h;++ty)for(int tx=0;tx<c.tiles_w;++tx){
            const uint32_t at=screen_entry_addr(c,tx,ty)-c.screen;
            const uint16_t entry=*reinterpret_cast<const uint16_t*>(map+at);
            if(changed[entry&0x3ff] || old_map[at/2]!=entry){raster_text_tile(c,r,tx,ty);++layer_raster_tiles;}
        }
        std::memcpy(r.chars.data(),chars,char_bytes);std::memcpy(r.map.data(),map,map_bytes);
        return true;
    }
    const bool tiles_same=chars_same;
    if(tiles_same && r.raster_valid){
        if(!std::memcmp(r.map.data(),map,map_bytes)){++layer_raster_hits;return true;}
        // Only screen entries changed: re-raster exactly those tiles.
        const auto* old=reinterpret_cast<const uint16_t*>(r.map.data());
        for(int ty=0;ty<c.tiles_h;++ty)for(int tx=0;tx<c.tiles_w;++tx){
            const uint32_t at=screen_entry_addr(c,tx,ty)-c.screen;
            if(old[at/2]!=*reinterpret_cast<const uint16_t*>(map+at)){raster_text_tile(c,r,tx,ty);++layer_raster_tiles;}
        }
        std::memcpy(r.map.data(),map,map_bytes);
        return true;
    }
    if(tiles_same){
        try{r.px.resize(size_t(c.tiles_w)*c.tiles_h*64);}catch(const std::bad_alloc&){r=TextLayerRaster{};return false;}
        raster_text_layer(c,r);
        std::memcpy(r.map.data(),map,map_bytes);
        r.raster_valid=true;++layer_raster_full;
        return true;
    }
    try{r.map.resize(map_bytes);r.chars.resize(char_bytes);r.colors.resize(color_count);}
    catch(const std::bad_alloc&){r=TextLayerRaster{};return false;}
    std::memcpy(r.map.data(),map,map_bytes);
    std::memcpy(r.chars.data(),chars,char_bytes);
    std::memcpy(r.colors.data(),c.colors,color_count*4);
    r.screen=c.screen;r.char_base=c.chars;r.tiles_w=c.tiles_w;r.tiles_h=c.tiles_h;
    r.bpp8=c.bpp8;r.ext_palette=c.ext_palette;r.valid=true;r.raster_valid=false;
    // castle-30fps: tile changes are now incremental, so a newly seen layer
    // is rasterized at once instead of after a second identical scanout
    // (which cost a tile walk plus a delayed whole-map raster). Colour
    // changes keep the deferred behaviour (palette animation).
    static const bool eager=std::getenv("PPU_NO_EAGER_LAYER_RASTER")==nullptr;
    // castle-min30: GPU-affine overlays (raster_on_colour_change) re-raster on
    // palette animation too: their alternative is the whole CPU planes path.
    if(eager && (!layout || raster_on_colour_change)){
        try{r.px.resize(size_t(c.tiles_w)*c.tiles_h*64);}catch(const std::bad_alloc&){r=TextLayerRaster{};return false;}
        raster_text_layer(c,r);
        r.raster_valid=true;++layer_raster_full;
        return true;
    }
    ++layer_raster_direct;return false;
}

// castle-30fps: window-free BG-only row merge, N front-to-back sources fully
// unrolled (same selection as the packed/fast_multi loops in scanout_bg:
// first and second opaque source, backdrop otherwise). Kept out of line so
// the compiler can hold every pointer in a register.
template<int N> static void __attribute__((noinline)) merge_bg_row(const uint32_t* const* src,const uint8_t* sl,const uint8_t* sp,
    uint32_t backdrop,uint32_t* out,uint32_t* behind,uint8_t* ol,uint8_t* op,uint8_t* ul,uint8_t* up){
    const uint32_t* s0=src[0];const uint32_t* s1=N>1?src[1]:nullptr;const uint32_t* s2=N>2?src[2]:nullptr;const uint32_t* s3=N>3?src[3]:nullptr;
    const uint8_t l0=sl[0],p0=sp[0],l1=N>1?sl[1]:5,p1=N>1?sp[1]:4,l2=N>2?sl[2]:5,p2=N>2?sp[2]:4,l3=N>3?sl[3]:5,p3=N>3?sp[3]:4;
    for(int x=0;x<SCREEN_W;++x){
        if(!(x&3)){
            // castle-min30: four pixels with every source transparent.
            uint32_t any=s0[x]|s0[x+1]|s0[x+2]|s0[x+3];
            if(N>1)any|=s1[x]|s1[x+1]|s1[x+2]|s1[x+3];
            if(N>2)any|=s2[x]|s2[x+1]|s2[x+2]|s2[x+3];
            if(N>3)any|=s3[x]|s3[x+1]|s3[x+2]|s3[x+3];
            if(!(any>>24)){
                for(int q=0;q<4;++q){out[x+q]=backdrop;behind[x+q]=backdrop;ol[x+q]=5;op[x+q]=4;ul[x+q]=5;up[x+q]=4;}
                x+=3;continue;
            }
        }
        uint32_t v[4]={s0[x],N>1?s1[x]:0u,N>2?s2[x]:0u,N>3?s3[x]:0u};
        const uint8_t l[4]={l0,l1,l2,l3},pr[4]={p0,p1,p2,p3};
        uint32_t c=backdrop,u=backdrop;uint8_t cl=5,cp=4,bl=5,bp=4;
        int k=0;
        for(;k<N;++k)if(v[k]>>24){c=v[k];cl=l[k];cp=pr[k];++k;break;}
        for(;k<N;++k)if(v[k]>>24){u=v[k];bl=l[k];bp=pr[k];break;}
        out[x]=c;behind[x]=u;ol[x]=cl;op[x]=cp;ul[x]=bl;up[x]=bp;
    }
}
// Same selection with window layer masks (general loop of scanout_bg without
// geometry): a source only counts where its window bit is set.
template<int N> static void __attribute__((noinline)) merge_bg_row_windowed(const uint32_t* const* src,const uint8_t* sl,const uint8_t* sp,
    const uint8_t* wb,const uint8_t* window,uint32_t backdrop,uint32_t* out,uint32_t* behind,uint8_t* ol,uint8_t* op,uint8_t* ul,uint8_t* up){
    for(int x=0;x<SCREEN_W;++x){
        const unsigned w=window[x];
        uint32_t c=backdrop,u=backdrop;uint8_t cl=5,cp=4,bl=5,bp=4;
        int k=0;
        for(;k<N;++k){const uint32_t v=src[k][x];if((w&wb[k]) && (v>>24)){c=v;cl=sl[k];cp=sp[k];++k;break;}}
        for(;k<N;++k){const uint32_t v=src[k][x];if((w&wb[k]) && (v>>24)){u=v;bl=sl[k];bp=sp[k];break;}}
        out[x]=c;if(behind)behind[x]=u;if(ol)ol[x]=cl;if(op)op[x]=cp;if(ul)ul[x]=bl;if(up)up[x]=bp;
    }
}
static void scanout_bg(Engine eng, Framebuffer &fb, const Framebuffer* geometry,
                       uint8_t* layers, uint8_t* priorities, Framebuffer* behind = nullptr,
                       uint8_t* behind_layers = nullptr, uint8_t* behind_priorities = nullptr,
                       const uint8_t* windows = nullptr,bool capture_source=false,
                       int y_begin = 0,int y_end = SCREEN_H,TextLayerRaster* layer_cache = nullptr,
                       const uint8_t* row_mask = nullptr) {
    const EngineMap &em = kEngines[eng];
    const uint32_t dispcnt = rd32(em.reg_base);

    // Display mode 0 is "display off" -- the DS shows white, not black.
    const unsigned disp_mode = (dispcnt >> 16) & 3;
    const bool forced_blank = (dispcnt >> 7) & 1;
    if ((!capture_source && disp_mode == 0) || forced_blank) {
        for (int y = y_begin; y < y_end; ++y)
            for (int x = 0; x < SCREEN_W; ++x) fb.px[y][x] = 0xFFFFFFFFu;
        return;
    }

    BgConfig bgs[4];
    for (int i = 0; i < 4; ++i) bgs[i] = read_bg(em, i, dispcnt);

    // Registers and palette memory do not change during a synchronous scanout.
    // Resolve bank mappings and convert colours once, not for each sampled texel.
    static uint32_t palette[256],extended[4][4096];
    bool resolved[4]{};
    for(unsigned i=0;i<256;++i)palette[i]=bgr555(rd16(em.pltt_base+i*2));
    for(auto& bg:bgs) {
        if(!bg.enabled)continue;
        bg.colors=palette;
        if(bg.bpp8 && bg.ext_palette){
            const unsigned slot=bg.palette_slot;
            if(!resolved[slot]){
                static const bool reference_ext=std::getenv("PPU_REFERENCE_EXT_PALETTE")!=nullptr;
                if(reference_ext){
                    for(unsigned i=0;i<4096;++i)extended[slot][i]=bgr555(bg_ext_color(em,slot*8192+i*2));
                }else{
                    // bg_ext_color's bank selection depends only on the slot:
                    // resolve the mapped banks once, then OR their entries.
                    const uint16_t* sources[3];unsigned n=0;const unsigned offset=slot*8192;
                    auto rd8=[](uint32_t a){return *static_cast<volatile uint8_t*>(host_ptr(a));};
                    if(!em.has_base_offset){
                        if((rd8(0x04000248)&0x83)==0x82)sources[n++]=static_cast<const uint16_t*>(host_ptr(0x06898000+offset));
                    }else{
                        if((rd8(0x04000244)&0x87)==0x84)sources[n++]=static_cast<const uint16_t*>(host_ptr(0x06880000+offset));
                        for(unsigned bank=0;bank<2;++bank){
                            const unsigned cnt=rd8(0x04000245+bank);
                            if((cnt&0x87)==0x84 && (offset>>14)==((cnt>>3)&1))
                                sources[n++]=static_cast<const uint16_t*>(host_ptr(0x06890000+bank*0x4000+(offset&0x3fff)));
                        }
                    }
                    for(unsigned i=0;i<4096;++i){uint16_t c=0;for(unsigned k=0;k<n;++k)c|=sources[k][i];extended[slot][i]=bgr555(c);}
                }
                resolved[slot]=true;
            }
            bg.colors=extended[slot];
        }
    }
    const uint32_t backdrop = palette[0];
    bool cached_layer[4]{};
    if(layer_cache)for(int bg=0;bg<4;++bg)if(bgs[bg].enabled)cached_layer[bg]=update_text_layer(bgs[bg],layer_cache[bg]);
    // State is fixed for this scanout. Precompute the exact old back-to-front
    // order instead of testing 16 priority/BG combinations at every pixel.
    int order[4], count=0;
    const int geometry_priority=geometry && (dispcnt&(1u<<8)) ? (rd16(em.reg_base+8)&3) : -1;
    for(int priority=3;priority>=0;--priority)for(int bg=3;bg>=0;--bg) {
        if(bg==0 && geometry_priority==priority)order[count++]=-1;
        else if(bgs[bg].enabled && bgs[bg].priority==priority)order[count++]=bg;
    }

    // One scanline of each enabled BG, 0 = transparent (colours are opaque
    // ARGB). Text BGs fetch a tile row once per 8 pixels; affine and bitmap
    // BGs keep the per-pixel sampler.
    static uint32_t lines[4][SCREEN_W];
    // castle-30fps: scale/translate-only affine BGs (pb=pc=0, the room maps)
    // fetch the same raster column for a given x on every row, and the same
    // raster row for every pixel of a screen row: precompute the columns and
    // keep the previous line when consecutive rows map to the same row.
    static const bool affine_columns=std::getenv("PPU_NO_AFFINE_COLUMNS")==nullptr;
    static int32_t affine_column[4][SCREEN_W];static int affine_row[4];bool affine_axis[4]{};
    for(int bg=0;bg<4;++bg){
        const auto& c=bgs[bg];
        if(!affine_columns || !c.enabled || !c.extended_tiled || !cached_layer[bg] || c.pb || c.pc)continue;
        const int width=c.tiles_w*8;int32_t ux=c.ref_x;
        for(int x=0;x<SCREEN_W;++x,ux+=c.pa){const int px=ux>>8;
            affine_column[bg][x]=c.wrap?(px&(width-1)):(unsigned(px)<unsigned(width)?px:-1);}
        affine_axis[bg]=true;affine_row[bg]=INT32_MIN;
    }
    auto fill_line=[&](int bg,int y){
        auto& c=bgs[bg];uint32_t* line=lines[bg];
        if(affine_axis[bg]){
            const int width=c.tiles_w*8,height=c.tiles_h*8;
            int py=(c.ref_y+c.pd*y)>>8;
            if(c.wrap)py&=height-1;
            else if(unsigned(py)>=unsigned(height))py=-1;
            if(py==affine_row[bg])return; // lines[bg] already holds this row
            affine_row[bg]=py;
            if(py<0){std::fill_n(line,SCREEN_W,0u);return;}
            const uint32_t* row=layer_cache[bg].px.data()+size_t(py)*width;const int32_t* column=affine_column[bg];
            for(int x=0;x<SCREEN_W;++x){const int32_t k=column[x];line[x]=k>=0?row[k]:0u;}
            return;
        }
        if(c.extended_tiled && cached_layer[bg]){
            // Same texel as sample_bg: map coordinate (ref + P*(x,y))>>8,
            // wrapped or transparent outside, read from the cached map raster.
            const uint32_t* source=layer_cache[bg].px.data();
            const int width=c.tiles_w*8,height=c.tiles_h*8;
            int32_t ux=c.ref_x+c.pb*y,uy=c.ref_y+c.pd*y;
            if(c.wrap){
                for(int x=0;x<SCREEN_W;++x,ux+=c.pa,uy+=c.pc)
                    line[x]=source[size_t((uy>>8)&(height-1))*width+((ux>>8)&(width-1))];
            }else{
                for(int x=0;x<SCREEN_W;++x,ux+=c.pa,uy+=c.pc){
                    const int px=ux>>8,py=uy>>8;
                    line[x]=unsigned(px)<unsigned(width)&&unsigned(py)<unsigned(height)?source[size_t(py)*width+px]:0;
                }
            }
            return;
        }
        if(c.extended_tiled){
            for(int x=0;x<SCREEN_W;++x){uint32_t pixel;line[x]=sample_bg(c,x,y,pixel)?pixel:0;}
            return;
        }
        const int width=c.tiles_w*8,py=(y+c.vofs)&(c.tiles_h*8-1);
        int px=c.hofs&(width-1);
        if(cached_layer[bg]){
            // Copy the map row, wrapping horizontally at the map width.
            const uint32_t* source=&layer_cache[bg].px[size_t(py)*width];
            const int first=std::min(width-px,SCREEN_W);
            std::memcpy(line,source+px,first*4);
            for(int x=first;x<SCREEN_W;x+=width)std::memcpy(line+x,source,std::min(width,SCREEN_W-x)*4);
            return;
        }
        const unsigned fy=py&7;
        for(int x=0;x<SCREEN_W;){
            const int tx=px>>3;
            const uint16_t se=rd16(screen_entry_addr(c,tx,py>>3));
            const unsigned tile=se&0x3ff;
            const unsigned row_y=(se&0x800)?7-fy:fy;
            // VRAM is ordinary memory during a synchronous scanout.
            const auto* row=static_cast<const uint8_t*>(host_ptr(
                c.chars+tile*(c.bpp8?64:32)+row_y*(c.bpp8?8:4)));
            const uint32_t* palette=c.colors+(c.bpp8?(c.ext_palette?(se>>12)*256:0):(se>>12)*16);
            uint32_t texels[8];
            if(c.bpp8){
                for(unsigned k=0;k<8;++k){const unsigned index=row[k];texels[k]=index?palette[index]:0;}
            }else{
                for(unsigned k=0;k<4;++k){
                    const unsigned pair=row[k],low=pair&15,high=pair>>4;
                    texels[2*k]=low?palette[low]:0;texels[2*k+1]=high?palette[high]:0;
                }
            }
            if(se&0x400)std::reverse(texels,texels+8);
            static const bool reference=std::getenv("PPU_REFERENCE_FILL")!=nullptr;
            if(reference){
                const auto* vrow=static_cast<volatile uint8_t*>(host_ptr(c.chars+tile*(c.bpp8?64:32)+row_y*(c.bpp8?8:4)));
                const unsigned palette_base=c.bpp8?(c.ext_palette?(se>>12)*256:0):(se>>12)*16;
                for(unsigned j=0;j<8;++j){const unsigned fx=(se&0x400)?7-j:j;const unsigned value=vrow[c.bpp8?fx:fx>>1];
                    const unsigned index=c.bpp8?value:(value>>((fx&1)*4))&15;texels[j]=index?c.colors[palette_base+index]:0;}
            }
            unsigned k=px&7;
            const unsigned n=std::min<unsigned>(8-k,unsigned(SCREEN_W-x));
            std::memcpy(line+x,texels+k,n*4);x+=n;
            px=(tx*8+8)&(width-1);
        }
    };
    // Front-to-back sources. BG line pixels and geometry share one test:
    // BG colours are opaque ARGB and transparent texels are zero.
    unsigned source_layer[4],source_priority[4],window_bit[4];
    for(int i=0;i<count;++i){
        const int bg=order[i];
        source_layer[i]=bg<0?0:bg;
        source_priority[i]=bg<0?geometry_priority:bgs[bg].priority;
        window_bit[i]=1u<<(bg<0?0:bg);
    }
    static const bool fast_compose_disabled=std::getenv("PPU_REFERENCE_FILL")!=nullptr;
    // castle-30fps: one cached affine BG and nothing else (rotating
    // bottom-screen maps). Same selection as the packed path below with an
    // empty back source, fused with the cached affine texel fetch.
    static const bool fused_affine=std::getenv("PPU_NO_FUSED_AFFINE")==nullptr;
    if(fused_affine && count==1 && order[0]>=0 && cached_layer[order[0]] && bgs[order[0]].extended_tiled &&
       !windows && behind && behind_layers && behind_priorities && layers && priorities && !fast_compose_disabled){
        const auto& c=bgs[order[0]];
        const uint32_t* raster=layer_cache[order[0]].px.data();
        const int width=c.tiles_w*8,height=c.tiles_h*8;
        const uint8_t front_layer=uint8_t(source_layer[0]),front_priority=uint8_t(source_priority[0]);
        static uint32_t tl[16],tp[16];static uint32_t key=~0u;
        const uint32_t k=front_layer|(front_priority<<8);
        if(k!=key){
            key=k;
            for(unsigned m=0;m<16;++m){
                uint32_t l=0,pr=0;
                for(unsigned q=0;q<4;++q){const bool ha=(m>>q)&1;l|=uint32_t(ha?front_layer:5)<<(q*8);pr|=uint32_t(ha?front_priority:4)<<(q*8);}
                tl[m]=l;tp[m]=pr;
            }
        }
        int previous_py=INT32_MIN,previous_y=-1;
        for(int y=y_begin;y<y_end;++y){
            if(row_mask && !row_mask[y])continue;
            uint32_t* out=fb.px[y];const unsigned row=y*SCREEN_W;
            uint8_t* ol=layers+row;uint8_t* op=priorities+row;
            std::fill_n(behind->px[y],SCREEN_W,backdrop);
            std::memset(behind_layers+row,5,SCREEN_W);std::memset(behind_priorities+row,4,SCREEN_W);
            if(affine_axis[order[0]]){
                // Scale/translate only: one raster row per screen row.
                int py=(c.ref_y+c.pd*y)>>8;
                if(c.wrap)py&=height-1;else if(unsigned(py)>=unsigned(height))py=-1;
                if(py==previous_py && previous_y==y-1){
                    std::memcpy(out,fb.px[y-1],SCREEN_W*4);std::memcpy(ol,ol-SCREEN_W,SCREEN_W);std::memcpy(op,op-SCREEN_W,SCREEN_W);
                    previous_y=y;continue;
                }
                previous_py=py;previous_y=y;
                const uint32_t* src=py>=0?raster+size_t(py)*width:nullptr;const int32_t* column=affine_column[order[0]];
                for(int x=0;x<SCREEN_W;x+=4){
                    unsigned m=0;
                    for(unsigned q=0;q<4;++q){
                        const int32_t k=column[x+q];const uint32_t a=src&&k>=0?src[k]:0u;
                        const unsigned ha=(a>>24)!=0;out[x+q]=ha?a:backdrop;m|=ha<<q;
                    }
                    std::memcpy(ol+x,&tl[m],4);std::memcpy(op+x,&tp[m],4);
                }
                continue;
            }
            // Locals: stores through out/ol/op must not force reloads of c.
            int32_t ux=c.ref_x+c.pb*y,uy=c.ref_y+c.pd*y;
            const int32_t pa=c.pa,pc=c.pc;const uint32_t back=backdrop;
            const unsigned wmask=unsigned(width-1),hmask=unsigned(height-1);
            const unsigned shift=__builtin_ctz(unsigned(width));
            uint32_t* const o=out;
            if(c.wrap){
                for(int x=0;x<SCREEN_W;x+=4){
                    unsigned m=0;
                    for(unsigned q=0;q<4;++q,ux+=pa,uy+=pc){
                        const uint32_t a=raster[((unsigned(uy>>8)&hmask)<<shift)|(unsigned(ux>>8)&wmask)];
                        const unsigned ha=(a>>24)!=0;o[x+q]=ha?a:back;m|=ha<<q;
                    }
                    std::memcpy(ol+x,&tl[m],4);std::memcpy(op+x,&tp[m],4);
                }
            }else{
                for(int x=0;x<SCREEN_W;x+=4){
                    unsigned m=0;
                    for(unsigned q=0;q<4;++q,ux+=pa,uy+=pc){
                        const unsigned px=unsigned(ux>>8),py=unsigned(uy>>8);
                        const uint32_t a=px<=wmask && py<=hmask?raster[(py<<shift)|px]:0;
                        const unsigned ha=(a>>24)!=0;o[x+q]=ha?a:back;m|=ha<<q;
                    }
                    std::memcpy(ol+x,&tl[m],4);std::memcpy(op+x,&tp[m],4);
                }
            }
        }
        return;
    }
    for (int y = y_begin; y < y_end; ++y) {
        if(row_mask && !row_mask[y])continue;
        const uint32_t* source[4];
        static const bool direct_rows=std::getenv("PPU_NO_DIRECT_LAYER_ROWS")==nullptr;
        for(int i=0;i<count;++i){
            if(order[i]>=0){
                const int bg=order[i];const auto& c=bgs[bg];
                if(direct_rows && cached_layer[bg] && !c.extended_tiled){
                    // Cached text layer: the visible row is a contiguous
                    // slice of the map raster unless it wraps horizontally.
                    const int width=c.tiles_w*8,py=(y+c.vofs)&(c.tiles_h*8-1),px=c.hofs&(width-1);
                    if(px+SCREEN_W<=width){source[i]=&layer_cache[bg].px[size_t(py)*width+px];continue;}
                }
                fill_line(bg,y);source[i]=lines[bg];
            }
            else source[i]=geometry->px[y];
        }
        const uint8_t* window_row=windows?windows+y*SCREEN_W:nullptr;
        uint32_t* out=fb.px[y];
        uint32_t* out_behind=behind?behind->px[y]:nullptr;
        const unsigned row=y*SCREEN_W;
        static const bool unrolled_merge=std::getenv("PPU_NO_UNROLLED_MERGE")==nullptr;
        if(unrolled_merge && count==0 && !fast_compose_disabled){
            // castle-30fps: no BG source at all (e.g. engine A with only the
            // 3D layer): every pixel is the backdrop, as in the general loop.
            std::fill_n(out,SCREEN_W,backdrop);if(out_behind)std::fill_n(out_behind,SCREEN_W,backdrop);
            if(layers)std::memset(layers+row,5,SCREEN_W);if(priorities)std::memset(priorities+row,4,SCREEN_W);
            if(behind_layers)std::memset(behind_layers+row,5,SCREEN_W);if(behind_priorities)std::memset(behind_priorities+row,4,SCREEN_W);
            continue;
        }
        if(unrolled_merge && window_row && count>=1 && count<=4 && !fast_compose_disabled && geometry_priority<0){
            const uint32_t* s[4];uint8_t sl[4],sp[4],wb[4];
            for(int k=0;k<count;++k){s[k]=source[count-1-k];sl[k]=uint8_t(source_layer[count-1-k]);sp[k]=uint8_t(source_priority[count-1-k]);wb[k]=uint8_t(window_bit[count-1-k]);}
            uint8_t* ol=layers?layers+row:nullptr;uint8_t* op=priorities?priorities+row:nullptr;
            uint8_t* ul=behind_layers?behind_layers+row:nullptr;uint8_t* up=behind_priorities?behind_priorities+row:nullptr;
            switch(count){
            case 1:merge_bg_row_windowed<1>(s,sl,sp,wb,window_row,backdrop,out,out_behind,ol,op,ul,up);break;
            case 2:merge_bg_row_windowed<2>(s,sl,sp,wb,window_row,backdrop,out,out_behind,ol,op,ul,up);break;
            case 3:merge_bg_row_windowed<3>(s,sl,sp,wb,window_row,backdrop,out,out_behind,ol,op,ul,up);break;
            default:merge_bg_row_windowed<4>(s,sl,sp,wb,window_row,backdrop,out,out_behind,ol,op,ul,up);break;
            }
            continue;
        }
        if(unrolled_merge && !window_row && out_behind && behind_layers && behind_priorities && layers && priorities &&
           count>=2 && count<=4 && !fast_compose_disabled && geometry_priority<0){
            const uint32_t* s[4];uint8_t sl[4],sp[4];
            for(int k=0;k<count;++k){s[k]=source[count-1-k];sl[k]=uint8_t(source_layer[count-1-k]);sp[k]=uint8_t(source_priority[count-1-k]);}
            uint8_t* ol=layers+row;uint8_t* op=priorities+row;uint8_t* ul=behind_layers+row;uint8_t* up=behind_priorities+row;
            if(count==2)merge_bg_row<2>(s,sl,sp,backdrop,out,out_behind,ol,op,ul,up);
            else if(count==3)merge_bg_row<3>(s,sl,sp,backdrop,out,out_behind,ol,op,ul,up);
            else merge_bg_row<4>(s,sl,sp,backdrop,out,out_behind,ol,op,ul,up);
            continue;
        }
        // Same selection as the general loop below, specialised for the
        // window-free background cache with one or two layers.
        if(!window_row && out_behind && behind_layers && behind_priorities && layers && priorities &&
           (count==1 || count==2) && !fast_compose_disabled){
            const uint32_t* front_source=source[count-1];
            const uint32_t* back_source=count==2?source[0]:nullptr;
            const uint8_t front_layer=uint8_t(source_layer[count-1]),front_priority=uint8_t(source_priority[count-1]);
            const uint8_t back_layer=count==2?uint8_t(source_layer[0]):5,back_priority=count==2?uint8_t(source_priority[0]):4;
            uint8_t* ol=layers+row;uint8_t* op=priorities+row;uint8_t* ul=behind_layers+row;uint8_t* up=behind_priorities+row;
            static const bool packed=std::getenv("PPU_REFERENCE_MERGE")==nullptr;
            if(packed){
                // Same per-pixel selection, metadata written four pixels per
                // word from tables indexed by the four (has_a,has_b) pairs.
                static uint32_t tl[256],tp[256],tul[256],tup[256];static uint32_t key=~0u;
                const uint32_t k=front_layer|(front_priority<<8)|(back_layer<<16)|(uint32_t(back_priority)<<24);
                if(k!=key){
                    key=k;
                    for(unsigned m=0;m<256;++m){
                        uint32_t l=0,pr=0,bl=0,bp=0;
                        for(unsigned q=0;q<4;++q){
                            const unsigned s=(m>>(q*2))&3;const bool ha=s&2,hb=s&1;
                            l|=uint32_t(ha?front_layer:hb?back_layer:5)<<(q*8);
                            pr|=uint32_t(ha?front_priority:hb?back_priority:4)<<(q*8);
                            bl|=uint32_t(ha&&hb?back_layer:5)<<(q*8);
                            bp|=uint32_t(ha&&hb?back_priority:4)<<(q*8);
                        }
                        tl[m]=l;tp[m]=pr;tul[m]=bl;tup[m]=bp;
                    }
                }
                for(int x=0;x<SCREEN_W;x+=4){
                    // castle-min30: four pixels where both layers are transparent
                    // (most of a menu/text layer): backdrop and the m=0 codes.
                    {const uint32_t* f4=front_source+x;uint32_t any=f4[0]|f4[1]|f4[2]|f4[3];
                     if(back_source){const uint32_t* b4=back_source+x;any|=b4[0]|b4[1]|b4[2]|b4[3];}
                     if(!(any>>24)){
                        out[x]=out[x+1]=out[x+2]=out[x+3]=backdrop;
                        out_behind[x]=out_behind[x+1]=out_behind[x+2]=out_behind[x+3]=backdrop;
                        std::memcpy(ol+x,&tl[0],4);std::memcpy(op+x,&tp[0],4);
                        std::memcpy(ul+x,&tul[0],4);std::memcpy(up+x,&tup[0],4);
                        continue;
                     }}
                    unsigned m=0;
                    for(unsigned q=0;q<4;++q){
                        const uint32_t a=front_source[x+q],b=back_source?back_source[x+q]:0;
                        const bool ha=(a>>24)!=0,hb=(b>>24)!=0;
                        out[x+q]=ha?a:hb?b:backdrop;
                        out_behind[x+q]=ha&&hb?b:backdrop;
                        m|=(unsigned(ha)*2+unsigned(hb))<<(q*2);
                    }
                    std::memcpy(ol+x,&tl[m],4);std::memcpy(op+x,&tp[m],4);
                    std::memcpy(ul+x,&tul[m],4);std::memcpy(up+x,&tup[m],4);
                }
                continue;
            }
            for(int x=0;x<SCREEN_W;++x){
                const uint32_t a=front_source[x],b=back_source?back_source[x]:0;
                const bool has_a=(a>>24)!=0,has_b=(b>>24)!=0;
                if(has_a){
                    out[x]=a;ol[x]=front_layer;op[x]=front_priority;
                    if(has_b){out_behind[x]=b;ul[x]=back_layer;up[x]=back_priority;}
                    else{out_behind[x]=backdrop;ul[x]=5;up[x]=4;}
                }else{
                    out_behind[x]=backdrop;ul[x]=5;up[x]=4;
                    if(has_b){out[x]=b;ol[x]=back_layer;op[x]=back_priority;}
                    else{out[x]=backdrop;ol[x]=5;op[x]=4;}
                }
            }
            continue;
        }
        static const bool fast_multi=std::getenv("PPU_REFERENCE_MERGE")==nullptr;
        if(fast_multi && !window_row && out_behind && behind_layers && behind_priorities && layers && priorities &&
           (count==3 || count==4) && !fast_compose_disabled){
            // Same selection as the general loop: first and second opaque
            // source front-to-back, backdrop otherwise.
            const uint32_t* s[4];uint8_t sl[4],sp[4];
            for(int k=0;k<count;++k){s[k]=source[count-1-k];sl[k]=uint8_t(source_layer[count-1-k]);sp[k]=uint8_t(source_priority[count-1-k]);}
            uint8_t* ol=layers+row;uint8_t* op=priorities+row;uint8_t* ul=behind_layers+row;uint8_t* up=behind_priorities+row;
            for(int x=0;x<SCREEN_W;++x){
                uint32_t c=backdrop,u=backdrop;uint8_t cl=5,cp=4,bl=5,bp=4;
                int k=0;
                for(;k<count;++k){const uint32_t v=s[k][x];if(v>>24){c=v;cl=sl[k];cp=sp[k];++k;break;}}
                for(;k<count;++k){const uint32_t v=s[k][x];if(v>>24){u=v;bl=sl[k];bp=sp[k];break;}}
                out[x]=c;ol[x]=cl;op[x]=cp;out_behind[x]=u;ul[x]=bl;up[x]=bp;
            }
            continue;
        }
        for (int x = 0; x < SCREEN_W; ++x) {
            uint32_t c = backdrop;
            unsigned layer=5,priority=4;
            uint32_t under = backdrop;
            unsigned under_layer = 5, under_priority = 4;
            // Only the nearest two visible planes can participate in DS
            // blending. Traverse front-to-back and never sample hidden BGs.
            bool found=false;
            for(int i=count-1;i>=0;--i) {
                if(window_row && !(window_row[x] & window_bit[i]))continue;
                const uint32_t pixel=source[i][x];
                if(!(pixel>>24))continue;
                if(!found){
                    c=pixel;layer=source_layer[i];priority=source_priority[i];
                    if(!behind)break;
                    found=true;
                }else{
                    under=pixel;under_layer=source_layer[i];under_priority=source_priority[i];
                    break;
                }
            }
            out[x] = c;
            if (out_behind) out_behind[x] = under;
            if (behind_layers) behind_layers[row+x] = under_layer;
            if (behind_priorities) behind_priorities[row+x] = under_priority;
            if(layers)layers[row+x]=layer;
            if(priorities)priorities[row+x]=priority;
        }
    }
}

void ppu_scanout(Engine eng, Framebuffer &fb) {
    sync_vram_views();
    static uint8_t windows[SCREEN_W*SCREEN_H];
    const bool enabled=build_window_masks(eng,windows);
    scanout_bg(eng,fb,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,enabled?windows:nullptr);
}

static double (*prepare_clock)()=nullptr;
static PpuPrepareProfile prepare_profiles[2]{};
// --- OBJ layer --------------------------------------------------------------
// GBATEK "LCD OBJ": 128 OAM entries per engine; tiles in the engine's OBJ
// VRAM region, palette at pltt+0x200. Sizes come from the shape/size table.
// Affine params sit in the filler word of entries group*4+0..3 (pa,pb,pc,pd).
static const int kObjSizes[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},    // square
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},    // wide
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}},    // tall
    };
struct NoObjBox {void operator()(int,int,int,int) const {}};
struct ObjClip {int x0=0,x1=SCREEN_W,y0=0,y1=SCREEN_H;};
// 256-colour OBJ extended palettes (DISPCNT bit 31): 16 palettes of 256
// colours in the first 8 KiB of the bank VRAMCNT maps there (engine A: F or G
// with MST 5, engine B: I with MST 3), read through the LCDC backing like the
// BG extended palettes above.
static uint16_t obj_ext_color(Engine eng,unsigned offset){
    auto rd8=[](uint32_t a){return *static_cast<volatile uint8_t*>(host_ptr(a));};
    if(eng==ENGINE_B)return (rd8(0x04000249)&0x87)==0x83?rd16(0x068a0000+offset):0;
    uint16_t color=0;
    for(unsigned bank=0;bank<2;++bank)
        if((rd8(0x04000245+bank)&0x87)==0x85)color|=rd16(0x06890000+bank*0x4000+offset);
    return color;
}
template<class Put,class Box=NoObjBox> static void scan_obj(Engine eng, Put put,bool window_only=false,bool capture_source=false,Box box=Box{},ObjClip clip=ObjClip{}) {
    const uint32_t oam_base = eng == ENGINE_A ? 0x07000000u : 0x07000400u;
    const uint32_t obj_vram = eng == ENGINE_A ? 0x06400000u : 0x06600000u;
    const uint32_t obj_pltt = kEngines[eng].pltt_base + 0x200u;
    // DISPCNT bit 4: OBJ 1D mapping; bits 20-21: tile-index boundary shift.
    const uint32_t dispcnt = rd32(kEngines[eng].reg_base);
    if(!(dispcnt&(1u<<12)) || (dispcnt&0x80) || (!capture_source && ((dispcnt>>16)&3)==0))return;
    const bool mapping1D=dispcnt&(1u<<4);
    const uint32_t boundary = 32u << ((dispcnt >> 20) & 3);
    // OBJ palette resolved once: palette memory is fixed during this scan.
    static uint32_t obj_colors[256];
    {const auto* raw=static_cast<const uint16_t*>(host_ptr(obj_pltt));for(unsigned c=0;c<256;++c)obj_colors[c]=bgr555(raw[c]);}
    // 256-colour OBJs use extended palette (a2>>12) instead when bit 31 is set;
    // each one is resolved once per scan, on first use.
    const bool ext_palettes=(dispcnt&0x80000000u)!=0;
    static uint32_t ext_colors[16][256];bool ext_resolved[16]{};
    auto c256_colors=[&](unsigned palette)->const uint32_t*{
        if(!ext_palettes)return obj_colors;
        if(!ext_resolved[palette]){
            for(unsigned c=0;c<256;++c)ext_colors[palette][c]=bgr555(obj_ext_color(eng,(palette*256+c)*2));
            ext_resolved[palette]=true;
        }
        return ext_colors[palette];
    };

    for (int i = 127; i >= 0; --i) {           // low index on top: draw high first
        const uint16_t a0 = rd16(oam_base + i * 8u);
        const uint16_t a1 = rd16(oam_base + i * 8u + 2);
        const uint16_t a2 = rd16(oam_base + i * 8u + 4);
        const bool affine = a0 & 0x100;
        if (!affine && (a0 & 0x200)) continue;             // disabled
        const unsigned obj_mode=(a0>>10)&3;
        if((obj_mode==2)!=window_only)continue;
        const int shape = (a0 >> 14) & 3;
        if (shape == 3) continue;
        const int size = (a1 >> 14) & 3;
        const int w = kObjSizes[shape][size][0], h = kObjSizes[shape][size][1];
        const bool dbl = affine && (a0 & 0x200);
        const int bw = dbl ? w * 2 : w, bh = dbl ? h * 2 : h;
        int x = a1 & 0x1FF, y = a0 & 0xFF;
        if (x >= 256) x -= 512;
        if (y >= 192 && y >= 256 - bh) y -= 256;
        const bool c256 = a0 & 0x2000;
        const bool hflip = !affine && (a1 & 0x1000);
        const bool vflip = !affine && (a1 & 0x2000);
        const uint32_t tile = a2 & 0x3FF;
        const uint32_t pal = (a2 >> 12) & 0xF;
        const uint32_t* const colors256 = c256 && !window_only ? c256_colors(pal) : obj_colors;

        int pa = 256, pb = 0, pc = 0, pd = 256;
        if (affine) {
            const int grp = (a1 >> 9) & 0x1F;
            pa = (int16_t)rd16(oam_base + (grp * 4 + 0) * 8u + 6);
            pb = (int16_t)rd16(oam_base + (grp * 4 + 1) * 8u + 6);
            pc = (int16_t)rd16(oam_base + (grp * 4 + 2) * 8u + 6);
            pd = (int16_t)rd16(oam_base + (grp * 4 + 3) * 8u + 6);
        }

        static const bool fast_obj=std::getenv("PPU_REFERENCE_OBJ")==nullptr;
        if(fast_obj && !affine && obj_mode!=3){
            // Same texels/order as the general loop below: the VRAM view and
            // OBJ palette are contiguous host memory, decoded once per row.
            const uint32_t vram_mask=eng==ENGINE_A?0x3ffffu:0x1ffffu;
            const auto* vram=static_cast<const uint8_t*>(host_ptr(obj_vram));
            const unsigned alpha=obj_mode==1?17u:0u,priority=(a2>>10)&3;
            const int tiles_per_row=w/8;
            const int sx0=std::max(0,clip.x0-x),sx1=std::min(bw,clip.x1-x);
            box(x+sx0,x+sx1,std::max(clip.y0,y),std::min(clip.y1,y+bh));
            for(int sy=0;sy<bh;++sy){
                const int py=y+sy;
                if(py<clip.y0 || py>=clip.y1)continue;
                const int ty=vflip?h-1-sy:sy,trow=ty>>3,fy=ty&7;
                for(int sx=sx0;sx<sx1;++sx){
                    const int tx=hflip?w-1-sx:sx,tcol=tx>>3,fx=tx&7;
                    const uint32_t tno=trow*tiles_per_row+tcol;
                    unsigned index;
                    if(c256){
                        index=vram[((mapping1D?tile*boundary+tno*64u:tile*32u+trow*1024u+tcol*64u)+fy*8u+fx)&vram_mask];
                        if(!index)continue;
                        put(x+sx,py,window_only?0:colors256[index],priority,alpha);
                    }else{
                        const uint8_t b=vram[((mapping1D?tile*boundary+tno*32u:tile*32u+trow*1024u+tcol*32u)+fy*4u+fx/2)&vram_mask];
                        index=(fx&1)?(b>>4):(b&0xF);
                        if(!index)continue;
                        put(x+sx,py,window_only?0:obj_colors[pal*16u+index],priority,alpha);
                    }
                }
            }
            continue;
        }
        if(fast_obj && affine && obj_mode!=3){
            // General loop's affine mapping with the products accumulated
            // incrementally (exact integer sums), texels read as above.
            const uint32_t vram_mask=eng==ENGINE_A?0x3ffffu:0x1ffffu;
            const auto* vram=static_cast<const uint8_t*>(host_ptr(obj_vram));
            const unsigned alpha=obj_mode==1?17u:0u,priority=(a2>>10)&3;
            const int tiles_per_row=w/8;
            const int sx0=std::max(0,clip.x0-x),sx1=std::min(bw,clip.x1-x);
            box(x+sx0,x+sx1,std::max(clip.y0,y),std::min(clip.y1,y+bh));
            for(int sy=0;sy<bh;++sy){
                const int py=y+sy;
                if(py<clip.y0 || py>=clip.y1 || sx0>=sx1)continue;
                const int cy=sy-bh/2;
                int ux=pa*(sx0-bw/2)+pb*cy,uy=pc*(sx0-bw/2)+pd*cy;
                for(int sx=sx0;sx<sx1;++sx,ux+=pa,uy+=pc){
                    const int tx=(ux>>8)+w/2,ty=(uy>>8)+h/2;
                    if(tx<0 || tx>=w || ty<0 || ty>=h)continue;
                    const int tcol=tx>>3,trow=ty>>3,fx=tx&7,fy=ty&7;
                    const uint32_t tno=trow*tiles_per_row+tcol;
                    unsigned index;
                    if(c256){
                        index=vram[((mapping1D?tile*boundary+tno*64u:tile*32u+trow*1024u+tcol*64u)+fy*8u+fx)&vram_mask];
                        if(!index)continue;
                        put(x+sx,py,window_only?0:colors256[index],priority,alpha);
                    }else{
                        const uint8_t b=vram[((mapping1D?tile*boundary+tno*32u:tile*32u+trow*1024u+tcol*32u)+fy*4u+fx/2)&vram_mask];
                        index=(fx&1)?(b>>4):(b&0xF);
                        if(!index)continue;
                        put(x+sx,py,window_only?0:obj_colors[pal*16u+index],priority,alpha);
                    }
                }
            }
            continue;
        }
        box(std::max(clip.x0,x),std::min(clip.x1,x+bw),std::max(clip.y0,y),std::min(clip.y1,y+bh));
        for (int sy = 0; sy < bh; ++sy) {
            const int py = y + sy;
            if (py < clip.y0 || py >= clip.y1) continue;
            for (int sx = 0; sx < bw; ++sx) {
                const int px = x + sx;
                if (px < clip.x0 || px >= clip.x1) continue;
                // texel coordinate: affine maps through the matrix around the
                // box center; plain is direct with optional flips.
                int tx, ty;
                if (affine) {
                    const int cx = sx - bw / 2, cy = sy - bh / 2;
                    tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                    ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                    if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
                } else {
                    tx = hflip ? w - 1 - sx : sx;
                    ty = vflip ? h - 1 - sy : sy;
                }
                if(obj_mode==3){
                    const auto p=sample_bitmap_obj({static_cast<const uint8_t*>(host_ptr(obj_vram)),eng==ENGINE_A?0x3ffffu:0x1ffffu},
                        eng==ENGINE_B?dispcnt&0xc0b1fff7:dispcnt,a2,w,h,tx,ty);
                    if(p)put(px,py,bgr555(p.color),(a2>>10)&3,p.alpha);
                    continue;
                }
                const int tcol = tx >> 3, trow = ty >> 3;
                const int fx = tx & 7, fy = ty & 7;
                // 1D mapping: tiles of one sprite are consecutive.
                const int tiles_per_row = w / 8;
                const uint32_t tno = trow * tiles_per_row + tcol;
                uint32_t index;
                if (c256) {
                    const uint32_t addr = obj_vram + ((mapping1D ? tile*boundary+tno*64u : tile*32u+trow*1024u+tcol*64u) + fy*8u + fx)%(eng==ENGINE_A?0x40000u:0x20000u);
                    index = *static_cast<volatile uint8_t *>(host_ptr(addr));
                    if (!index) continue;
                    put(px,py,window_only?0:colors256[index],(a2>>10)&3,obj_mode==1?17u:0u);
                } else {
                    const uint32_t addr = obj_vram + ((mapping1D ? tile*boundary+tno*32u : tile*32u+trow*1024u+tcol*32u) + fy*4u + fx/2)%(eng==ENGINE_A?0x40000u:0x20000u);
                    const uint8_t b = *static_cast<volatile uint8_t *>(host_ptr(addr));
                    index = (fx & 1) ? (b >> 4) : (b & 0xF);
                    if (!index) continue;
                    put(px,py,window_only?0:bgr555(rd16(obj_pltt+(pal*16u+index)*2u)),(a2>>10)&3,obj_mode==1?17u:0u);
                }
            }
        }
    }
}

static bool build_window_masks(Engine eng,uint8_t* masks,bool capture_source) {
    const unsigned base=kEngines[eng].reg_base,disp=rd32(base);
    if(!(disp&0xe000))return false;
    if(g_window_cut_scope && !capture_source && windows_binary(eng))return false; // cut drawn at composition
    const unsigned outside=rd16(base+0x4a),inside=rd16(base+0x48);
    std::memset(masks,outside&0x3f,SCREEN_W*SCREEN_H);
    if(disp&(1u<<15))scan_obj(eng,[&](int x,int y,uint32_t,unsigned,unsigned){
        masks[y*SCREEN_W+x]=(outside>>8)&0x3f;
    },true,capture_source);
    // WIN0 overrides WIN1, then OBJ-window, then the outside mask.
    auto in_range=[](unsigned value,unsigned start,unsigned end){
        return start<=end ? value>=start && value<end : value>=start || value<end;
    };
    // H-blank handlers may rewrite WIN0H/WIN1H per scanline (scanline_windows.h).
    const unsigned shadow=base+SCANLINE_WINDOW_SHADOW;
    const bool per_line=rd32(shadow)==1;
    for(int window=1;window>=0;--window)if(disp&(1u<<(13+window))){
        const unsigned v=rd16(base+0x44+window*2);
        const unsigned mask=(inside>>(window*8))&0x3f;
        for(unsigned y=0;y<SCREEN_H;++y)if(in_range(y,v>>8,v&255)){
            const unsigned h=per_line?rd16(shadow+4+y*4+window*2):rd16(base+0x40+window*2);
            for(unsigned x=0;x<SCREEN_W;++x)if(in_range(x,h>>8,h&255))masks[y*SCREEN_W+x]=mask;
        }
    }
    return true;
}

void ppu_scanout_obj(Engine eng, Framebuffer &fb) {
    sync_vram_views();
    static uint8_t windows[SCREEN_W*SCREEN_H];const bool enabled=build_window_masks(eng,windows);
    scan_obj(eng,[&](int x,int y,uint32_t color,unsigned,unsigned){
        if(!enabled || (windows[y*SCREEN_W+x]&16))fb.px[y][x]=color;
    });
}

// DS composites six-bit channels; palette channels enter as five-bit <<1.
// Front3D uses its own five-bit alpha plus1, coefficients summing to32.
// Eligible second targets force this blend independently of BLDCNT mode/WIN.
// The viewer/GX native alpha is the five-bit value expanded to eight bits.
static uint32_t native_geometry_blend(uint32_t front,uint32_t back) {
    const unsigned eva=(front>>27)+1,evb=32-eva;
    uint32_t result=0xff000000u;
    for(unsigned shift:{0u,8u,16u}) {
        const unsigned a=(front>>(shift+2))&63,b=(back>>(shift+2))&63;
        const unsigned six=(a*eva+b*evb+16)>>5;
        result|=((six<<2)|(six>>4))<<shift;
    }
    return result;
}

static uint32_t brightness(uint32_t color,unsigned factor,bool up,bool master=false) {
    factor=std::min(factor,16u);uint32_t result=0xff000000u;
    for(unsigned shift:{0u,8u,16u}) {
        unsigned c=((color>>shift)&255)>>2;
        const unsigned bias=master ? (up?0:15) : (up?8:7);
        c=up ? c+((63-c)*factor+bias)/16 : c-(c*factor+bias)/16;
        result|=((c<<2)|(c>>4))<<shift;
    }
    return result;
}

// BLDCNT selects only the front pixel and the immediately underlying layer.
// Blending a partially composed stack would incorrectly recurse through BGs.
static uint32_t alpha_blend(uint32_t front, uint32_t back, unsigned eva, unsigned evb) {
    eva = std::min(eva,16u); evb = std::min(evb,16u);
    uint32_t result = 0xff000000u;
    for (unsigned shift : {0u,8u,16u}) {
        const unsigned a = ((front >> shift) & 255) >> 2;
        const unsigned b = ((back >> shift) & 255) >> 2;
        const unsigned value = std::min(63u,(a*eva+b*evb+8)/16);
        result |= ((value << 2) | (value >> 4)) << shift;
    }
    return result;
}

// minigames-30fps-min: binary windows (ppu.h PpuWindowCut). Region classes
// follow compose_candidates/build_window_masks exactly: a "full" region
// enables every enabled layer (BG0 = 3D, OBJ) and, if BLDCNT has an effect,
// the effect bit (semi OBJ and the native 3D blend ignore that bit anyway);
// an "empty" region enables none of them, so its pixels are the backdrop,
// brightened when its effect bit, mode2/3 and the backdrop first target are
// set, then master brightness. Backdrop as a mode1 first target is refused.
static bool window_cut_enabled(){static const bool value=std::getenv("PPU_NO_WINDOW_CUT")==nullptr;return value;}
static bool windows_binary(Engine eng,uint32_t* empty_color){
    if(!window_cut_enabled())return false;
    const auto& em=kEngines[eng];const unsigned base=em.reg_base,disp=rd32(base);
    if(!(disp&0x6000) || (disp&0x8000) || (disp&0x80) || ((disp>>16)&3)!=1)return false;
    if(eng==ENGINE_A && (rd32(0x04000064)&0x80000000u))return false; // display capture source
    const unsigned enabled=(disp>>8)&0x1f,blend=rd16(base+0x50),mode=(blend>>6)&3;
    const unsigned inside=rd16(base+0x48),outside=rd16(base+0x4a);
    unsigned masks[3]={outside&0x3f,0,0};unsigned regions=1;
    if(disp&(1u<<13))masks[regions++]=inside&0x3f;
    if(disp&(1u<<14))masks[regions++]=(inside>>8)&0x3f;
    bool have_empty=false;uint32_t color=0;
    for(unsigned r=0;r<regions;++r){
        const unsigned m=masks[r];
        if((m&enabled)==enabled && ((m&0x20) || mode==0))continue; // full
        if((m&enabled)!=0 || ((m&0x20) && mode==1 && (blend&0x20)))return false;
        uint32_t c=bgr555(rd16(em.pltt_base));
        if((m&0x20) && (mode==2 || mode==3) && (blend&0x20))c=brightness(c,rd16(base+0x54)&31,mode==2);
        const unsigned master=rd16(base+0x6c),master_mode=master>>14;
        if(master_mode==1 || master_mode==2)c=brightness(c,master&31,master_mode==1,true);
        c|=0xff000000u;
        if(have_empty && c!=color)return false; // one colour per frame
        have_empty=true;color=c;
    }
    if(empty_color)*empty_color=have_empty?color:0;
    return true;
}
static uint32_t dispcnt_key_value(Engine eng){
    const uint32_t disp=rd32(kEngines[eng].reg_base);
    if(!g_window_cut_scope || !window_cut_enabled())return disp;
    return (!(disp&0xe000) || windows_binary(eng))?disp&~0xe000u:disp;
}
static uint32_t dispcnt_key_address(Engine eng){
    const uint32_t base=kEngines[eng].reg_base;
    if(!g_window_cut_scope || !window_cut_enabled())return base;
    *static_cast<volatile uint32_t*>(host_ptr(base+DISPCNT_KEY_SHADOW))=dispcnt_key_value(eng);
    return base+DISPCNT_KEY_SHADOW;
}
// Rectangles of the empty pixels (count 0: windows are a no-op). False when
// the windows are not binary. Same region selection as build_window_masks.
static bool compute_window_cut(Engine eng,PpuWindowCut& cut){
    uint32_t color=0;
    if(!windows_binary(eng,&color))return false;
    cut.count=0;cut.color=color;cut.signature=0;
    if(!color)return true;
    const auto& em=kEngines[eng];const unsigned base=em.reg_base,disp=rd32(base);
    const unsigned enabled=(disp>>8)&0x1f,blend=rd16(base+0x50),mode=(blend>>6)&3;
    const unsigned inside=rd16(base+0x48),outside=rd16(base+0x4a);
    auto is_empty=[&](unsigned m){return !((m&enabled)==enabled && ((m&0x20) || mode==0));};
    const bool outside_empty=is_empty(outside&0x3f);
    const bool inside_empty[2]={is_empty(inside&0x3f),is_empty((inside>>8)&0x3f)};
    const unsigned shadow=base+SCANLINE_WINDOW_SHADOW;
    const bool per_line=rd32(shadow)==1;
    uint8_t row[SCREEN_W];
    PpuWindowCut::Rect open[SCREEN_W/2];unsigned open_count=0;
    uint64_t h=0x9e3779b97f4a7c15ull^color;
    auto fill=[&](unsigned start,unsigned end,uint8_t value){ // in_range(x,start,end) of build_window_masks
        if(start<=end){if(end>start)std::memset(row+start,value,std::min(end,unsigned(SCREEN_W))-std::min(start,unsigned(SCREEN_W)));}
        else{if(start<SCREEN_W)std::memset(row+start,value,SCREEN_W-start);std::memset(row,value,std::min(end,unsigned(SCREEN_W)));}
    };
    auto in_range=[](unsigned value,unsigned start,unsigned end){
        return start<=end ? value>=start && value<end : value>=start || value<end;
    };
    for(unsigned y=0;y<=SCREEN_H;++y){
        PpuWindowCut::Rect runs[SCREEN_W/2];unsigned run_count=0;
        if(y<SCREEN_H){
            std::memset(row,outside_empty,SCREEN_W);
            for(int window=1;window>=0;--window)if(disp&(1u<<(13+window))){
                const unsigned v=rd16(base+0x44+window*2);
                if(!in_range(y,v>>8,v&255))continue;
                const unsigned hw=per_line?rd16(shadow+4+y*4+window*2):rd16(base+0x40+window*2);
                fill(hw>>8,hw&255,inside_empty[window]);
            }
            for(unsigned x=0;x<SCREEN_W;){
                if(!row[x]){++x;continue;}
                unsigned e=x;while(e<SCREEN_W && row[e])++e;
                runs[run_count++]={uint16_t(x),uint16_t(e),uint16_t(y),uint16_t(y+1)};x=e;
            }
        }
        // Extend the open rectangles when this row has the same runs.
        bool same=run_count==open_count;
        for(unsigned i=0;same && i<run_count;++i)same=runs[i].x0==open[i].x0 && runs[i].x1==open[i].x1;
        if(same && y<SCREEN_H){for(unsigned i=0;i<open_count;++i)open[i].y1=uint16_t(y+1);continue;}
        for(unsigned i=0;i<open_count;++i){
            if(cut.count>=PpuWindowCut::kMaxRects)return false;
            cut.rects[cut.count++]=open[i];
            h^=(uint64_t(open[i].x0)|uint64_t(open[i].x1)<<16|uint64_t(open[i].y0)<<32|uint64_t(open[i].y1)<<48);h*=0x100000001b3ull;h^=h>>29;
        }
        open_count=run_count;for(unsigned i=0;i<run_count;++i)open[i]=runs[i];
    }
    cut.signature=h^cut.count;
    return true;
}
// Ring of cuts per engine: a view keeps its cut for four more preparations
// (the presentation worker reads the previous frame's while the next is made).
// Signature: 0 not binary, 1 binary without empty pixels, else the cut's.
static const PpuWindowCut* window_cut_for(Engine eng){
    static PpuWindowCut* ring[2]={nullptr,nullptr};static unsigned next[2]{};
    g_window_cut_issued[eng]=0;
    if(!ring[eng]){ // host memory (malloc), never the game heaps
        auto* memory=static_cast<PpuWindowCut*>(std::malloc(sizeof(PpuWindowCut)*5));if(!memory)return nullptr;
        for(unsigned i=0;i<5;++i)new(memory+i) PpuWindowCut();ring[eng]=memory;
    }
    auto& cut=ring[eng][next[eng]];
    const bool binary=compute_window_cut(eng,cut);
    static const bool trace=std::getenv("PPU_WINDOW_CUT_TRACE")!=nullptr;
    if(trace){
        const unsigned base=kEngines[eng].reg_base,disp=rd32(base);
        const uint64_t key=(uint64_t(disp&0xe000)<<32)|(uint64_t(rd16(base+0x48))<<16)|rd16(base+0x4a)|(uint64_t(rd16(base+0x50))<<48)|(uint64_t(binary)<<47);
        static uint64_t last[2]={~0ull,~0ull},calls=0;static unsigned printed=0;++calls;
        if((disp&0xe000) && key!=last[eng] && printed<400){last[eng]=key;++printed;
            std::printf("WINDOW_CUT eng=%u call=%llu disp=%08x in=%04x out=%04x bld=%04x master=%04x binary=%u rects=%u color=%08x\n",unsigned(eng),
                (unsigned long long)calls,disp,unsigned(rd16(base+0x48)),unsigned(rd16(base+0x4a)),unsigned(rd16(base+0x50)),unsigned(rd16(base+0x6c)),
                unsigned(binary),binary?cut.count:0u,binary?unsigned(cut.color):0u);}
    }
    if(!binary)return nullptr;
    if(!cut.count){g_window_cut_issued[eng]=1;return nullptr;}
    g_window_cut_issued[eng]=cut.signature|2;
    next[eng]=(next[eng]+1)%5;
    return &cut;
}
static uint64_t window_cut_signature(Engine eng){
    static PpuWindowCut* probe=nullptr;
    if(!probe){auto* memory=static_cast<PpuWindowCut*>(std::malloc(sizeof(PpuWindowCut)));if(memory)probe=new(memory) PpuWindowCut();}
    if(!probe || !compute_window_cut(eng,*probe))return 0;
    return probe->count?probe->signature|2:1;
}

void ppu_scanout_scene_with_geometry(Engine eng,Framebuffer& fb,const Framebuffer& geometry) {
    sync_vram_views();
    const auto& em=kEngines[eng];const auto disp=rd32(em.reg_base);
    if((disp&0x80) || ((disp>>16)&3)==0){scanout_bg(eng,fb,nullptr,nullptr,nullptr);return;}
    static Framebuffer behind, objects;
    static uint8_t layers[SCREEN_W*SCREEN_H],priorities[SCREEN_W*SCREEN_H];
    static uint8_t behind_layers[SCREEN_W*SCREEN_H],behind_priorities[SCREEN_W*SCREEN_H];
    static uint8_t object_priorities[SCREEN_W*SCREEN_H];
    static uint8_t object_alpha[SCREEN_W*SCREEN_H];
    const unsigned blend=rd16(em.reg_base+0x50),evy=rd16(em.reg_base+0x54)&31;
    const unsigned master=rd16(em.reg_base+0x6c),mode=(blend>>6)&3;
    const unsigned alpha=rd16(em.reg_base+0x52);
    const bool has3d=eng==ENGINE_A && (disp&8) && (disp&(1u<<8));
    static uint8_t windows[SCREEN_W*SCREEN_H];
    const bool has_windows=build_window_masks(eng,windows);
    // OBJ is one plane: resolve OAM overlap before merging with BGs. A second
    // sprite must never become the blend target beneath the winning sprite.
    std::memset(object_priorities,0xff,sizeof(object_priorities));
    bool has_objects=false,has_obj_alpha=false;
    scan_obj(eng,[&](int x,int y,uint32_t color,unsigned priority,unsigned obj_alpha){
        if(has_windows && !(windows[y*SCREEN_W+x]&16))return;
        has_objects=true;
        const unsigned i=y*SCREEN_W+x;
        if(priority<=(object_priorities[i]&0x7f)){
            objects.px[y][x]=color;object_priorities[i]=priority|(obj_alpha?0x80:0);
            object_alpha[i]=obj_alpha;has_obj_alpha|=obj_alpha!=0;
        }
    });
    const bool needs_second=mode==1 || ((has_obj_alpha || has3d) && (blend&0x3f00));
    scanout_bg(eng,fb,has3d?&geometry:nullptr,layers,priorities,
        needs_second?&behind:nullptr,needs_second?behind_layers:nullptr,needs_second?behind_priorities:nullptr,
        has_windows?windows:nullptr);
    if(has_objects)for(int y=0;y<SCREEN_H;++y)for(int x=0;x<SCREEN_W;++x){
        const unsigned i=y*SCREEN_W+x, priority=object_priorities[i]&0x7f;
        if(priority<=priorities[i]){
            if(needs_second){behind.px[y][x]=fb.px[y][x]; behind_layers[i]=layers[i];}
            fb.px[y][x]=objects.px[y][x]; layers[i]=4|(object_priorities[i]&0x80); priorities[i]=priority;
        } else if(needs_second && priority<=behind_priorities[i]) {
            behind.px[y][x]=objects.px[y][x]; behind_layers[i]=4;
        }
    }
    const bool layer_effect=(mode!=0 && (blend&0x3f)!=0) || ((has_obj_alpha || has3d) && (blend&0x3f00));
    const bool master_effect=(master>>14)==1 || (master>>14)==2;
    if(!layer_effect && !master_effect){
        if(has3d)for(auto& row:fb.px)for(auto& c:row)c|=0xff000000u;
        return;
    }
    for(int y=0;y<SCREEN_H;++y)for(int x=0;x<SCREEN_W;++x){
        auto& c=fb.px[y][x];
        const bool effects=!has_windows || (windows[y*SCREEN_W+x]&32);
        const unsigned layer=layers[y*SCREEN_W+x]&0x7f;
        const bool forced_alpha=(layers[y*SCREEN_W+x]&0x80) &&
            (blend&(1u<<(8+behind_layers[y*SCREEN_W+x])));
        const bool forced_geometry=has3d && layer==0 && (blend&(1u<<(8+behind_layers[y*SCREEN_W+x])));
        if(forced_geometry)c=native_geometry_blend(c,behind.px[y][x]);
        else if(forced_alpha || (effects && mode==1 && (blend&(1u<<layer)) &&
           (blend&(1u<<(8+behind_layers[y*SCREEN_W+x])))))
            c=forced_alpha && object_alpha[y*SCREEN_W+x]<=16
                ?alpha_blend(c,behind.px[y][x],object_alpha[y*SCREEN_W+x],16-object_alpha[y*SCREEN_W+x])
                :alpha_blend(c,behind.px[y][x],alpha&31,(alpha>>8)&31);
        else if(effects && (mode==2 || mode==3) && (blend&(1u<<layer)))c=brightness(c,evy,mode==2);
        if((master>>14)==1 || (master>>14)==2)c=brightness(c,master&31,(master>>14)==1,true);
        c|=0xff000000u;
    }
}

void ppu_prepare_profile_clock(double (*clock)()){prepare_clock=clock;}
void ppu_prepare_profile_add_obj_plan(Engine eng,double ms){prepare_profiles[eng].obj_plan+=ms;}
double (*ppu_prepare_profile_clock_get())(){return prepare_clock;}
PpuPrepareProfile ppu_prepare_profile_read(Engine eng,bool reset){
    const auto result=prepare_profiles[eng];if(reset)prepare_profiles[eng]={};return result;
}
namespace {
struct BgInputSpan {uint32_t address;size_t size;};
// minigames-2d-gpu: VRAMCNT bytes an engine's BG/OBJ samplers depend on.
// With the flat VRAM model (no physical bus) BG/OBJ read fixed host windows;
// only the extended-palette banks (A: E,F,G; B: H) are selected by VRAMCNT.
// Banks A-D toggling between LCDC and the other engine (display capture
// every frame) must not invalidate this engine's caches. Physical VRAM keeps
// all of them (views follow the mapping).
static BgInputSpan vramcnt_span(Engine eng){
    static const bool all=std::getenv("PPU_FULL_VRAMCNT_KEY")!=nullptr;
    if(all || io_vram_banks())return {0x04000240u,9};
    return eng==ENGINE_A?BgInputSpan{0x04000244u,3}:BgInputSpan{0x04000248u,1};
}
// Byte-sized MMIO spans must not misalign subsequent palette/VRAM snapshots:
// ARM memcmp can compare aligned ranges a word at a time. Padding belongs only
// to the retained buffer; it is never read from or compared with source memory.
static size_t cache_snapshot_offset(size_t offset){
    return (offset+3)&~size_t(3);
}

struct BgCandidateCache {
    // Double-buffered: a miss rasterizes into the idle buffer, so a consumer
    // still reading the previous planes (the core-1 presenter) is unaffected.
    PpuBackgroundPlanes planes[2];
    unsigned current=0;
    HostVector<BgInputSpan> spans;
    HostVector<uint8_t> snapshot,windows;
    uint64_t generation=0;
    bool valid=false,has_windows=false;
    TextLayerRaster layers[4];
    // Rows in which planes[current] differs from planes[current^1].
    uint8_t changed_rows[SCREEN_H]{};
    bool changed_all=true;
    // minigames-2d-gpu: planes[i].front alpha holds ppu_front_alpha_code.
    bool alpha_encoded[2]{};
};
// minigames-30fps-min: the active cache way of each engine (see select_cache_way).
static BgCandidateCache background_cache_storage[2];
static BgCandidateCache* background_cache[2]={&background_cache_storage[0],&background_cache_storage[1]};
static uint64_t background_scroll_reuses,background_row_reuses,background_dirty_rows,background_full_rasters,background_row_refusals[8];

// True when the cached BG inputs differ from DS memory only in the vertical
// scroll of the single enabled text BG. rows = new VOFS - old VOFS, wrapped
// to the map height, with 0 < |rows| < SCREEN_H. Each text-BG scanline
// depends on its own source row only, so a shifted raster is exact.
static bool vertical_scroll_only(Engine eng,const BgCandidateCache& cache,const HostVector<BgInputSpan>& spans,int& rows){
    const auto& em=kEngines[eng];const auto disp=rd32(em.reg_base);
    int enabled=-1;
    for(int bg=0;bg<4;++bg)if(read_bg(em,bg,disp).enabled){if(enabled>=0)return false;enabled=bg;}
    if(enabled<0)return false;
    const auto config=read_bg(em,enabled,disp);
    if(config.extended_tiled)return false;
    const uint32_t vofs=em.reg_base+0x12+enabled*4;
    size_t offset=0;bool found=false;uint16_t old_vofs=0;
    for(const auto& span:spans){
        offset=cache_snapshot_offset(offset);
        const auto* old=cache.snapshot.data()+offset;
        const auto* now=static_cast<const uint8_t*>(host_ptr(span.address));
        if(vofs>=span.address && vofs+2<=span.address+span.size){
            const size_t at=vofs-span.address;
            if(std::memcmp(old,now,at) || std::memcmp(old+at+2,now+at+2,span.size-at-2))return false;
            old_vofs=uint16_t(old[at]|(old[at+1]<<8));found=true;
        }else if(std::memcmp(old,now,span.size))return false;
        offset+=span.size;
    }
    if(!found)return false;
    const int height=config.tiles_h*8;
    int delta=(int(config.vofs)-int(old_vofs&0x1ff))&(height-1);
    if(delta>height/2)delta-=height;
    if(delta==0 || delta>=SCREEN_H || delta<=-SCREEN_H)return false;
    rows=delta;return true;
}
// Generalised reuse for text-only, window-free BGs: every enabled layer has
// the same vertical scroll delta and an unchanged horizontal scroll, BG
// registers/VRAMCNT/palettes are otherwise identical, and map entries or
// character tiles may change. Then new row y equals old row y+rows except for
// rows exposed by the shift and rows whose visible map cells changed (entry
// value or referenced tile bytes). Those rows are marked in dirty[].
static bool row_incremental(Engine eng,const BgCandidateCache& cache,const HostVector<BgInputSpan>& spans,
                            int& rows,uint8_t* dirty){
    const auto& em=kEngines[eng];const auto disp=rd32(em.reg_base);
    auto old_at=[&](uint32_t address,size_t size)->const uint8_t*{
        size_t offset=0;
        for(const auto& span:spans){
            offset=cache_snapshot_offset(offset);
            if(address>=span.address && address+size<=span.address+span.size)return cache.snapshot.data()+offset+(address-span.address);
            offset+=span.size;
        }
        return nullptr;
    };
    auto same=[&](uint32_t address,size_t size){
        const auto* old=old_at(address,size);
        return old && !std::memcmp(old,host_ptr(address),size);
    };
    // castle-30fps: an extended (affine) tiled BG whose registers, map,
    // tiles and palette are all unchanged contributes identical pixels, so
    // text-layer row updates stay exact beside it (bottom-screen maps).
    static const bool affine_rows=std::getenv("PPU_NO_AFFINE_ROW_REUSE")==nullptr;
    BgConfig bgs[4];int enabled=0;bool has_affine=false;
    for(int bg=0;bg<4;++bg){
        bgs[bg]=read_bg(em,bg,disp);
        if(!bgs[bg].enabled)continue;
        if(bgs[bg].extended_tiled && (!affine_rows || bgs[bg].extended_bitmap)){++background_row_refusals[5];return false;}
        has_affine|=bgs[bg].extended_tiled;
        ++enabled;
    }
    auto ext_palette_same=[&](unsigned slot){
        const unsigned offset=slot*8192;
        if(!em.has_base_offset)return (rd16(0x04000248)&0x83)!=0x82 || same(0x06898000+offset,8192);
        if((rd16(0x04000244)&0x87)==0x84 && !same(0x06880000+offset,8192))return false;
        for(unsigned bank=0;bank<2;++bank){
            const unsigned cnt=*static_cast<volatile uint8_t*>(host_ptr(0x04000245+bank));
            if((cnt&0x87)==0x84 && (offset>>14)==((cnt>>3)&1) && !same(0x06890000+bank*0x4000+(offset&0x3fff),8192))return false;
        }
        return true;
    };
    if(!enabled){++background_row_refusals[0];return false;}
    const auto vramcnt=vramcnt_span(eng);
    if(!same(dispcnt_key_address(eng),4) || !same(em.reg_base+8,8) || !same(vramcnt.address,vramcnt.size) || !same(em.pltt_base,0x200)){++background_row_refusals[1];return false;}
    // Affine/window/mosaic registers (0x20..0x4f) are not sampled by text BGs
    // without windows, but stay part of the exact key: require them equal.
    if(!same(em.reg_base+0x20,0x20))return false;
    bool have_delta=false;int delta=0;
    for(int bg=0;bg<4;++bg){
        const uint32_t scroll=em.reg_base+0x10+bg*4;
        const auto* old=old_at(scroll,4);if(!old)return false;
        if(!bgs[bg].enabled){if(std::memcmp(old,host_ptr(scroll),4))return false;continue;}
        if(bgs[bg].extended_tiled)continue; // scroll registers unused by affine BGs
        const unsigned old_h=(old[0]|(old[1]<<8))&0x1ff,old_v=(old[2]|(old[3]<<8))&0x1ff;
        if(old_h!=unsigned(bgs[bg].hofs)){++background_row_refusals[2];return false;}
        const int height=bgs[bg].tiles_h*8;
        int d=(int(bgs[bg].vofs)-int(old_v))&(height-1);
        if(d>height/2)d-=height;
        if(have_delta && d!=delta){++background_row_refusals[3];return false;}
        delta=d;have_delta=true;
    }
    if(delta>=SCREEN_H || delta<=-SCREEN_H)return false;
    if(has_affine && delta)return false; // affine pixels do not follow a text scroll
    std::memset(dirty,0,SCREEN_H);
    if(delta>0)std::memset(dirty+SCREEN_H-delta,1,delta);
    else if(delta<0)std::memset(dirty,1,-delta);
    for(int bg=0;bg<4;++bg){
        const auto& c=bgs[bg];if(!c.enabled)continue;
        if(c.extended_tiled){
            if(!same(c.screen,size_t(c.tiles_w)*c.tiles_h*2) || !same(c.chars,65536) ||
               (c.ext_palette && !ext_palette_same(c.palette_slot))){++background_row_refusals[5];return false;}
            continue;
        }
        if(c.bpp8 && c.ext_palette && (!affine_rows || !ext_palette_same(c.palette_slot))){++background_row_refusals[4];return false;} // changed extended palettes: full raster
        const unsigned tile_bytes=c.bpp8?64:32;
        const auto* old_chars=old_at(c.chars,c.bpp8?65536:32768);
        const auto* old_map=old_at(c.screen,size_t(c.tiles_w)*c.tiles_h*2);
        if(!old_chars || !old_map)return false;
        const auto* chars=static_cast<const uint8_t*>(host_ptr(c.chars));
        const auto* map=static_cast<const uint8_t*>(host_ptr(c.screen));
        static uint8_t tile_changed[1024];
        bool any_tile=false;
        if(std::memcmp(old_chars,chars,c.bpp8?65536:32768)){
            for(unsigned tile=0;tile<1024;++tile){
                tile_changed[tile]=std::memcmp(old_chars+tile*tile_bytes,chars+tile*tile_bytes,tile_bytes)!=0;
                any_tile|=tile_changed[tile];
            }
        }
        const bool map_changed=std::memcmp(old_map,map,size_t(c.tiles_w)*c.tiles_h*2)!=0;
        if(!any_tile && !map_changed)continue;
        // Visible tile columns for this layer's (unchanged) horizontal scroll.
        uint8_t visible[64]{};
        for(int x=0;x<SCREEN_W;++x)visible[((x+c.hofs)&(c.tiles_w*8-1))>>3]=1;
        uint8_t row_dirty[64]{};
        for(int ty=0;ty<c.tiles_h;++ty)for(int tx=0;tx<c.tiles_w;++tx){
            if(!visible[tx])continue;
            const uint32_t at=screen_entry_addr(c,tx,ty)-c.screen;
            const uint16_t now=uint16_t(map[at]|(map[at+1]<<8)),was=uint16_t(old_map[at]|(old_map[at+1]<<8));
            if(now!=was || (any_tile && tile_changed[now&0x3ff])){row_dirty[ty]=1;}
        }
        for(int y=0;y<SCREEN_H;++y)if(row_dirty[((y+c.vofs)&(c.tiles_h*8-1))>>3])dirty[y]=1;
    }
    rows=delta;return true;
}
// minigames-2d-gpu: store ppu_front_alpha_code in the front alpha of the
// given rows (rows==nullptr: all of [y0,y1)). Row copies carry it along.
static void encode_front_alpha(Framebuffer& front,const uint8_t* layers,const uint8_t* priorities,
                               int y0,int y1,const uint8_t* rows=nullptr){
    // Four pixels per step: code = 32*p + 48 + 16*(layer!=4), p<=4 and
    // layer<8 in these planes (priority 4 = backdrop), stored straight into
    // the alpha byte of each little-endian 0xAARRGGBB word.
    for(int y=y0;y<y1;++y){
        if(rows && !rows[y])continue;
        uint8_t* bytes=reinterpret_cast<uint8_t*>(front.px[y]);
        const uint32_t* l=reinterpret_cast<const uint32_t*>(layers+y*SCREEN_W);
        const uint32_t* p=reinterpret_cast<const uint32_t*>(priorities+y*SCREEN_W);
        for(int k=0;k<SCREEN_W/4;++k){
            const uint32_t eq=(l[k]&0x07070707u)^0x04040404u;
            const uint32_t nz=(eq|(eq>>1)|(eq>>2))&0x01010101u;
            const uint32_t a=((p[k]&0x07070707u)<<5)+0x30303030u+(nz<<4);
            bytes[k*16+3]=uint8_t(a);bytes[k*16+7]=uint8_t(a>>8);
            bytes[k*16+11]=uint8_t(a>>16);bytes[k*16+15]=uint8_t(a>>24);
        }
    }
}
// Row y of the new raster equals row y+rows of the old one.
static void shift_rows(PpuBackgroundPlanes& planes,const PpuBackgroundPlanes& old,int rows){
    const int keep=SCREEN_H-(rows>0?rows:-rows);
    auto copy=[&](auto* to,const auto* from,size_t row_bytes){
        auto* d=reinterpret_cast<uint8_t*>(to);const auto* s=reinterpret_cast<const uint8_t*>(from);
        if(rows>0)std::memcpy(d,s+rows*row_bytes,keep*row_bytes);
        else std::memcpy(d-rows*row_bytes,s,keep*row_bytes);
    };
    copy(&planes.front.px[0][0],&old.front.px[0][0],SCREEN_W*4);copy(&planes.back.px[0][0],&old.back.px[0][0],SCREEN_W*4);
    copy(planes.front_layer,old.front_layer,SCREEN_W);copy(planes.front_priority,old.front_priority,SCREEN_W);
    copy(planes.back_layer,old.back_layer,SCREEN_W);copy(planes.back_priority,old.back_priority,SCREEN_W);
}
static uint64_t background_hits,background_misses;
static PpuBackgroundTransferStats background_transfers{};

// OAM animation does not change BG candidates unless it changes an OBJ-window
// mask. Keep the BG planes before objects/effects, with an exact snapshot of
// every supported BG sampler input and the resolved window masks.
static const PpuBackgroundPlanes& background_planes_cached(Engine eng,BgCandidateCache& cache,
                                                          bool has_windows,const uint8_t* windows,
                                                          bool uncached=false,bool encode_alpha=false){
    const auto& em=kEngines[eng];
    static HostVector<BgInputSpan> spans;spans.clear();
    auto add=[&](uint32_t address,size_t size){spans.push_back({address,size});};
    add(dispcnt_key_address(eng),4);add(em.reg_base+8,0x38);
    {const auto v=vramcnt_span(eng);add(v.address,v.size);}add(em.pltt_base,0x200);
    const auto disp=rd32(em.reg_base);
    auto extended_palette=[&](unsigned slot){
        const unsigned offset=slot*8192;
        if(!em.has_base_offset){
            if((rd16(0x04000248)&0x83)==0x82)add(0x06898000+offset,8192);
            return;
        }
        if((rd16(0x04000244)&0x87)==0x84)add(0x06880000+offset,8192);
        for(unsigned bank=0;bank<2;++bank){
            const unsigned cnt=*static_cast<volatile uint8_t*>(host_ptr(0x04000245+bank));
            if((cnt&0x87)==0x84 && (offset>>14)==((cnt>>3)&1))
                add(0x06890000+bank*0x4000+(offset&0x3fff),8192);
        }
    };
    for(int bg=0;bg<4;++bg){
        const auto config=read_bg(em,bg,disp);if(!config.enabled)continue;
        if(config.extended_bitmap)add(config.bitmap_base,config.bitmap_mask+1);
        else {add(config.screen,config.tiles_w*config.tiles_h*2);add(config.chars,config.bpp8?65536:32768);}
        if(config.bpp8 && config.ext_palette)extended_palette(config.palette_slot);
    }
    std::sort(spans.begin(),spans.end(),[](const BgInputSpan& a,const BgInputSpan& b){return a.address<b.address;});
    unsigned merged=0;
    for(unsigned i=0;i<spans.size();++i){
        if(merged && spans[i].address<=spans[merged-1].address+spans[merged-1].size){
            auto& previous=spans[merged-1];
            previous.size=std::max(previous.size,size_t(spans[i].address-previous.address)+spans[i].size);
        }else spans[merged++]=spans[i];
    }
    spans.resize(merged);
    size_t size=0;for(const auto& span:spans){size=cache_snapshot_offset(size);size+=span.size;}
    bool same=!uncached && cache.valid && cache.snapshot.size()==size && cache.spans.size()==merged &&
        cache.has_windows==has_windows;
    for(unsigned i=0;same && i<merged;++i)
        same=cache.spans[i].address==spans[i].address && cache.spans[i].size==spans[i].size;
    const bool layout_same=same;
    if(same && has_windows)
        same=std::memcmp(cache.windows.data(),windows,SCREEN_W*SCREEN_H)==0;
    size_t offset=0;
    if(same)for(const auto& span:spans){
        offset=cache_snapshot_offset(offset);
        background_transfers.input_compared+=span.size;
        if(std::memcmp(cache.snapshot.data()+offset,host_ptr(span.address),span.size)!=0){same=false;break;}
        offset+=span.size;
    }
    if(!same){
        ++background_misses;
        const double raster_start=prepare_clock?prepare_clock():0;
        const auto& previous=cache.planes[cache.current];
        auto& planes=cache.planes[cache.current^1];
        int rows=0;
        static const bool scroll_reuse=std::getenv("PPU_NO_SCROLL_REUSE")==nullptr;
        static const bool row_reuse=std::getenv("PPU_NO_ROW_REUSE")==nullptr;
        static uint8_t dirty_rows[SCREEN_H];
        double tick[3]{};if(prepare_clock)tick[0]=prepare_clock();
        // castle-30fps: identical window masks (e.g. a dialog box while its
        // text is typed) keep row updates exact; windows do not scroll, so
        // only the in-place (rows==0) update is allowed with them.
        static const bool window_rows=std::getenv("PPU_NO_WINDOW_ROW_REUSE")==nullptr;
        const bool same_windows=!has_windows ? !cache.has_windows :
            (window_rows && cache.has_windows && cache.windows.size()==size_t(SCREEN_W*SCREEN_H) && !std::memcmp(cache.windows.data(),windows,SCREEN_W*SCREEN_H));
        bool row_ok=row_reuse && layout_same && same_windows && row_incremental(eng,cache,spans,rows,dirty_rows);
        if(row_ok && has_windows && rows!=0)row_ok=false;
        if(prepare_clock && has_windows && !row_ok)std::printf("BG_ROW_REFUSED eng=%u layout=%u sameWindows=%u rows=%d refusals=%llu,%llu,%llu,%llu,%llu,%llu\n",unsigned(eng),unsigned(layout_same),unsigned(same_windows),rows,
            (unsigned long long)background_row_refusals[0],(unsigned long long)background_row_refusals[1],(unsigned long long)background_row_refusals[2],(unsigned long long)background_row_refusals[3],(unsigned long long)background_row_refusals[4],(unsigned long long)background_row_refusals[5]);
        if(prepare_clock){tick[1]=prepare_clock();prepare_profiles[eng].detail[0]+=tick[1]-tick[0];}
        if(row_ok){
            static const bool sparse_copy=std::getenv("PPU_NO_SPARSE_ROW_COPY")==nullptr;
            const bool sparse=rows==0 && sparse_copy && !cache.changed_all;
            // Kept rows come from the previous buffer (and, for the sparse
            // copy, from the idle buffer itself): their alpha is valid only
            // if those buffers were encoded.
            const bool kept_alpha=cache.alpha_encoded[cache.current] && (!sparse || cache.alpha_encoded[cache.current^1]);
            if(sparse){
                // The idle buffer already equals the current one except in
                // the rows the last update changed: bring only those over.
                for(int y=0;y<SCREEN_H;++y)if(cache.changed_rows[y]){
                    const size_t at=size_t(y)*SCREEN_W;
                    std::memcpy(planes.front.px[y],previous.front.px[y],SCREEN_W*4);
                    std::memcpy(planes.back.px[y],previous.back.px[y],SCREEN_W*4);
                    std::memcpy(planes.front_layer+at,previous.front_layer+at,SCREEN_W);
                    std::memcpy(planes.front_priority+at,previous.front_priority+at,SCREEN_W);
                    std::memcpy(planes.back_layer+at,previous.back_layer+at,SCREEN_W);
                    std::memcpy(planes.back_priority+at,previous.back_priority+at,SCREEN_W);
                }
            }else shift_rows(planes,previous,rows);
            if(prepare_clock){tick[2]=prepare_clock();prepare_profiles[eng].detail[1]+=tick[2]-tick[1];}
            static const bool layered=std::getenv("PPU_NO_LAYER_CACHE")==nullptr;
            scanout_bg(eng,planes.front,nullptr,planes.front_layer,planes.front_priority,
                &planes.back,planes.back_layer,planes.back_priority,has_windows?windows:nullptr,false,0,SCREEN_H,
                layered?cache.layers:nullptr,dirty_rows);
            if(encode_alpha)encode_front_alpha(planes.front,planes.front_layer,planes.front_priority,0,SCREEN_H,kept_alpha?dirty_rows:nullptr);
            cache.alpha_encoded[cache.current^1]=encode_alpha;
            ++background_row_reuses;
            for(int y=0;y<SCREEN_H;++y)background_dirty_rows+=dirty_rows[y];
            if(rows==0){std::memcpy(cache.changed_rows,dirty_rows,SCREEN_H);cache.changed_all=false;}
            else cache.changed_all=true;
        }else if(cache.changed_all=true,scroll_reuse && layout_same && !has_windows && vertical_scroll_only(eng,cache,spans,rows)){
            // Shift the previous raster; rasterize only the exposed rows.
            shift_rows(planes,previous,rows);
            const int first=rows>0?SCREEN_H-rows:0,last=rows>0?SCREEN_H:-rows;
            scanout_bg(eng,planes.front,nullptr,planes.front_layer,planes.front_priority,
                &planes.back,planes.back_layer,planes.back_priority,nullptr,false,first,last);
            if(encode_alpha){
                if(cache.alpha_encoded[cache.current])encode_front_alpha(planes.front,planes.front_layer,planes.front_priority,first,last);
                else encode_front_alpha(planes.front,planes.front_layer,planes.front_priority,0,SCREEN_H);
            }
            cache.alpha_encoded[cache.current^1]=encode_alpha;
            ++background_scroll_reuses;
        }else{
            ++background_full_rasters;
            static const bool layered=std::getenv("PPU_NO_LAYER_CACHE")==nullptr;
            scanout_bg(eng,planes.front,nullptr,planes.front_layer,planes.front_priority,
                &planes.back,planes.back_layer,planes.back_priority,has_windows?windows:nullptr,false,0,SCREEN_H,
                layered?cache.layers:nullptr);
            if(encode_alpha)encode_front_alpha(planes.front,planes.front_layer,planes.front_priority,0,SCREEN_H);
            cache.alpha_encoded[cache.current^1]=encode_alpha;
        }
        cache.has_windows=has_windows;
        if(has_windows){cache.windows.resize(SCREEN_W*SCREEN_H);std::memcpy(cache.windows.data(),windows,SCREEN_W*SCREEN_H);}
        const double snap_start=prepare_clock?prepare_clock():0;
        if(prepare_clock)prepare_profiles[eng].detail[2]+=snap_start-(tick[2]?tick[2]:tick[1]);
        cache.spans=spans;cache.snapshot.resize(size);offset=0;
        for(const auto& span:spans){
            offset=cache_snapshot_offset(offset);
            std::memcpy(cache.snapshot.data()+offset,host_ptr(span.address),span.size);offset+=span.size;
            background_transfers.input_snapshotted+=span.size;
        }
        cache.current^=1;
        cache.valid=true;++cache.generation;
        if(prepare_clock)prepare_profiles[eng].detail[3]+=prepare_clock()-snap_start;
        if(prepare_clock){auto& profile=prepare_profiles[eng];profile.raster+=prepare_clock()-raster_start;++profile.raster_calls;}
        // Diagnostics (profile clock only): expensive BG plane rasters.
        if(prepare_clock){const double ms=prepare_clock()-raster_start;if(ms>4.0){
            const auto d=rd32(em.reg_base);std::printf("BG_RASTER eng=%u ms=%.2f path=%s rows=%d disp=%08x cnt=%04x,%04x,%04x,%04x\n",unsigned(eng),ms,
                row_ok?"row":"full",rows,unsigned(d),unsigned(rd16(em.reg_base+8)),unsigned(rd16(em.reg_base+10)),unsigned(rd16(em.reg_base+12)),unsigned(rd16(em.reg_base+14)));}}
    }else{
        ++background_hits;
        if(encode_alpha && !cache.alpha_encoded[cache.current]){
            // Same pixels, newly requested alpha code. The other buffer may
            // still be read by the presenter: only this one is written, and
            // only its alpha byte, which no colour consumer reads.
            auto& planes=cache.planes[cache.current];
            encode_front_alpha(planes.front,planes.front_layer,planes.front_priority,0,SCREEN_H);
            cache.alpha_encoded[cache.current]=true;
        }
    }
    return cache.planes[cache.current];
}
static void scanout_bg_cached(Engine eng,PpuPreparedScene& scene,bool encode_alpha=false){
    const auto& planes=background_planes_cached(eng,*background_cache[eng],scene.has_windows,scene.window_mask,false,encode_alpha);
    // The legacy path merges CPU objects into mutable planes. Its private
    // copy remains necessary; the independent BG-only consumer borrows instead.
    scene.front=planes.front;scene.back=planes.back;
    std::memcpy(scene.front_layer,planes.front_layer,sizeof scene.front_layer);
    std::memcpy(scene.front_priority,planes.front_priority,sizeof scene.front_priority);
    std::memcpy(scene.back_layer,planes.back_layer,sizeof scene.back_layer);
    std::memcpy(scene.back_priority,planes.back_priority,sizeof scene.back_priority);
    background_transfers.planes_copied+=sizeof(PpuBackgroundPlanes);
}
}
void ppu_bg_reuse_stats(uint64_t* out){out[0]=background_row_reuses;out[1]=background_dirty_rows;out[2]=background_full_rasters;out[3]=background_scroll_reuses;for(unsigned i=0;i<6;++i)out[4+i]=background_row_refusals[i];}
void ppu_layer_cache_stats(uint64_t& full,uint64_t& tiles,uint64_t& hits,uint64_t& direct){full=layer_raster_full;tiles=layer_raster_tiles;hits=layer_raster_hits;direct=layer_raster_direct;}
void ppu_background_cache_stats(uint64_t& hits,uint64_t& misses){hits=background_hits;misses=background_misses;}
PpuBackgroundTransferStats ppu_background_transfer_stats(bool reset){
    const auto result=background_transfers;if(reset)background_transfers={};return result;
}

// OBJ-only update of a prepared scene: when the cached BG planes are exactly
// those the scene was built from, only pixels inside the previous or current
// OBJ bounding box can differ. Those are restored from the BG planes and
// merged again; every other pixel already equals a full preparation.
struct ObjIncremental {
    const PpuBackgroundPlanes* planes=nullptr;uint64_t generation=0;
    int x0=0,x1=0,y0=0,y1=0;          // OBJ box of the last preparation
    int dx0=0,dx1=SCREEN_W,dy0=0,dy1=SCREEN_H; // pixels changed by the last preparation
    bool valid=false;
    // OBJ inputs of the last preparation, for per-sprite change boxes.
    uint8_t oam[1024]{},pltt[512]{};
    // Tile bytes of every drawn sprite at the last preparation (per OAM slot).
    HostVector<uint8_t> tiles;
    uint32_t tile_offset[128]{},tile_size[128]{};
    bool snapshot_valid=false;
    // Persistent OBJ layer (colour, priority|semi flag, alpha) and its key.
    Framebuffer objects;
    uint8_t priorities[SCREEN_W*SCREEN_H],object_alpha[SCREEN_W*SCREEN_H];
    uint32_t disp=0;
    bool layer_valid=false;
};
static uint64_t obj_sprite_updates;
// On-screen box of a sprite drawn by the regular OBJ scan (scan_obj rules).
static bool obj_box(const uint8_t* oam,unsigned i,int& x0,int& x1,int& y0,int& y1){
    const unsigned a0=oam[i*8]|(oam[i*8+1]<<8),a1=oam[i*8+2]|(oam[i*8+3]<<8);
    const bool affine=a0&0x100;
    if(!affine && (a0&0x200))return false;
    if(((a0>>10)&3)==2)return false;
    const int shape=(a0>>14)&3;if(shape==3)return false;
    const int size=(a1>>14)&3;
    const int w=kObjSizes[shape][size][0],h=kObjSizes[shape][size][1];
    const bool dbl=affine && (a0&0x200);
    const int bw=dbl?w*2:w,bh=dbl?h*2:h;
    int x=a1&0x1ff,y=a0&0xff;
    if(x>=256)x-=512;
    if(y>=192 && y>=256-bh)y-=256;
    x0=std::max(0,x);x1=std::min(SCREEN_W,x+bw);y0=std::max(0,y);y1=std::min(SCREEN_H,y+bh);
    return x0<x1 && y0<y1;
}
// Visits the OBJ VRAM byte ranges a drawn sprite samples (scan_obj addressing,
// before the VRAM-size wrap, which visit() applies). False for bitmap OBJ.
template<class Visit> static bool obj_tile_spans(const uint8_t* oam,unsigned i,uint32_t disp,Visit visit){
    const unsigned a0=oam[i*8]|(oam[i*8+1]<<8),a1=oam[i*8+2]|(oam[i*8+3]<<8),a2=oam[i*8+4]|(oam[i*8+5]<<8);
    if(((a0>>10)&3)==3)return false;
    const int shape=(a0>>14)&3,size=(a1>>14)&3;
    const unsigned w=kObjSizes[shape][size][0],h=kObjSizes[shape][size][1];
    const bool c256=a0&0x2000;const unsigned tile=a2&0x3ff;
    if(disp&(1u<<4)){
        const unsigned boundary=32u<<((disp>>20)&3);
        visit(tile*boundary,(w*h)/(c256?1:2));
    }else for(unsigned row=0;row<h/8;++row)visit(tile*32+row*1024,w*(c256?8:4));
    return true;
}
static uint64_t obj_incremental_updates,obj_full_updates;
static void restore_rect(PpuPreparedScene& scene,const PpuBackgroundPlanes& planes,int x0,int x1,int y0,int y1){
    if(x0>=x1 || y0>=y1)return;
    const size_t n=size_t(x1-x0);
    for(int y=y0;y<y1;++y){
        const unsigned i=y*SCREEN_W+x0;
        std::memcpy(&scene.front.px[y][x0],&planes.front.px[y][x0],n*4);
        std::memcpy(&scene.back.px[y][x0],&planes.back.px[y][x0],n*4);
        std::memcpy(scene.front_layer+i,planes.front_layer+i,n);std::memcpy(scene.front_priority+i,planes.front_priority+i,n);
        std::memcpy(scene.back_layer+i,planes.back_layer+i,n);std::memcpy(scene.back_priority+i,planes.back_priority+i,n);
    }
}
static void prepare_scene(Engine eng,PpuPreparedScene& scene,Framebuffer* unresolved_front,bool cache_backgrounds=false,bool resolve=true,bool capture_source=false,ObjIncremental* incremental=nullptr) {
    double stamp[5]{};if(prepare_clock)stamp[0]=prepare_clock();
    const auto& em=kEngines[eng];const auto disp=rd32(em.reg_base);
    const bool was_resolved=scene.resolved;
    const bool was_encoded=scene.front_alpha_encoded;
    scene.resolved=false;
    scene.front_alpha_encoded=false;
    scene.has_obj_alpha=false;
    scene.blank=(disp&0x80)||(!capture_source && ((disp>>16)&3)==0);
    scene.has_geometry=!scene.blank && eng==ENGINE_A && (disp&8) && (disp&(1u<<8));
    scene.geometry_priority=rd16(em.reg_base+8)&3;
    scene.blend=rd16(em.reg_base+0x50);scene.alpha=rd16(em.reg_base+0x52);
    scene.brightness=rd16(em.reg_base+0x54)&31;scene.master=rd16(em.reg_base+0x6c);
    scene.has_windows=build_window_masks(eng,scene.window_mask,capture_source);
    if(prepare_clock)stamp[1]=prepare_clock();
    const PpuBackgroundPlanes* reuse_planes=nullptr;
    static const bool obj_incremental=std::getenv("PPU_NO_OBJ_INCREMENTAL")==nullptr;
    if(cache_backgrounds && !scene.blank && !std::getenv("PLAYER_UNCACHED_BACKGROUNDS")){
        // minigames-2d-gpu: only a geometry engine without windows consumes
        // the front alpha code (GPU geometry selection).
        static const bool front_alpha=std::getenv("PPU_NO_FRONT_ALPHA")==nullptr;
        const bool encode=front_alpha && !scene.has_windows && scene.has_geometry;
        if(incremental && obj_incremental){
            auto& bg=*background_cache[eng];
            const auto& planes=background_planes_cached(eng,bg,scene.has_windows,scene.window_mask,false,encode);
            const bool planes_encoded=bg.alpha_encoded[bg.current];
            if(incremental->valid && !was_resolved && incremental->planes==&planes && incremental->generation==bg.generation){
                reuse_planes=&planes;scene.front_alpha_encoded=was_encoded && planes_encoded;
                if(encode && planes_encoded && !was_encoded){
                    // First request on a reused scene: encode it once in place.
                    encode_front_alpha(scene.front,scene.front_layer,scene.front_priority,0,SCREEN_H);
                    scene.front_alpha_encoded=true;
                }
            }else{
                scene.front_alpha_encoded=planes_encoded;
                scene.front=planes.front;scene.back=planes.back;
                std::memcpy(scene.front_layer,planes.front_layer,sizeof scene.front_layer);
                std::memcpy(scene.front_priority,planes.front_priority,sizeof scene.front_priority);
                std::memcpy(scene.back_layer,planes.back_layer,sizeof scene.back_layer);
                std::memcpy(scene.back_priority,planes.back_priority,sizeof scene.back_priority);
                background_transfers.planes_copied+=sizeof(PpuBackgroundPlanes);
                incremental->valid=false;
            }
            incremental->planes=&planes;incremental->generation=bg.generation;
        }else{scanout_bg_cached(eng,scene,encode);scene.front_alpha_encoded=background_cache[eng]->alpha_encoded[background_cache[eng]->current];}
    }
    else scanout_bg(eng,scene.front,nullptr,scene.front_layer,scene.front_priority,
        &scene.back,scene.back_layer,scene.back_priority,scene.has_windows?scene.window_mask:nullptr,capture_source);
    if(prepare_clock)stamp[2]=prepare_clock();
    if(scene.blank){
        if(incremental)incremental->valid=false;
        if(prepare_clock){auto& p=prepare_profiles[eng];++p.calls;p.windows+=stamp[1]-stamp[0];p.bg+=stamp[2]-stamp[1];}
        return;
    }
    // OBJ layer. With an incremental context it persists across preparations
    // and is updated per sprite: unchanged OBJ tiles/palette/DISPCNT and no
    // windows mean only the boxes of sprites whose attributes (or affine
    // matrix) changed, at old and new positions, can differ.
    static Framebuffer scratch_objects;
    static uint8_t scratch_priorities[SCREEN_W*SCREEN_H],scratch_alpha[SCREEN_W*SCREEN_H];
    Framebuffer& objects=incremental?incremental->objects:scratch_objects;
    uint8_t* priorities=incremental?incremental->priorities:scratch_priorities;
    uint8_t* object_alpha=incremental?incremental->object_alpha:scratch_alpha;
    static const bool sprite_incremental=std::getenv("PPU_NO_SPRITE_INCREMENTAL")==nullptr;
    const uint32_t oam_address=eng==ENGINE_A?0x07000000u:0x07000400u;
    const uint32_t vram_address=eng==ENGINE_A?0x06400000u:0x06600000u;
    const size_t vram_bytes=eng==ENGINE_A?0x40000u:0x20000u;
    const auto* oam_now=static_cast<const uint8_t*>(host_ptr(oam_address));
    const auto* pltt_now=static_cast<const uint8_t*>(host_ptr(em.pltt_base+0x200));
    const auto* vram_now=static_cast<const uint8_t*>(host_ptr(vram_address));
    bool layer_reusable=incremental && sprite_incremental && incremental->layer_valid && !capture_source &&
        !scene.has_windows && incremental->snapshot_valid && incremental->disp==dispcnt_key_value(eng) &&
        !std::memcmp(incremental->pltt,pltt_now,512);
    // Gather the tile bytes of the currently drawn sprites (also the snapshot).
    static HostVector<uint8_t> tiles_now;static uint32_t offset_now[128],size_now[128];
    bool tiles_ok=incremental!=nullptr;
    if(incremental){
        tiles_now.clear();
        for(unsigned i=0;i<128 && tiles_ok;++i){
            offset_now[i]=uint32_t(tiles_now.size());
            int bx0,bx1,by0,by1;
            if(obj_box(oam_now,i,bx0,bx1,by0,by1)){
                const bool regular=obj_tile_spans(oam_now,i,disp,[&](uint32_t at,uint32_t bytes){
                    at%=uint32_t(vram_bytes);
                    const uint32_t first=std::min<uint32_t>(bytes,uint32_t(vram_bytes)-at);
                    tiles_now.insert(tiles_now.end(),vram_now+at,vram_now+at+first);
                    if(bytes>first)tiles_now.insert(tiles_now.end(),vram_now,vram_now+(bytes-first));
                });
                if(!regular)tiles_ok=false; // bitmap OBJ: no per-sprite key
            }
            size_now[i]=uint32_t(tiles_now.size())-offset_now[i];
        }
    }
    layer_reusable=layer_reusable && tiles_ok;
    const double obj_t1=prepare_clock?prepare_clock():0; // diagnostics (PPU_OBJ_DETAIL)
    double obj_t2=0;
    ObjClip clip{};
    // minigames-30fps-min: the changed sprites' boxes, clustered, instead of
    // their single bounding box (moving sprites at both screen ends made the
    // union box cover most of the screen: Shell Smash ~44K pixels/frame).
    // Each cluster is rasterized completely (priorities reset, every sprite
    // over it drawn), then restored + merged cluster by cluster.
    static ObjClip boxes[256];unsigned box_count=0;
    static const bool clustered=std::getenv("PPU_NO_OBJ_BOX_LIST")==nullptr;
    int x0=SCREEN_W,x1=0,y0=SCREEN_H,y1=0; // layer coverage (all current sprite boxes)
    auto put=[&](int x,int y,uint32_t color,unsigned priority,unsigned obj_alpha){
        if(scene.has_windows && !(scene.window_mask[y*SCREEN_W+x]&16))return;
        const unsigned i=y*SCREEN_W+x;
        if(priority<=(priorities[i]&0x7f)){objects.px[y][x]=color;priorities[i]=priority|(obj_alpha?0x80:0);object_alpha[i]=obj_alpha;}
    };
    if(layer_reusable){
        bool group_changed[32];
        for(unsigned g=0;g<32;++g){
            group_changed[g]=false;
            for(unsigned k=0;k<4;++k)if(std::memcmp(incremental->oam+(g*4+k)*8+6,oam_now+(g*4+k)*8+6,2))group_changed[g]=true;
        }
        int rx0=SCREEN_W,rx1=0,ry0=SCREEN_H,ry1=0;
        auto add=[&](const uint8_t* table,unsigned i){
            int bx0,bx1,by0,by1;
            if(!obj_box(table,i,bx0,bx1,by0,by1))return;
            rx0=std::min(rx0,bx0);rx1=std::max(rx1,bx1);ry0=std::min(ry0,by0);ry1=std::max(ry1,by1);
            bx0=std::max(bx0,0);bx1=std::min(bx1,SCREEN_W);by0=std::max(by0,0);by1=std::min(by1,SCREEN_H);
            if(bx0<bx1 && by0<by1 && box_count<256)boxes[box_count++]={bx0,bx1,by0,by1};
        };
        for(unsigned i=0;i<128;++i){
            const unsigned old_a0=incremental->oam[i*8]|(incremental->oam[i*8+1]<<8),new_a0=oam_now[i*8]|(oam_now[i*8+1]<<8);
            const unsigned old_g=(incremental->oam[i*8+3]>>1)&31,new_g=(oam_now[i*8+3]>>1)&31;
            const bool changed=std::memcmp(incremental->oam+i*8,oam_now+i*8,6)!=0 ||
                ((old_a0&0x100) && group_changed[old_g]) || ((new_a0&0x100) && group_changed[new_g]) ||
                incremental->tile_size[i]!=size_now[i] ||
                std::memcmp(incremental->tiles.data()+incremental->tile_offset[i],tiles_now.data()+offset_now[i],size_now[i])!=0;
            if(changed){add(incremental->oam,i);add(oam_now,i);}
        }
        if(rx0>=rx1 || ry0>=ry1){rx0=rx1=ry0=ry1=0;}
        clip={rx0,rx1,ry0,ry1};
        if(clip.x0<clip.x1 && clustered && box_count<256){
            // Greedy clustering: merge two boxes whenever their union costs
            // no more than both apart plus one 32x32 sprite.
            auto area=[](const ObjClip& b){return (b.x1-b.x0)*(b.y1-b.y0);};
            for(bool merged=true;merged;){
                merged=false;
                for(unsigned a=0;a<box_count && !merged;++a)for(unsigned b=a+1;b<box_count;++b){
                    const ObjClip u{std::min(boxes[a].x0,boxes[b].x0),std::max(boxes[a].x1,boxes[b].x1),
                                    std::min(boxes[a].y0,boxes[b].y0),std::max(boxes[a].y1,boxes[b].y1)};
                    if(area(u)<=area(boxes[a])+area(boxes[b])+1024){boxes[a]=u;boxes[b]=boxes[--box_count];merged=true;break;}
                }
            }
        }else box_count=0;
        if(!box_count && clip.x0<clip.x1)boxes[box_count++]=clip;
        for(unsigned k=0;k<box_count;++k){
            const auto& b=boxes[k];
            for(int y=b.y0;y<b.y1;++y)std::memset(priorities+y*SCREEN_W+b.x0,0xff,b.x1-b.x0);
        }
        if(prepare_clock)obj_t2=prepare_clock();
        for(unsigned k=0;k<box_count;++k)scan_obj(eng,put,false,capture_source,NoObjBox{},boxes[k]);
        for(unsigned i=0;i<128;++i){int bx0,bx1,by0,by1;if(obj_box(oam_now,i,bx0,bx1,by0,by1)){
            x0=std::min(x0,bx0);x1=std::max(x1,bx1);y0=std::min(y0,by0);y1=std::max(y1,by1);}}
        ++obj_sprite_updates;
    }else{
        std::memset(priorities,0xff,SCREEN_W*SCREEN_H);
        scan_obj(eng,put,false,capture_source,[&](int bx0,int bx1,int by0,int by1){
            // Conservative on-screen sprite box: covers every pixel it can put.
            if(bx0>=bx1 || by0>=by1)return;
            x0=std::min(x0,bx0);x1=std::max(x1,bx1);y0=std::min(y0,by0);y1=std::max(y1,by1);
        });
        clip={0,SCREEN_W,0,SCREEN_H};
    }
    static const bool bounded=std::getenv("PPU_REFERENCE_OBJ")==nullptr;
    if(!bounded){x0=0;x1=SCREEN_W;y0=0;y1=SCREEN_H;}
    if(x0>=x1 || y0>=y1){x0=x1=y0=y1=0;}
    const double obj_t3=prepare_clock?prepare_clock():0;
    // Region of the scene to recompose from the BG planes and the layer.
    int mx0=x0,mx1=x1,my0=y0,my1=y1;
    bool scan_semi=false,boxed=false;
    if(reuse_planes && layer_reusable){
        boxed=true; // restore + merge per cluster below
        mx0=std::max(x0,clip.x0);mx1=std::min(x1,clip.x1);my0=std::max(y0,clip.y0);my1=std::min(y1,clip.y1);
        incremental->dx0=clip.x0;incremental->dx1=clip.x1;incremental->dy0=clip.y0;incremental->dy1=clip.y1;
        scan_semi=true;
    }else if(reuse_planes){
        // Restore the union of the previous and current OBJ boxes.
        const int ux0=std::min(incremental->x1>incremental->x0?incremental->x0:SCREEN_W,x1>x0?x0:SCREEN_W);
        const int ux1=std::max(incremental->x1>incremental->x0?incremental->x1:0,x1>x0?x1:0);
        const int uy0=std::min(incremental->y1>incremental->y0?incremental->y0:SCREEN_H,y1>y0?y0:SCREEN_H);
        const int uy1=std::max(incremental->y1>incremental->y0?incremental->y1:0,y1>y0?y1:0);
        restore_rect(scene,*reuse_planes,ux0,ux1,uy0,uy1);
        incremental->dx0=ux0;incremental->dx1=ux1;incremental->dy0=uy0;incremental->dy1=uy1;
        ++obj_incremental_updates;
    }else if(incremental){
        incremental->dx0=0;incremental->dx1=SCREEN_W;incremental->dy0=0;incremental->dy1=SCREEN_H;
        ++obj_full_updates;
    }
    auto merge_objects=[&](int mx0,int mx1,int my0,int my1){
    if(mx0>=mx1 || my0>=my1)return;
    for(int y=my0;y<my1;++y)for(int x=mx0;x<mx1;++x){
        // Pixels without OBJ keep priority 0xff (no-op below): skip them by words.
        if(!(x&3) && x+4<=mx1){
            uint32_t word;std::memcpy(&word,priorities+y*SCREEN_W+x,4);
            if(word==0xffffffffu){x+=3;continue;}
        }
        const unsigned i=y*SCREEN_W+x,p=priorities[i]&0x7f;
        if(p<=scene.front_priority[i]){
            scene.back.px[y][x]=scene.front.px[y][x];
            scene.back_layer[i]=scene.front_layer[i];scene.back_priority[i]=scene.front_priority[i];
            scene.front.px[y][x]=(objects.px[y][x]&0xffffffu)|(ppu_front_alpha_code(4,p)<<24);
            scene.front_layer[i]=4|(priorities[i]&0x80);scene.front_priority[i]=p;
            scene.front_obj_alpha[i]=object_alpha[i];
            scene.has_obj_alpha|=(priorities[i]&0x80)!=0;
        }else if(p<=scene.back_priority[i]){
            scene.back.px[y][x]=objects.px[y][x];scene.back_layer[i]=4;scene.back_priority[i]=p;
        }
    }
    };
    if(boxed){
        // Sequential: a later overlapping cluster restores the BG planes over
        // the overlap before merging again (a merge is not idempotent).
        for(unsigned k=0;k<box_count;++k){const auto& b=boxes[k];restore_rect(scene,*reuse_planes,b.x0,b.x1,b.y0,b.y1);
            merge_objects(std::max(x0,b.x0),std::min(x1,b.x1),std::max(y0,b.y0),std::min(y1,b.y1));}
    }else merge_objects(mx0,mx1,my0,my1);
    if(scan_semi && !scene.has_obj_alpha){
        // Semi-transparent OBJ pixels outside the recomposed box were kept.
        for(unsigned k=0;k<SCREEN_W*SCREEN_H;++k)if(scene.front_layer[k]&0x80){scene.has_obj_alpha=true;break;}
    }
    if(incremental){
        incremental->x0=x0;incremental->x1=x1;incremental->y0=y0;incremental->y1=y1;
        incremental->valid=true;
        incremental->layer_valid=!capture_source && !scene.has_windows;incremental->disp=dispcnt_key_value(eng);
        std::memcpy(incremental->oam,oam_now,1024);std::memcpy(incremental->pltt,pltt_now,512);
        try{incremental->tiles=tiles_now;std::memcpy(incremental->tile_offset,offset_now,sizeof offset_now);
            std::memcpy(incremental->tile_size,size_now,sizeof size_now);incremental->snapshot_valid=tiles_ok;}
        catch(const std::bad_alloc&){incremental->snapshot_valid=false;incremental->layer_valid=false;}
    }
    if(prepare_clock)stamp[3]=prepare_clock();
    {
        static const bool detail=std::getenv("PPU_OBJ_DETAIL")!=nullptr;static unsigned printed=0;
        if(detail && prepare_clock && stamp[3]-stamp[2]>8 && printed<60){++printed;
            unsigned sprites=0;for(unsigned i=0;i<128;++i){int a,b,c,d;sprites+=obj_box(oam_now,i,a,b,c,d);}
            long area=0;for(unsigned k=0;k<box_count;++k)area+=long(boxes[k].x1-boxes[k].x0)*(boxes[k].y1-boxes[k].y0);
            std::printf("PPU_OBJ_DETAIL eng=%u reusable=%u sprites=%u boxes=%u boxArea=%ld clip=%dx%d tiles=%u gather=%.2f scan=%.2f(%.2f) merge=%.2f total=%.2f\n",
                unsigned(eng),unsigned(layer_reusable),sprites,box_count,area,clip.x1-clip.x0,clip.y1-clip.y0,unsigned(tiles_now.size()),
                obj_t1-stamp[2],obj_t3-obj_t1,obj_t2?obj_t3-obj_t2:0.0,stamp[3]-obj_t3,stamp[3]-stamp[2]);
        }
        // minigames-30fps-min diagnostics: OBJ incremental coverage.
        static const bool trace=std::getenv("PPU_OBJ_TRACE")!=nullptr;
        if(trace){
            static unsigned n[2]{},reusable[2]{};static double clip_area[2]{},merge_area[2]{},ms[2]{};
            ++n[eng];reusable[eng]+=layer_reusable;clip_area[eng]+=double(clip.x1-clip.x0)*(clip.y1-clip.y0);
            merge_area[eng]+=double(mx1-mx0)*(my1-my0);if(prepare_clock)ms[eng]+=stamp[3]-stamp[2];
            if(n[eng]==120){std::printf("PPU_OBJ eng=%u reusable=%u clipArea=%.0f mergeArea=%.0f objMs=%.3f tiles=%u\n",unsigned(eng),reusable[eng],
                clip_area[eng]/120,merge_area[eng]/120,ms[eng]/120,unsigned(tiles_now.size()));n[eng]=reusable[eng]=0;clip_area[eng]=merge_area[eng]=ms[eng]=0;}
        }
    }
    // Engines without GX can resolve effects once at preparation time.
    // Per-pixel in-place output is safe: composition only reads that pixel.
    if(!scene.has_geometry && resolve){
        if(unresolved_front)*unresolved_front=scene.front;
        ppu_compose_prepared(scene,scene.front,scene.front);
        scene.resolved=true;
    }
    if(prepare_clock){
        stamp[4]=prepare_clock();auto& p=prepare_profiles[eng];++p.calls;
        p.windows+=stamp[1]-stamp[0];p.bg+=stamp[2]-stamp[1];
        p.objects+=stamp[3]-stamp[2];p.resolve+=stamp[4]-stamp[3];
    }
}
void ppu_prepare_scene(Engine eng,PpuPreparedScene& scene){sync_vram_views();prepare_scene(eng,scene,nullptr);}

namespace {
struct PreparedSpan {uint32_t address;size_t size;};
struct PreparedCache {
    PpuPreparedScene scene;
    Framebuffer unresolved_front;
    HostVector<uint8_t> snapshot;
    HostVector<PreparedSpan> spans;
    bool valid=false;
    ObjIncremental incremental;
    // CPU-resolved composition (see ppu_prepare_candidates_cached), kept apart
    // from the raw planes so OBJ-only updates can resolve their box only.
    Framebuffer resolved_front;
    uint64_t resolved_generation=0,dirty_base=0;
    int rx0=0,rx1=SCREEN_W,ry0=0,ry1=SCREEN_H;
    uint16_t resolved_regs[4]{};
    bool resolved_valid=false;
    uint64_t plane_generation=0;
    bool plane_overflow=false;
    void touch(){
        // minigames-30fps-min: one counter for every cache way, so a
        // generation never names planes of two different ways.
        static uint64_t counter=0;
        if(plane_overflow)return;
        if(counter==UINT64_MAX){plane_overflow=true;return;}
        plane_generation=++counter;
    }
};
static PreparedCache prepared_cache_storage[2];
static PreparedCache* prepared_cache[2]={&prepared_cache_storage[0],&prepared_cache_storage[1]};
static uint64_t prepared_hits,prepared_misses;
}
static const PpuPreparedScene& prepare_scene_cached_impl(Engine eng,bool resolve);
static const PpuPreparedScene& prepare_scene_cached(Engine eng,bool resolve){
    const double start=prepare_clock?prepare_clock():0;
    const auto& scene=prepare_scene_cached_impl(eng,resolve);
    if(prepare_clock)prepare_profiles[eng].cached+=prepare_clock()-start;
    return scene;
}
// Every address the supported BG/OBJ samplers of one engine can read (merged).
static void collect_prepared_spans(Engine eng,HostVector<PreparedSpan>& storage){
    const auto& em=kEngines[eng];
    // Cover all addresses the supported BG/OBJ samplers can read, including
    // character/map base offsets, tile indices and extended palette banks.
    // Conservative spans intentionally invalidate on unused writes as well.
    storage.clear();
    auto add=[&](uint32_t address,size_t size){storage.push_back({address,size});};
    // BLDCNT/BLDALPHA/BLDY and MASTER_BRIGHT affect composition only. Keep
    // geometry-independent candidates when a fade changes these registers.
    // DISPSTAT/VCOUNT are timing state, never sampled by the PPU.
    // minigames-30fps-min: binary windows are not sampled by the planes
    // (WIN0H..WINOUT 0x40-0x4b and the per-line shadow are left out).
    const bool cut=g_window_cut_scope && windows_binary(eng);
    add(dispcnt_key_address(eng),4);
    if(cut){add(em.reg_base+8,0x38);add(em.reg_base+0x4c,4);}else add(em.reg_base+8,0x48);
    add(em.reg_base+0x56,0x16);add(em.reg_base+0x6e,2);
    if(!cut && (rd32(em.reg_base)&0x6000))add(em.reg_base+SCANLINE_WINDOW_SHADOW,SCANLINE_WINDOW_BYTES); // per-line windows
    {const auto v=vramcnt_span(eng);add(v.address,v.size);}add(em.pltt_base,0x400);
    const auto disp=rd32(em.reg_base);
    auto extended_palette=[&](unsigned slot){
        const unsigned offset=slot*8192;
        if(!em.has_base_offset){
            if((rd16(0x04000248)&0x83)==0x82)add(0x06898000+offset,8192);
            return;
        }
        if((rd16(0x04000244)&0x87)==0x84)add(0x06880000+offset,8192);
        for(unsigned bank=0;bank<2;++bank){
            const unsigned cnt=*static_cast<volatile uint8_t*>(host_ptr(0x04000245+bank));
            if((cnt&0x87)==0x84 && (offset>>14)==((cnt>>3)&1))
                add(0x06890000+bank*0x4000+(offset&0x3fff),8192);
        }
    };
    for(int bg=0;bg<4;++bg){
        const auto config=read_bg(em,bg,disp);if(!config.enabled)continue;
        if(config.extended_bitmap)add(config.bitmap_base,config.bitmap_mask+1);
        else {add(config.screen,config.tiles_w*config.tiles_h*2);add(config.chars,config.bpp8?65536:32768);}
        if(config.bpp8 && config.ext_palette)extended_palette(config.palette_slot);
    }
    if(disp&(1u<<12)){
        const unsigned oam=eng==ENGINE_A?0x07000000u:0x07000400u;
        const unsigned vram=eng==ENGINE_A?0x06400000u:0x06600000u;
        const unsigned obj_bytes=eng==ENGINE_A?0x40000u:0x20000u;
        auto add_obj=[&](unsigned offset,unsigned bytes){
            offset%=obj_bytes;
            const unsigned first=std::min(bytes,obj_bytes-offset);
            add(vram+offset,first);if(bytes>first)add(vram,bytes-first);
        };
        add(oam,0x400); // Attributes/matrices invalidate before any tile reuse.
        if(disp&0x80000000u){ // 256-colour OBJ extended palettes (obj_ext_color)
            if(eng==ENGINE_B){if((*static_cast<volatile uint8_t*>(host_ptr(0x04000249))&0x87)==0x83)add(0x068a0000,8192);}
            else for(unsigned bank=0;bank<2;++bank)
                if((*static_cast<volatile uint8_t*>(host_ptr(0x04000245+bank))&0x87)==0x85)add(0x06890000+bank*0x4000,8192);
        }
        const bool mapping1d=disp&(1u<<4);
        const unsigned boundary=32u<<((disp>>20)&3);
        for(unsigned i=0;i<128;++i){
            const unsigned a0=rd16(oam+i*8),a1=rd16(oam+i*8+2),a2=rd16(oam+i*8+4);
            const bool affine=a0&0x100;
            if(!affine && (a0&0x200))continue;
            const unsigned mode=(a0>>10)&3,shape=a0>>14;
            if(shape==3 || (mode==2 && !(disp&(1u<<15))))continue;
            const unsigned size=a1>>14;
            const int w=kObjSizes[shape][size][0],h=kObjSizes[shape][size][1];
            const int bw=affine && (a0&0x200)?w*2:w,bh=affine && (a0&0x200)?h*2:h;
            int x=a1&511,y=a0&255;if(x>=256)x-=512;
            if(y>=192 && y>=256-bh)y-=256;
            if(x>=SCREEN_W || x+bw<=0 || y>=SCREEN_H || y+bh<=0)continue;
            if(mode==3){if(a2>>12)add(vram,eng==ENGINE_A?0x40000:0x20000);continue;}
            const bool bpp8=a0&0x2000;const unsigned tile=a2&1023;
            if(mapping1d)add_obj(tile*boundary,(w*h)/(bpp8?1:2));
            else for(int row=0;row<h/8;++row)
                add_obj(tile*32+row*1024,w*(bpp8?8:4));
        }
    }
    // Merge overlapping character/map spans so shared tiles are compared once.
    std::sort(storage.begin(),storage.end(),[](const PreparedSpan& a,const PreparedSpan& b){return a.address<b.address;});
    unsigned merged=0;
    for(unsigned i=0;i<storage.size();++i){
        if(merged && storage[i].address<=storage[merged-1].address+storage[merged-1].size){
            auto& previous=storage[merged-1];
            previous.size=std::max(previous.size,size_t(storage[i].address-previous.address)+storage[i].size);
        }else storage[merged++]=storage[i];
    }
    // Address/size metadata is itself part of the key: equal byte lengths alone
    // must not reuse a snapshot after a BG changes its base or tile format.
    storage.resize(merged);
}
// minigames-30fps-min: DS dual-screen 3D (Trampoline Time/Terror) switches
// engine A between two display configurations (DISPCNT) on alternate frames,
// so every preparation missed and re-rasterized all BG/OBJ planes. A second
// cache way (prepared scene + BG planes + OBJ incremental state), allocated
// on host memory only once such a fast alternation is seen, keeps one way per
// DISPCNT value. Each way still validates its exact input snapshot.
// PPU_NO_CACHE_WAYS=1 keeps the single way.
struct CacheWays {
    PreparedCache* prepared[2]{};BgCandidateCache* bg[2]{};
    uint32_t key[2]={~0u,~0u};unsigned active=0;uint64_t calls=0,last_change=0;bool failed=false;
    // Way whose planes an in-flight presentation job reads (-1: none).
    int pinned=-1;
};
static CacheWays cache_ways[2];
static bool allocate_cache_way(Engine eng,unsigned way){
    auto& w=cache_ways[eng];
    if(w.prepared[way])return true;
    if(w.failed)return false;
    auto* prepared=static_cast<PreparedCache*>(std::malloc(sizeof(PreparedCache)));
    auto* bg=static_cast<BgCandidateCache*>(std::malloc(sizeof(BgCandidateCache)));
    if(!prepared || !bg){std::free(prepared);std::free(bg);w.failed=true;return false;}
    w.prepared[way]=new(prepared) PreparedCache();w.bg[way]=new(bg) BgCandidateCache();
    std::printf("PPU cache way 2 allocated eng=%u bytes=%u\n",unsigned(eng),unsigned(sizeof(PreparedCache)+sizeof(BgCandidateCache)));
    return true;
}
static unsigned active_way_index(Engine eng){return prepared_cache[eng]==cache_ways[eng].prepared[1] && cache_ways[eng].prepared[1]?1u:0u;}
static bool way_inputs_unchanged(Engine eng);
static void select_cache_way(Engine eng){
    static const bool enabled=std::getenv("PPU_NO_CACHE_WAYS")==nullptr;
    if(!enabled)return;
    auto& w=cache_ways[eng];
    if(!w.prepared[0]){w.prepared[0]=&prepared_cache_storage[eng];w.bg[0]=&background_cache_storage[eng];}
    ++w.calls;
    const uint32_t key=rd32(kEngines[eng].reg_base)&~0xe000u; // window enables do not select a way
    if(key!=w.key[w.active]){
        const unsigned other=w.active^1;
        if(w.prepared[other]){
            // Both ways exist: the other one is the least recently used.
            if(w.key[other]!=key)w.key[other]=key;
            w.active=other;
        }else if(w.key[w.active]!=~0u && w.calls-w.last_change<=4 && allocate_cache_way(eng,other)){
            w.key[other]=key;w.active=other;
        }else w.key[w.active]=key;
        w.last_change=w.calls;
    }
    // A job still reads the active way: reuse it only read-only (unchanged
    // inputs: a pure cache hit), otherwise prepare into the other way.
    if(w.pinned==int(w.active)){
        prepared_cache[eng]=w.prepared[w.active];background_cache[eng]=w.bg[w.active];
        if(!way_inputs_unchanged(eng)){
            const unsigned other=w.active^1;
            if(allocate_cache_way(eng,other)){w.active=other;w.key[other]=key;}
        }
    }
    prepared_cache[eng]=w.prepared[w.active];background_cache[eng]=w.bg[w.active];
}
void ppu_pin_candidate_ways(bool pinned){
    for(unsigned e=0;e<2;++e)cache_ways[e].pinned=pinned && cache_ways[e].prepared[0]?int(cache_ways[e].active):-1;
}
bool ppu_unpinned_way_available(Engine eng){
    auto& w=cache_ways[eng];
    static const bool enabled=std::getenv("PPU_NO_CACHE_WAYS")==nullptr;
    if(!enabled || !w.prepared[0])return false;
    return w.pinned<0 || allocate_cache_way(eng,unsigned(w.pinned)^1u);
}
static const PpuPreparedScene& prepare_scene_cached_impl(Engine eng,bool resolve) {
    sync_vram_views();
    select_cache_way(eng);
    auto& cache=(*prepared_cache[eng]);const auto& em=kEngines[eng];
    static HostVector<PreparedSpan> storage;collect_prepared_spans(eng,storage);
    const unsigned merged=storage.size();
    size_t size=0;for(const auto& span:storage){size=cache_snapshot_offset(size);size+=span.size;}
    if(cache.snapshot.size()!=size){cache.snapshot.resize(size);cache.valid=false;}
    bool same=cache.valid && cache.spans.size()==merged;size_t offset=0;
    for(unsigned i=0;same && i<merged;++i)same=cache.spans[i].address==storage[i].address && cache.spans[i].size==storage[i].size;
    if(same)for(const auto& span:storage){
        offset=cache_snapshot_offset(offset);
        if(std::memcmp(cache.snapshot.data()+offset,host_ptr(span.address),span.size)!=0){same=false;break;}
        offset+=span.size;
    }
    if(same){
        ++prepared_hits;
        auto& scene=cache.scene;
        const uint16_t blend=rd16(em.reg_base+0x50),alpha=rd16(em.reg_base+0x52);
        const uint16_t bright=rd16(em.reg_base+0x54)&31,master=rd16(em.reg_base+0x6c);
        if(scene.blend!=blend || scene.alpha!=alpha || scene.brightness!=bright || scene.master!=master){
            scene.blend=blend;scene.alpha=alpha;scene.brightness=bright;scene.master=master;
            if(scene.resolved && !scene.has_geometry && !scene.blank){
                // Engine B stores an already resolved front plane. Restore its
                // original colours so repeated fades never compound rounding.
                cache.touch();scene.front=cache.unresolved_front;scene.resolved=false;
            }
        }
        if(resolve && !scene.resolved && !scene.has_geometry && !scene.blank){
            cache.touch();cache.unresolved_front=scene.front;
            ppu_compose_prepared(scene,scene.front,scene.front);scene.resolved=true;
        }
        return scene;
    }
    cache.dirty_base=cache.plane_generation;
    cache.valid=false;cache.touch();
    ++prepared_misses;offset=0;cache.spans=storage;
    for(const auto& span:storage){offset=cache_snapshot_offset(offset);std::memcpy(cache.snapshot.data()+offset,host_ptr(span.address),span.size);offset+=span.size;}
    prepare_scene(eng,cache.scene,&cache.unresolved_front,true,resolve,false,&cache.incremental);cache.valid=true;
    cache.rx0=cache.incremental.dx0;cache.rx1=cache.incremental.dx1;cache.ry0=cache.incremental.dy0;cache.ry1=cache.incremental.dy1;
    if(!cache.incremental.valid){cache.rx0=0;cache.rx1=SCREEN_W;cache.ry0=0;cache.ry1=SCREEN_H;}
    {
        // minigames-30fps-min diagnostics: PPU_VERIFY_PREPARED=1 re-prepares
        // every miss from scratch (no BG/OBJ caches) and counts differing
        // plane pixels (front/back colour RGB, layers, priorities).
        static const bool verify=std::getenv("PPU_VERIFY_PREPARED")!=nullptr;
        if(verify && !cache.scene.resolved){
            static PpuPreparedScene* reference=nullptr;
            if(!reference){void* m=std::malloc(sizeof(PpuPreparedScene));if(m)reference=new(m) PpuPreparedScene();}
            if(reference){
                prepare_scene(eng,*reference,nullptr,false,false,false,nullptr);
                const auto& s=cache.scene;unsigned bad=0,first=~0u;
                for(unsigned i=0;i<SCREEN_W*SCREEN_H;++i){
                    const unsigned y=i/SCREEN_W,x=i%SCREEN_W;
                    const bool fl=s.front_layer[i]!=reference->front_layer[i] || s.front_priority[i]!=reference->front_priority[i];
                    const bool fc=((s.front.px[y][x]^reference->front.px[y][x])&0xffffffu)!=0;
                    const bool semi=(s.front_layer[i]&0x80)!=0;
                    const bool bc=semi && (((s.back.px[y][x]^reference->back.px[y][x])&0xffffffu) || s.back_layer[i]!=reference->back_layer[i]);
                    if(fl||fc||bc){++bad;if(first==~0u)first=i;}
                }
                static unsigned long long checks[2]{},bad_checks[2]{};++checks[eng];bad_checks[eng]+=bad!=0;
                if(bad || checks[eng]%600==0)std::printf("PPU_VERIFY eng=%u checks=%llu bad=%llu pixels=%u first=%u,%u\n",unsigned(eng),checks[eng],bad_checks[eng],bad,first%SCREEN_W,first/SCREEN_W);
            }
        }
    }
    return cache.scene;
}
// Read-only: would ppu_prepare_candidates_cached(eng) return the scene it
// returned last time? Compares the exact input snapshot without touching the
// cache, so it may run while the presentation worker reads the prepared planes.
bool ppu_prepared_inputs_unchanged(Engine eng){
    WindowCutScope scope; // predicts ppu_prepare_candidates_cached (minigames-30fps-min)
    if(window_cut_signature(eng)!=g_window_cut_issued[eng])return false;
    return way_inputs_unchanged(eng);
}
// The active cache way's exact snapshot still equals DS memory and the
// composition registers (a preparation now would be a read-only hit).
static bool way_inputs_unchanged(Engine eng){
    const auto& cache=(*prepared_cache[eng]);const auto& em=kEngines[eng];
    if(!cache.valid || io_vram_banks())return false;
    static HostVector<PreparedSpan> storage[2];auto& spans=storage[eng];
    collect_prepared_spans(eng,spans);
    if(cache.spans.size()!=spans.size())return false;
    size_t offset=0;
    for(unsigned i=0;i<spans.size();++i){
        if(cache.spans[i].address!=spans[i].address || cache.spans[i].size!=spans[i].size)return false;
        offset=cache_snapshot_offset(offset);
        if(offset+spans[i].size>cache.snapshot.size() ||
           std::memcmp(cache.snapshot.data()+offset,host_ptr(spans[i].address),spans[i].size)!=0)return false;
        offset+=spans[i].size;
    }
    const auto& scene=cache.scene;
    return scene.blend==rd16(em.reg_base+0x50) && scene.alpha==rd16(em.reg_base+0x52) &&
        scene.brightness==(rd16(em.reg_base+0x54)&31) && scene.master==rd16(em.reg_base+0x6c);
}
CandidatePlaneVersion ppu_candidate_plane_version(const PpuPreparedCandidates& view){
    // minigames-30fps-min: the view may belong to either cache way (a job
    // reads a pinned way while the next frame is prepared in the other).
    for(unsigned slot=0;slot<4;++slot){
        const unsigned engine=slot&1;
        const PreparedCache* way=slot<2?prepared_cache[engine]:
            (cache_ways[engine].prepared[0]==prepared_cache[engine]?cache_ways[engine].prepared[1]:cache_ways[engine].prepared[0]);
        if(!way)continue;
        const auto& cache=*way;const auto& scene=cache.scene;
        if(!cache.valid || cache.plane_overflow || !cache.plane_generation)continue;
        // Preserve the earlier admission boundary until A extended OBJ overlap
        // has its DS reference. Such inputs still use exact byte checks.
        if(engine==ENGINE_A && (rd32(kEngines[engine].reg_base)&0x80001000u)==0x80001000u)continue;
        const auto* raw=scene.resolved?&cache.unresolved_front:&scene.front;
        // A CPU-resolved view (see ppu_prepare_candidates_cached) is versioned
        // by the same generation, under its own identity.
        const bool resolved_view=cache.resolved_valid && cache.resolved_generation==cache.plane_generation && &view.front==&cache.resolved_front;
        if((&view.front==raw || resolved_view) && &view.back==&scene.back &&
           view.front_layer()==scene.front_layer && view.front_priority()==scene.front_priority &&
           view.back_layer()==scene.back_layer && view.back_priority()==scene.back_priority &&
           view.window_mask()==scene.window_mask && view.front_obj_alpha()==scene.front_obj_alpha)
            return {resolved_view?static_cast<const void*>(&cache.resolved_front):static_cast<const void*>(&cache),cache.plane_generation};
    }
    return {};
}
void ppu_prepared_cache_stats(uint64_t& hits,uint64_t& misses){hits=prepared_hits;misses=prepared_misses;}
const PpuPreparedScene& ppu_prepare_scene_cached(Engine eng){return prepare_scene_cached(eng,true);}

// Effects the resident GPU composition cannot reproduce exactly (BLDCNT
// mode1 alpha between two targets, semi-transparent OBJ). Without geometry the
// whole engine output is independent of the GPU, so the exact CPU composition
// (the same compose_candidates the synchronous fallback runs) is resolved once
// per cache generation and handed over as an effect-free opaque front plane.
static uint64_t semi_preblends;
static void compose_candidates(PpuPreparedCandidates candidates,const Framebuffer& geometry,Framebuffer& output,bool master,
                               int rx0=0,int rx1=SCREEN_W,int ry0=0,int ry1=SCREEN_H);
static bool cpu_effects_required(const PpuPreparedScene& s){
    if(s.blank || s.has_geometry)return false;
    const unsigned mode=(s.blend>>6)&3;
    return s.has_obj_alpha || (mode==1 && (s.blend&0x3f) && (s.blend&0x3f00));
}
static PpuPreparedCandidates prepare_candidates_cached_impl(Engine eng);
// minigames-30fps-min: candidate views carry the frame's binary window cut.
PpuPreparedCandidates ppu_prepare_candidates_cached(Engine eng) {
    WindowCutScope scope;
    auto view=prepare_candidates_cached_impl(eng);
    view.state.window_cut=window_cut_for(eng);
    return view;
}
static PpuPreparedCandidates prepare_candidates_cached_impl(Engine eng) {
    const auto& scene=prepare_scene_cached(eng,false);
    static const bool cpu_resolve=std::getenv("PPU_NO_CPU_RESOLVE")==nullptr;
    if(cpu_resolve && cpu_effects_required(scene) && !scene.resolved){
        auto& cache=(*prepared_cache[eng]);
        const uint16_t regs[4]={scene.blend,scene.alpha,scene.brightness,scene.master};
        const bool regs_same=cache.resolved_valid && !std::memcmp(regs,cache.resolved_regs,sizeof regs);
        if(!regs_same || cache.resolved_generation!=cache.plane_generation){
            const double start=prepare_clock?prepare_clock():0;
            const bool partial=regs_same && cache.resolved_generation==cache.dirty_base;
            // Composition reads only its own pixel: a box update is exact
            // when every pixel outside it kept planes and registers.
            if(partial)compose_candidates({scene,scene.front},scene.front,cache.resolved_front,true,cache.rx0,cache.rx1,cache.ry0,cache.ry1);
            else compose_candidates({scene,scene.front},scene.front,cache.resolved_front,true);
            cache.touch();cache.dirty_base=0;
            cache.resolved_generation=cache.plane_generation;std::memcpy(cache.resolved_regs,regs,sizeof regs);cache.resolved_valid=true;
            if(prepare_clock)prepare_profiles[eng].resolve+=prepare_clock()-start;
        }
        PpuPreparedCandidates view{scene,cache.resolved_front};
        view.state.blend=0;view.state.alpha=0;view.state.brightness=0;view.state.master=0;
        view.state.has_obj_alpha=false;view.state.has_windows=false;view.state.resolved=true;
        return view;
    }
    {
        static const bool obj_first=std::getenv("PPU_NO_GPU_OBJ_FIRST_BLEND")==nullptr;
        const unsigned mode=(scene.blend>>6)&3;
        if(obj_first && cpu_resolve && scene.has_geometry && !scene.blank && !scene.has_windows && mode==1 &&
           (scene.blend&0x3f)==0x10 && (scene.blend&0x100)){
            bool own_alpha=false;
            if(scene.has_obj_alpha)for(unsigned i=0;i<SCREEN_W*SCREEN_H && !own_alpha;++i)
                own_alpha=(scene.front_layer[i]&0x80) && scene.front_obj_alpha[i]!=17;
            if(!own_alpha){
                PpuPreparedCandidates view{scene,scene.front};
                view.state.obj_first_blend=true;
                return view;
            }
        }
        // minigames-30fps-min: BG (or BG+OBJ) first targets over 3D, drawn by
        // the resident GPU composition (state.bg_first_blend) instead of the
        // CPU readback path. PPU_NO_GPU_BG_FIRST_BLEND=1 refuses.
        static const bool bg_first=std::getenv("PPU_NO_GPU_BG_FIRST_BLEND")==nullptr;
        const unsigned first=scene.blend&0x3e,second=(scene.blend>>8)&0x3e;
        if(bg_first && cpu_resolve && scene.has_geometry && !scene.blank && mode==1 && first && !(first&second)){
            bool own_alpha=false;
            if(scene.has_obj_alpha)for(unsigned i=0;i<SCREEN_W*SCREEN_H && !own_alpha;++i)
                own_alpha=(scene.front_layer[i]&0x80) && scene.front_obj_alpha[i]!=17;
            if(!own_alpha){
                PpuPreparedCandidates view{scene,scene.front};
                view.state.bg_first_blend=true;
                return view;
            }
        }
    }
    if(cpu_resolve && scene.has_geometry && scene.has_obj_alpha && !scene.blank){
        // Semi-transparent OBJ over a geometry engine: when for every such
        // pixel the 3D layer lies behind both the OBJ and its blend target,
        // the DS result is independent of geometry coverage. Pre-blend those
        // pixels exactly as compose_candidates does and present the rest of
        // the scene through the ordinary opaque GPU composition.
        const unsigned mode=(scene.blend>>6)&3;
        const unsigned gp=scene.geometry_priority;
        bool admitted=!((mode==2 || mode==3) && (scene.blend&0x10));
        for(unsigned i=0;admitted && i<SCREEN_W*SCREEN_H;++i){
            if(!(scene.front_layer[i]&0x80))continue;
            const unsigned bl=scene.back_layer[i]&0x7f,bp=scene.back_priority[i];
            if(gp<scene.front_priority[i] || gp<bp || (gp==bp && bl!=4)){
                admitted=false;
                static unsigned reported=0;
                if(reported<8 && std::getenv("PPU_SEMI_TRACE")){++reported;
                    std::printf("PPU semi refused eng=%u px=%u gp=%u objprio=%u back=%u/%u blend=%04x\n",unsigned(eng),i,gp,unsigned(scene.front_priority[i]),bl,bp,unsigned(scene.blend));}
            }
        }
        if(admitted){
            struct SemiCache {Framebuffer front;uint64_t generation=0;uint16_t blend=0,alpha=0;bool valid=false;};
            static HostVector<SemiCache> semi(4); // per engine and cache way (minigames-30fps-min)
            auto& out=semi[eng*2+active_way_index(eng)];const auto& cache=(*prepared_cache[eng]);
            if(!out.valid || out.generation!=cache.plane_generation || out.blend!=scene.blend || out.alpha!=scene.alpha){
                out.front=scene.front;
                const AlphaBlend uniform(scene.alpha&31,(scene.alpha>>8)&31);
                for(int y=0;y<SCREEN_H;++y)for(int x=0;x<SCREEN_W;++x){
                    const unsigned i=y*SCREEN_W+x;
                    if(!(scene.front_layer[i]&0x80))continue;
                    const unsigned bl=scene.back_layer[i]&0x7f;
                    if(!(scene.blend&(1u<<(8+bl))))continue;
                    const unsigned a=scene.front_obj_alpha[i];
                    out.front.px[y][x]=a<=16?AlphaBlend(a,16-a)(scene.front.px[y][x],scene.back.px[y][x])
                        :uniform(scene.front.px[y][x],scene.back.px[y][x]);
                }
                out.generation=cache.plane_generation;out.blend=scene.blend;out.alpha=scene.alpha;out.valid=true;
                ++semi_preblends;
            }
            PpuPreparedCandidates view{scene,out.front};
            view.state.has_obj_alpha=false;
            return view;
        }
        // minigames-30fps-min: semi OBJ blending with the back plane or the
        // 3D (state.semi_back, drawn by the GPU: Composite::obj_first). It also
        // covers the semi_geometry case below with a cheaper worker mask
        // (Picture Poker: 13 ms -> obj_first). PPU_NO_GPU_SEMI_BACK=1 refuses;
        // PPU_SEMI_GEOMETRY_FIRST=1 keeps semi_geometry first.
        static const bool gpu_semi_back=std::getenv("PPU_NO_GPU_SEMI_BACK")==nullptr;
        static const bool semi_geometry_first=std::getenv("PPU_SEMI_GEOMETRY_FIRST")!=nullptr;
        auto semi_back_view=[&]()->std::optional<PpuPreparedCandidates>{
            // OBJ as a second target under 3D in front of it would need the
            // native blend over the raw OBJ colour: refused.
            bool back_ok=gpu_semi_back && !scene.has_windows && (mode==0 || (mode==1 && !(scene.blend&0x3f))) && !(scene.blend&0x1000);
            for(unsigned i=0;back_ok && i<SCREEN_W*SCREEN_H;++i)
                if((scene.front_layer[i]&0x80) && scene.front_obj_alpha[i]!=17)back_ok=false;
            if(!back_ok)return std::nullopt;
            PpuPreparedCandidates view{scene,scene.front};
            view.state.semi_back=true;
            return view;
        };
        if(!semi_geometry_first)if(auto view=semi_back_view())return *view;
        static const bool gpu_semi=std::getenv("PPU_NO_GPU_SEMI")==nullptr;
        bool semi_ok=gpu_semi && mode!=2 && mode!=3 && !scene.has_windows;
        for(unsigned i=0;semi_ok && i<SCREEN_W*SCREEN_H;++i){
            if(!(scene.front_layer[i]&0x80))continue;
            const unsigned bl=scene.back_layer[i]&0x7f;
            if(scene.front_obj_alpha[i]!=17 || gp<scene.front_priority[i] || (scene.blend&(1u<<(8+bl))))semi_ok=false;
        }
        if(semi_ok){
            PpuPreparedCandidates view{scene,scene.front};
            view.state.semi_geometry=true;
            return view;
        }
        if(semi_geometry_first)if(auto view=semi_back_view())return *view;
    }
    PpuPreparedCandidates view{scene,scene.resolved?(*prepared_cache[eng]).unresolved_front:scene.front};
    view.front_alpha_encoded=scene.front_alpha_encoded;
    return view;
}
static std::optional<PpuPreparedCandidates> background_candidates_impl(Engine eng);
std::optional<PpuPreparedCandidates> ppu_prepare_background_candidates_cached(Engine eng) {
    const double start=prepare_clock?prepare_clock():0;
    WindowCutScope scope;
    auto result=background_candidates_impl(eng);
    if(result)result->state.window_cut=window_cut_for(eng);
    if(prepare_clock)prepare_profiles[eng].bg_only+=prepare_clock()-start;
    return result;
}
// castle-30fps: single extended-affine BG (rotating/scrolling bottom-screen
// map) handed to the GPU as its whole-map raster plus the affine registers,
// instead of rasterizing 256x192 planes on the CPU every time the map moves.
// The raster is the same exact TextLayerRaster the CPU path samples.
// castle-min30: text layers in front of the affine layer (pause menu buttons,
// room overlays) are merged into one CPU overlay plane drawn over the GPU
// affine layer, and a BLDCNT brightness effect on the affine layer/backdrop
// is applied by the GPU colour table: the map no longer forces the CPU
// planes path (full 256x192 merges with per-pixel affine sampling) whenever
// it rotates or a menu opens. PPU_NO_GPU_AFFINE_OVERLAY=1 restores the
// single-layer, effect-free admission.
static std::optional<PpuPreparedCandidates> gpu_affine_candidates(Engine eng,BgCandidateCache& cache,const PpuCompositionState& scene){
    static const bool enabled=std::getenv("PPU_NO_GPU_AFFINE")==nullptr;
    static const bool overlay_admission=std::getenv("PPU_NO_GPU_AFFINE_OVERLAY")==nullptr;
    const unsigned mode=(scene.blend>>6)&3;
    if(!enabled || (mode && !overlay_admission))return std::nullopt;
    const auto& em=kEngines[eng];const auto disp=rd32(em.reg_base);
    int only=-1;int overlay_bg[3]{};unsigned overlays=0;
    for(int bg=0;bg<4;++bg){
        const auto cfg=read_bg(em,bg,disp);if(!cfg.enabled)continue;
        if(cfg.extended_tiled || cfg.extended_bitmap){if(only>=0)return std::nullopt;only=bg;}
        else{if(!overlay_admission || overlays==3)return std::nullopt;overlay_bg[overlays++]=bg;}
    }
    if(only<0)return std::nullopt;
    BgConfig c=read_bg(em,only,disp);
    if(!c.extended_tiled || c.extended_bitmap || c.tiles_w*c.tiles_h>64*64)return std::nullopt;
    BgConfig ocfg[3];
    for(unsigned k=0;k<overlays;++k){
        ocfg[k]=read_bg(em,overlay_bg[k],disp);
        const auto& o=ocfg[k];
        // In front of the affine layer at every pixel (lower priority value,
        // or equal priority and lower BG index), and never an effect target.
        if(!(o.priority<c.priority || (o.priority==c.priority && overlay_bg[k]<only)))return std::nullopt;
        if((mode==2 || mode==3) && ((scene.blend>>overlay_bg[k])&1))return std::nullopt;
        if(o.bpp8 && o.ext_palette)return std::nullopt;
    }
    // castle-min30: BLDCNT mode 1 with only overlay layers as first targets
    // (the camera-touch circle over the room map): each first-target overlay
    // texel is blended on the CPU with the pixel below it (next overlay, the
    // affine texel at the same DS affine sample as scanout_bg, or the
    // backdrop) exactly as alpha_blend(); the GPU draws the result over the
    // affine layer. OBJ must take no part in the blend and no visible OBJ may
    // sit between a blended overlay and the affine layer.
    unsigned overlay_mask=0;for(unsigned k=0;k<overlays;++k)overlay_mask|=1u<<overlay_bg[k];
    const bool blend1=mode==1;
    if(blend1){
        // Target bits of disabled BG layers take no part (OBJ bits checked raw).
        const unsigned present=overlay_mask|(1u<<only)|0x20u;
        const unsigned first=scene.blend&present,second=(scene.blend>>8)&present;
        if((first&~(overlay_mask|0x20u)) || (second&overlay_mask) || (scene.blend&0x1010))return std::nullopt;
        if(disp&(1u<<12)){
            for(unsigned k=0;k<overlays;++k){
                if(!((first>>overlay_bg[k])&1))continue;
                const unsigned f=unsigned(ocfg[k].priority),b=unsigned(c.priority);
                if(f>=b)continue; // an OBJ wins priority ties: nothing fits strictly between
                const unsigned oam=eng==ENGINE_A?0x07000000u:0x07000400u;
                for(unsigned i=0;i<128;++i){
                    const unsigned a0=rd16(oam+i*8),a2=rd16(oam+i*8+4);
                    if(!(a0&0x100) && (a0&0x200))continue;
                    const unsigned q=(a2>>10)&3;
                    if(q>f && q<=b)return std::nullopt;
                }
            }
        }
    }
    if(&sm64ds_gpu_affine_ready && !sm64ds_gpu_affine_ready){
        if(&sm64ds_gpu_affine_wanted)sm64ds_gpu_affine_wanted=1;
        return std::nullopt;
    }
    struct OverlayKey {unsigned n=0;int bg[3]{};uint64_t generation[3]{};int hofs[3]{},vofs[3]{},priority[3]{};const void* raster[3]{};
        // Mode-1 blends also depend on the affine layer under the overlays.
        bool blend=false;uint32_t regs=0,backdrop=0;int32_t ref_x=0,ref_y=0;int16_t pa=0,pb=0,pc=0,pd=0;bool wrap=false;uint64_t affine_generation=0;const void* affine_raster=nullptr;
        bool operator==(const OverlayKey& b)const{
            if(n!=b.n || blend!=b.blend)return false;
            if(blend && (regs!=b.regs || backdrop!=b.backdrop || ref_x!=b.ref_x || ref_y!=b.ref_y || pa!=b.pa || pb!=b.pb || pc!=b.pc || pd!=b.pd ||
                wrap!=b.wrap || affine_generation!=b.affine_generation || affine_raster!=b.affine_raster))return false;
            for(unsigned k=0;k<n;++k)if(bg[k]!=b.bg[k] || generation[k]!=b.generation[k] || hofs[k]!=b.hofs[k] || vofs[k]!=b.vofs[k] ||
                priority[k]!=b.priority[k] || raster[k]!=b.raster[k])return false;
            return true;
        }};
    struct State {PpuGpuAffineLayer layer[2]{};HostVector<uint32_t> raster[2];uint64_t source[2]{~0ull,~0ull};unsigned current=0;uint64_t generation=0;
        HostVector<uint32_t> overlay[2];OverlayKey overlay_key[2];unsigned overlay_current=0;uint64_t overlay_generation=0;};
    static HostVector<State> states(2);auto& s=states[eng];
    // castle-min30: exact raw-input memo (registers, VRAMCNT, BG palette,
    // extended palette banks, maps, characters). Unchanged inputs (held
    // VBlanks) return the previous descriptor without palette conversion or
    // the per-layer snapshot compares. PPU_NO_AFFINE_MEMO=1 disables it.
    static const bool memo_enabled=std::getenv("PPU_NO_AFFINE_MEMO")==nullptr;
    struct Memo {HostVector<BgInputSpan> spans;HostVector<uint8_t> bytes;bool valid=false;};
    static Memo memos[2];auto& memo=memos[eng];
    static HostVector<BgInputSpan> memo_spans_storage[2];auto& memo_spans=memo_spans_storage[eng];memo_spans.clear();
    if(memo_enabled){
        auto add=[&](uint32_t address,size_t size){memo_spans.push_back({address,size});};
        add(em.reg_base,0x70);add(0x04000240,12);add(em.pltt_base,0x200);
        {const unsigned offset=c.palette_slot*8192;
         auto rd8=[](uint32_t a){return *static_cast<volatile uint8_t*>(host_ptr(a));};
         if(c.bpp8 && c.ext_palette){
             if(!em.has_base_offset){if((rd8(0x04000248)&0x83)==0x82)add(0x06898000+offset,8192);}
             else{
                 if((rd8(0x04000244)&0x87)==0x84)add(0x06880000+offset,8192);
                 for(unsigned bank=0;bank<2;++bank){const unsigned cnt=rd8(0x04000245+bank);
                     if((cnt&0x87)==0x84 && (offset>>14)==((cnt>>3)&1))add(0x06890000+bank*0x4000+(offset&0x3fff),8192);}
             }
         }}
        add(c.screen,size_t(c.tiles_w)*c.tiles_h*2);add(c.chars,c.bpp8?65536:32768);
        for(unsigned k=0;k<overlays;++k){add(ocfg[k].screen,size_t(ocfg[k].tiles_w)*ocfg[k].tiles_h*2);add(ocfg[k].chars,ocfg[k].bpp8?65536:32768);}
        std::sort(memo_spans.begin(),memo_spans.end(),[](const BgInputSpan& a,const BgInputSpan& b){return a.address<b.address;});
        unsigned merged=0;
        for(unsigned i=0;i<memo_spans.size();++i){
            if(merged && memo_spans[i].address<=memo_spans[merged-1].address+memo_spans[merged-1].size){
                auto& previous=memo_spans[merged-1];
                previous.size=std::max(previous.size,size_t(memo_spans[i].address-previous.address)+memo_spans[i].size);
            }else memo_spans[merged++]=memo_spans[i];
        }
        memo_spans.resize(merged);
        bool same=memo.valid && memo.spans.size()==merged;
        for(unsigned i=0;same && i<merged;++i)same=memo.spans[i].address==memo_spans[i].address && memo.spans[i].size==memo_spans[i].size;
        size_t offset=0;
        for(unsigned i=0;same && i<merged;++i){same=!std::memcmp(memo.bytes.data()+offset,host_ptr(memo_spans[i].address),memo_spans[i].size);offset+=memo_spans[i].size;}
        if(same && s.generation){
            PpuCompositionState resolved=scene;if(blend1){resolved.blend=0;resolved.alpha=0;} // mode 1 resolved in the overlay plane
            PpuPreparedCandidates out{resolved,cache.planes[0],s.generation,cache.planes[0].front,&s.layer[s.current]};
            out.gpu_affine=&s.layer[s.current];
            return out;
        }
        memo.valid=false;
    }
    // DS order front to back: priority, then BG index.
    for(unsigned i=1;i<overlays;++i)for(unsigned j=i;j>0 && (ocfg[j].priority<ocfg[j-1].priority ||
        (ocfg[j].priority==ocfg[j-1].priority && overlay_bg[j]<overlay_bg[j-1]));--j){std::swap(ocfg[j],ocfg[j-1]);std::swap(overlay_bg[j],overlay_bg[j-1]);}
    // Resolved colours exactly as scanout_bg does for its layer cache.
    static uint32_t palette[2][256],extended[2][4096];
    for(unsigned i=0;i<256;++i)palette[eng][i]=bgr555(rd16(em.pltt_base+i*2));
    c.colors=palette[eng];
    if(c.bpp8 && c.ext_palette){
        const uint16_t* sources[3];unsigned n=0;const unsigned offset=c.palette_slot*8192;
        auto rd8=[](uint32_t a){return *static_cast<volatile uint8_t*>(host_ptr(a));};
        if(!em.has_base_offset){
            if((rd8(0x04000248)&0x83)==0x82)sources[n++]=static_cast<const uint16_t*>(host_ptr(0x06898000+offset));
        }else{
            if((rd8(0x04000244)&0x87)==0x84)sources[n++]=static_cast<const uint16_t*>(host_ptr(0x06880000+offset));
            for(unsigned bank=0;bank<2;++bank){
                const unsigned cnt=rd8(0x04000245+bank);
                if((cnt&0x87)==0x84 && (offset>>14)==((cnt>>3)&1))
                    sources[n++]=static_cast<const uint16_t*>(host_ptr(0x06890000+bank*0x4000+(offset&0x3fff)));
            }
        }
        for(unsigned i=0;i<4096;++i){uint16_t v=0;for(unsigned k=0;k<n;++k)v|=sources[k][i];extended[eng][i]=bgr555(v);}
        c.colors=extended[eng];
    }
    auto& layer_cache=cache.layers[only];
    if(!update_text_layer(c,layer_cache) || !layer_cache.raster_valid)return std::nullopt;
    for(unsigned k=0;k<overlays;++k){
        ocfg[k].colors=palette[eng];
        auto& o=cache.layers[overlay_bg[k]];
        if(!update_text_layer(ocfg[k],o,true) || !o.raster_valid)return std::nullopt;
    }
    const uint32_t* overlay=nullptr;uint64_t overlay_generation=0;
    if(overlays){
        OverlayKey key;key.n=overlays;
        for(unsigned k=0;k<overlays;++k){const auto& o=cache.layers[overlay_bg[k]];key.bg[k]=overlay_bg[k];key.generation[k]=o.generation;
            key.hofs[k]=ocfg[k].hofs;key.vofs[k]=ocfg[k].vofs;key.priority[k]=ocfg[k].priority;key.raster[k]=o.px.data();}
        if(blend1){key.blend=true;key.regs=uint32_t(scene.blend)|(uint32_t(scene.alpha)<<16);key.backdrop=palette[eng][0];key.ref_x=c.ref_x;key.ref_y=c.ref_y;
            key.pa=int16_t(c.pa);key.pb=int16_t(c.pb);key.pc=int16_t(c.pc);key.pd=int16_t(c.pd);key.wrap=c.wrap;key.affine_generation=layer_cache.generation;key.affine_raster=layer_cache.px.data();}
        if(!s.overlay_generation || !(s.overlay_key[s.overlay_current]==key)){
            // Double-buffered like the raster: the worker may read the current plane.
            const unsigned target=s.overlay_current^1;
            try{s.overlay[target].resize(SCREEN_W*SCREEN_H);}catch(const std::bad_alloc&){return std::nullopt;}
            static uint32_t lines_storage[2][3][SCREEN_W];auto& lines=lines_storage[eng];
            const uint32_t* rows[3];uint32_t code[3];
            for(unsigned k=0;k<overlays;++k)code[k]=uint32_t(32u*(unsigned(ocfg[k].priority)+2u))<<24;
            uint32_t* out=s.overlay[target].data();
            for(int y=0;y<SCREEN_H;++y,out+=SCREEN_W){
                for(unsigned k=0;k<overlays;++k){
                    // Same texel as scanout_bg's cached-layer row copy.
                    const auto& o=ocfg[k];const auto& raster=cache.layers[overlay_bg[k]].px;
                    const int width=o.tiles_w*8,py=(y+o.vofs)&(o.tiles_h*8-1),px=o.hofs&(width-1);
                    const uint32_t* source=&raster[size_t(py)*width];
                    if(px+SCREEN_W<=width){rows[k]=source+px;continue;}
                    const int first=std::min(width-px,SCREEN_W);
                    std::memcpy(lines[k],source+px,first*4);
                    for(int x=first;x<SCREEN_W;x+=width)std::memcpy(lines[k]+x,source,std::min(width,SCREEN_W-x)*4);
                    rows[k]=lines[k];
                }
                if(blend1){
                    // Exact DS blend of first-target overlay texels (see admission above).
                    const unsigned first=scene.blend&0x3f,second=(scene.blend>>8)&0x3f;
                    const unsigned eva=std::min(scene.alpha&31u,16u),evb=std::min((scene.alpha>>8)&31u,16u);
                    // alpha_blend() through tables: per 6-bit channel min(63,(a*eva+b*evb+8)/16), expanded.
                    uint16_t ta[64],tb[64];uint8_t out8[128];
                    for(unsigned i=0;i<64;++i){ta[i]=uint16_t(i*eva+8);tb[i]=uint16_t(i*evb);}
                    for(unsigned i=0;i<128;++i){const unsigned q=std::min(63u,i);out8[i]=uint8_t((q<<2)|(q>>4));}
                    auto blend=[&](uint32_t f,uint32_t b){
                        uint32_t r=0xff000000u;
                        for(unsigned s=0;s<24;s+=8)r|=uint32_t(out8[(ta[((f>>s)&255)>>2]+tb[((b>>s)&255)>>2])>>4])<<s;
                        return r;
                    };
                    const uint32_t* araster=layer_cache.px.data();const int aw=c.tiles_w*8,ah=c.tiles_h*8;
                    for(int x=0;x<SCREEN_W;x+=4){
                        uint32_t any=0;
                        for(unsigned k=0;k<overlays;++k){const uint32_t* r=rows[k]+x;any|=r[0]|r[1]|r[2]|r[3];}
                        if(!(any>>24)){out[x]=out[x+1]=out[x+2]=out[x+3]=0u;continue;}
                        for(int q=0;q<4;++q){
                            const int px=x+q;uint32_t v=0;
                            for(unsigned k=0;k<overlays;++k){
                                const uint32_t a=rows[k][px];if(!(a>>24))continue;
                                uint32_t color=a;
                                if((first>>overlay_bg[k])&1){
                                    bool lower=false;
                                    for(unsigned j=k+1;j<overlays;++j)if(rows[j][px]>>24){lower=true;break;}
                                    if(!lower){
                                        // Same texel as scanout_bg's cached affine fill_line.
                                        const int32_t ux=c.ref_x+c.pb*y+c.pa*px,uy=c.ref_y+c.pd*y+c.pc*px;
                                        uint32_t under=0;
                                        if(c.wrap)under=araster[size_t((uy>>8)&(ah-1))*aw+((ux>>8)&(aw-1))];
                                        else{const int sx=ux>>8,sy=uy>>8;if(unsigned(sx)<unsigned(aw)&&unsigned(sy)<unsigned(ah))under=araster[size_t(sy)*aw+sx];}
                                        if(under>>24){if((second>>only)&1)color=blend(a,under);}
                                        else if(second&0x20)color=blend(a,palette[eng][0]);
                                    }
                                }
                                v=(color&0xffffffu)|code[k];break;
                            }
                            out[px]=v;
                        }
                    }
                }else if(overlays==1){
                    const uint32_t* r0=rows[0];const uint32_t c0=code[0];
                    for(int x=0;x<SCREEN_W;++x){const uint32_t a=r0[x];out[x]=(a>>24)?(a&0xffffffu)|c0:0u;}
                }else for(int x=0;x<SCREEN_W;x+=4){
                    uint32_t any=0;
                    for(unsigned k=0;k<overlays;++k){const uint32_t* r=rows[k]+x;any|=r[0]|r[1]|r[2]|r[3];}
                    if(!(any>>24)){out[x]=out[x+1]=out[x+2]=out[x+3]=0u;continue;}
                    for(int q=0;q<4;++q){
                        uint32_t v=0;
                        for(unsigned k=0;k<overlays;++k){const uint32_t a=rows[k][x+q];if(a>>24){v=(a&0xffffffu)|code[k];break;}}
                        out[x+q]=v;
                    }
                }
            }
            s.overlay_key[target]=key;s.overlay_current=target;++s.overlay_generation;
        }
        overlay=s.overlay[s.overlay_current].data();overlay_generation=s.overlay_generation;
    }
    const int width=c.tiles_w*8,height=c.tiles_h*8;
    PpuGpuAffineLayer next{nullptr,width,height,layer_cache.generation,c.ref_x,c.ref_y,int16_t(c.pa),int16_t(c.pb),int16_t(c.pc),int16_t(c.pd),
        c.wrap,palette[eng][0],uint8_t(only),uint8_t(c.priority)};
    next.overlay=overlay;next.overlay_generation=overlay_generation;
    if((mode==2 || mode==3) && (((scene.blend>>only)&1) || (scene.blend&0x20))){
        next.effect_mode=uint8_t(mode);next.effect_factor=uint8_t(std::min(16u,unsigned(scene.brightness)));
        next.effect_layer=(scene.blend>>only)&1;next.effect_backdrop=(scene.blend&0x20)!=0;
    }
    const auto& cur=s.layer[s.current];
    const bool same=s.generation && cur.width==next.width && cur.height==next.height && s.source[s.current]==layer_cache.generation &&
        cur.ref_x==next.ref_x && cur.ref_y==next.ref_y && cur.pa==next.pa && cur.pb==next.pb && cur.pc==next.pc && cur.pd==next.pd &&
        cur.wrap==next.wrap && cur.backdrop==next.backdrop && cur.layer==next.layer && cur.priority==next.priority &&
        cur.overlay==next.overlay && cur.overlay_generation==next.overlay_generation && cur.effect_mode==next.effect_mode &&
        cur.effect_factor==next.effect_factor && cur.effect_layer==next.effect_layer && cur.effect_backdrop==next.effect_backdrop;
    if(!same){
        // Double-buffered: the presentation worker may still read the other.
        const unsigned target=s.current^1;
        next.base_generation=0;next.dirty_bands[0]=next.dirty_bands[1]=0;
        if(s.source[target]!=layer_cache.generation || s.raster[target].size()!=size_t(width)*height){
            const auto& cur_raster=s.raster[s.current];
            const bool incremental=s.generation && cur_raster.size()==size_t(width)*height && height<=1024;
            try{s.raster[target].resize(size_t(width)*height);}catch(const std::bad_alloc&){return std::nullopt;}
            // Copy band by band, recording which 8-row bands differ from the
            // raster the consumer currently holds (the other buffer).
            const size_t band_words=size_t(width)*8;
            for(int band=0;band*8<height;++band){
                const uint32_t* src=layer_cache.px.data()+band*band_words;
                if(incremental && std::memcmp(src,cur_raster.data()+band*band_words,band_words*4)==0){
                    std::memcpy(s.raster[target].data()+band*band_words,src,band_words*4);continue;
                }
                std::memcpy(s.raster[target].data()+band*band_words,src,band_words*4);
                next.dirty_bands[band>>6]|=1ull<<(band&63);
            }
            if(incremental)next.base_generation=s.source[s.current];
            s.source[target]=layer_cache.generation;
        }else{
            // Same raster bytes as the target already holds; relative to the
            // current buffer nothing changed only if both share a generation.
            if(s.source[s.current]==layer_cache.generation)next.base_generation=layer_cache.generation;
        }
        next.raster=s.raster[target].data();
        s.layer[target]=next;s.current=target;++s.generation;
    }
    // The planes are not read on this path; any valid object serves as the
    // reference target, and the identity token is the layer descriptor.
    if(memo_enabled){
        size_t size=0;for(const auto& span:memo_spans)size+=span.size;
        try{memo.bytes.resize(size);memo.spans=memo_spans;}catch(const std::bad_alloc&){memo.valid=false;size=0;}
        if(size){size_t offset=0;for(const auto& span:memo_spans){std::memcpy(memo.bytes.data()+offset,host_ptr(span.address),span.size);offset+=span.size;}memo.valid=true;}
    }
    PpuCompositionState resolved=scene;if(blend1){resolved.blend=0;resolved.alpha=0;} // mode 1 resolved in the overlay plane
    PpuPreparedCandidates out{resolved,cache.planes[0],s.generation,cache.planes[0].front,&s.layer[s.current]};
    out.gpu_affine=&s.layer[s.current];
    return out;
}
static BgCandidateCache& bg_only_cache(Engine eng){
    static HostVector<BgCandidateCache> caches[2];
    auto& storage=caches[eng];if(storage.empty())storage.resize(1);
    return storage[0];
}
// castle-min30: the engine-B GPU-affine attempt alone, safe to run on the
// presentation core while the main thread prepares engine A: it touches only
// engine-B state (bg-only cache, affine descriptors, memo, per-engine
// scratch) and the PPU's shared scratch made per call/engine above. The
// caller runs ppu_sync_vram_views() first and sets state.window_cut after.
// Falls back (nullopt) whenever background_candidates_impl would not return
// a GPU-affine view first; the caller then runs the full path.
void ppu_sync_vram_views(){sync_vram_views();}
std::optional<PpuPreparedCandidates> ppu_prepare_background_affine(Engine eng){
    if(eng!=ENGINE_B)return std::nullopt;
    const auto& em=kEngines[eng];const auto disp=rd32(em.reg_base);
    if(((disp&0xe000) && !windows_binary(eng)) || ((disp>>16)&3)>=2)return std::nullopt;
    {   // Same mode-1 gate as background_candidates_impl before its affine attempt.
        const uint16_t blend=rd16(em.reg_base+0x50);
        if(((blend>>6)&3)==1 && (blend&0x3f) && (blend&0x3f00) && !(disp&0x80) && ((disp>>16)&3) && (blend&0x1010))return std::nullopt;
    }
    PpuCompositionState scene{};
    scene.resolved=false;scene.has_windows=false;scene.has_obj_alpha=false;
    scene.blank=(disp&0x80) || ((disp>>16)&3)==0;
    if(scene.blank)return std::nullopt;
    scene.has_geometry=false;
    scene.geometry_priority=rd16(em.reg_base+8)&3;
    scene.blend=rd16(em.reg_base+0x50);scene.alpha=rd16(em.reg_base+0x52);
    scene.brightness=rd16(em.reg_base+0x54)&31;scene.master=rd16(em.reg_base+0x6c);
    return gpu_affine_candidates(eng,bg_only_cache(eng),scene);
}
void ppu_apply_window_cut(Engine eng,PpuPreparedCandidates& candidates){candidates.state.window_cut=window_cut_for(eng);}
static std::optional<PpuPreparedCandidates> background_candidates_impl(Engine eng) {
    const auto& em=kEngines[eng];const auto disp=rd32(em.reg_base);
    // OBJ-window cannot be resolved without rasterising OBJ. Capture/LCDC/FIFO
    // presentation needs its own driver and must never receive regular planes.
    if(((disp&0xe000) && !windows_binary(eng)) || ((disp>>16)&3)>=2)return std::nullopt; // binary windows: cut (minigames-30fps-min)
    // BLDCNT mode1 between two targets: the OBJ-free planes cannot carry the
    // blend (an OBJ may sit between both targets). Callers then use the full
    // candidates, which ppu_prepare_candidates_cached resolves exactly on CPU.
    static const bool cpu_resolve=std::getenv("PPU_NO_CPU_RESOLVE")==nullptr;
    // castle-30fps: BG-only BLDCNT mode1 can be resolved on the BG planes
    // themselves when no OBJ takes part in the blend (see preblend below).
    static const bool bg_preblend=std::getenv("PPU_NO_BG_PREBLEND")==nullptr;
    bool preblend=false;
    if(cpu_resolve){
        const uint16_t blend=rd16(em.reg_base+0x50);
        if(((blend>>6)&3)==1 && (blend&0x3f) && (blend&0x3f00) && !(disp&0x80) && ((disp>>16)&3)){
            // OBJ as a first or second target needs per-pixel OBJ coverage.
            if(!bg_preblend || (blend&0x1010) || eng!=ENGINE_B)return std::nullopt;
            preblend=true;
        }
    }
    sync_vram_views();
    // The pixel cache must not be shared with normal preparation: CPU OBJ
    // fallback and its window masks cannot invalidate a borrowed BG-only view.
    auto& cache=bg_only_cache(eng);PpuCompositionState scene{};
    scene.resolved=false;scene.has_windows=false;scene.has_obj_alpha=false;
    scene.blank=(disp&0x80) || ((disp>>16)&3)==0;
    scene.has_geometry=!scene.blank && eng==ENGINE_A && (disp&8) && (disp&(1u<<8));
    scene.geometry_priority=rd16(em.reg_base+8)&3;
    scene.blend=rd16(em.reg_base+0x50);scene.alpha=rd16(em.reg_base+0x52);
    scene.brightness=rd16(em.reg_base+0x54)&31;scene.master=rd16(em.reg_base+0x6c);
    if(!scene.blank && eng==ENGINE_B){ // castle-min30: the affine path also resolves mode-1 overlays (before preblend)
        if(auto gpu=gpu_affine_candidates(eng,cache,scene))return gpu;
    }
    const auto previous_generation=cache.generation;
    static const bool front_alpha=std::getenv("PPU_NO_FRONT_ALPHA")==nullptr;
    const auto& planes=background_planes_cached(eng,cache,false,nullptr,
        std::getenv("PLAYER_UNCACHED_BACKGROUNDS")!=nullptr,front_alpha);
    if(previous_generation==cache.generation){
        ++background_transfers.immutable_hits;
        background_transfers.planes_avoided+=sizeof(PpuBackgroundPlanes);
    }else ++background_transfers.immutable_misses;
    if(preblend){
        // minigames-30fps-min (Loves Me): leave the mode1 pair blend to the GPU
        // composition (state.bg_first_blend without geometry: Composite::
        // obj_first variant 2, same exact rule as the CPU preblend below) when
        // no visible OBJ priority can sit between a first-target BG and a
        // second-target BG/backdrop (conservative, from BGxCNT priorities).
        // Saves the full-screen CPU preblend after every scroll.
        // PPU_NO_GPU_BG_PAIR_BLEND=1 keeps the CPU preblend.
        static const bool gpu_pair=std::getenv("PPU_NO_GPU_BG_PAIR_BLEND")==nullptr;
        if(gpu_pair){
            unsigned first_prio=0,second_prio=0; // bit p: a target layer at priority p (4 = backdrop)
            for(unsigned bg=0;bg<4;++bg)if(disp&(0x100u<<bg)){
                const unsigned prio=rd16(em.reg_base+8+bg*2)&3;
                if((scene.blend>>bg)&1)first_prio|=1u<<prio;
                if((scene.blend>>(8+bg))&1)second_prio|=1u<<prio;
            }
            if(scene.blend&0x2000)second_prio|=1u<<4;
            bool between=false;
            if(disp&(1u<<12)){
                const unsigned oam=eng==ENGINE_A?0x07000000u:0x07000400u;
                for(unsigned k=0;k<128 && !between;++k){
                    const unsigned a0=rd16(oam+k*8),a2=rd16(oam+k*8+4);
                    if(!(a0&0x100) && (a0&0x200))continue;
                    const unsigned q=(a2>>10)&3;
                    for(unsigned f=0;f<q;++f)if(first_prio&(1u<<f))for(unsigned b=q;b<5;++b)if(second_prio&(1u<<b))between=true;
                }
            }
            if(!between){
                PpuPreparedCandidates view{scene,planes,cache.generation};
                view.front_alpha_encoded=cache.alpha_encoded[cache.current];
                view.state.bg_first_blend=true;
                return view;
            }
        }
        // Exactly compose_candidates' geometry-free mode1 rule: a front
        // pixel of a first target over a second-target back pixel becomes
        // the DS blend. OBJs are not targets here, so an OBJ in front keeps
        // its colour and an OBJ behind both layers is hidden, as on the DS.
        // An OBJ between the two layers would cancel the blend: refused.
        struct Preblend {
            Framebuffer front[2];const PpuBackgroundPlanes* planes=nullptr;unsigned current=0;
            uint64_t source=0,generation=0;uint16_t blend=0,alpha=0;bool valid=false;
            bool pair[4][4]{}; // [front priority][back priority] of blended pixels
        };
        static Preblend* preblends=nullptr; // engine B only (see above)
        if(!preblends){try{preblends=new(HostAllocator<Preblend>{}.allocate(1)) Preblend{};}catch(const std::bad_alloc&){return std::nullopt;}}
        auto& out=*preblends;
        const uint16_t blend=scene.blend,alpha=scene.alpha;
        if(!out.valid || out.source!=cache.generation || out.blend!=blend || out.alpha!=alpha || out.planes!=&planes){
            auto& target=out.front[out.current^1];
            std::memcpy(&target,&planes.front,sizeof target);
            std::memset(out.pair,0,sizeof out.pair);
            const AlphaBlend mix(alpha&31,(alpha>>8)&31);
            for(int y=0;y<SCREEN_H;++y)for(int x=0;x<SCREEN_W;++x){
                const unsigned i=y*SCREEN_W+x;
                const unsigned fl=planes.front_layer[i]&0x7f,bl=planes.back_layer[i]&0x7f;
                if(!((blend>>fl)&1) || !((blend>>(8+bl))&1))continue;
                target.px[y][x]=mix(planes.front.px[y][x],planes.back.px[y][x]);
                out.pair[planes.front_priority[i]&3][planes.back_priority[i]&3]=true;
            }
            out.current^=1;out.source=cache.generation;out.blend=blend;out.alpha=alpha;out.valid=true;out.planes=&planes;
            static uint64_t counter=0;out.generation=++counter;
        }
        // Visible OBJ priorities q with front<q<=back would sit between the
        // two blended layers (OBJ wins priority ties): not representable.
        if((disp&(1u<<12))){
            const unsigned oam=eng==ENGINE_A?0x07000000u:0x07000400u;
            for(unsigned k=0;k<128;++k){
                const unsigned a0=rd16(oam+k*8),a2=rd16(oam+k*8+4);
                if(!(a0&0x100) && (a0&0x200))continue;
                const unsigned q=(a2>>10)&3;
                for(unsigned f=0;f<q;++f)for(unsigned b=q;b<4;++b)if(out.pair[f][b])return std::nullopt;
            }
        }
        scene.blend=0;scene.alpha=0;
        // Metadata planes are the (unblended) source planes: same layers and
        // priorities; only the front colours differ.
        return PpuPreparedCandidates{scene,planes,out.generation,out.front[out.current],&out.front[out.current]};
    }
    PpuPreparedCandidates view{scene,planes,cache.generation};
    view.front_alpha_encoded=cache.alpha_encoded[cache.current];
    return view;
}
PpuPreparedCandidates ppu_prepare_capture_candidates() {
    if(((rd32(kEngines[ENGINE_A].reg_base)>>16)&3)!=0)
        return ppu_prepare_candidates_cached(ENGINE_A);
    sync_vram_views();
    static HostVector<PpuPreparedScene> capture_scenes;
    if(capture_scenes.empty())capture_scenes.resize(1);
    auto& scene=capture_scenes[0];
    prepare_scene(ENGINE_A,scene,nullptr,false,false,true);
    return {scene,scene.front};
}

static void compose_candidates(PpuPreparedCandidates candidates,const Framebuffer& geometry,
                               Framebuffer& output,bool master,
                               int rx0,int rx1,int ry0,int ry1) {
    const auto& scene=candidates.state;const auto& candidate_front=candidates.front;
    if(scene.blank){output=candidate_front;return;}
    const unsigned mode=(scene.blend>>6)&3;
    const bool native_blend_possible=scene.has_geometry && (scene.blend&0x3f00);
    const unsigned master_mode=master?scene.master>>14:0;
    const AlphaBlend blend_pixels(scene.alpha&31,(scene.alpha>>8)&31);
    // Brightness depends only on the six-bit channel and the frame's factor.
    // Keep exact DS rounding while resolving the arithmetic once per frame.
    uint8_t layer_lut[64],master_lut[64];
    if(mode==2 || mode==3)for(unsigned i=0;i<64;++i)
        layer_lut[i]=brightness(i<<2,scene.brightness,mode==2)&255;
    if(master_mode==1 || master_mode==2)for(unsigned i=0;i<64;++i)
        master_lut[i]=brightness(i<<2,scene.master&31,master_mode==1,true)&255;
    auto apply_brightness=[](uint32_t c,const uint8_t* lut){
        return 0xff000000u | (uint32_t(lut[(c>>18)&63])<<16) |
            (uint32_t(lut[(c>>10)&63])<<8) | lut[(c>>2)&63];
    };
    auto geometry_before=[&](unsigned layer,unsigned priority){
        // OBJ wins a priority tie; BG0 wins ties with BG1..3 and backdrop.
        return scene.geometry_priority<priority || (scene.geometry_priority==priority && (layer&0x7f)!=4);
    };
    if(!scene.has_obj_alpha && !scene.has_windows && !native_blend_possible && (scene.blend&63)==63 &&
       (mode==2 || mode==3)) {
        // A uniform fade needs neither second candidates nor per-pixel
        // effect layer tests. Compose both brightness operations in channel space,
        // retaining the exact rounding after the first operation.
        if(master_mode==1 || master_mode==2)
            for(unsigned i=0;i<64;++i)layer_lut[i]=master_lut[layer_lut[i]>>2];
        if(!scene.has_geometry){
            for(int y=ry0;y<ry1;++y)for(int x=rx0;x<rx1;++x)
                output.px[y][x]=apply_brightness(candidate_front.px[y][x],layer_lut);
        }else{
            for(int y=ry0;y<ry1;++y)for(int x=rx0;x<rx1;++x){
                const unsigned i=y*SCREEN_W+x;const auto gx=geometry.px[y][x];
                const auto front=(gx>>24) && geometry_before(candidates.front_layer()[i],candidates.front_priority()[i])
                    ?gx:candidate_front.px[y][x];
                output.px[y][x]=apply_brightness(front,layer_lut);
            }
        }
        return;
    }
    if(mode==0 && !scene.has_obj_alpha && !native_blend_possible){
        // Master brightness applies after selection and does not need the
        // second visible layer either.
        for(int y=ry0;y<ry1;++y)for(int x=rx0;x<rx1;++x){
            const unsigned i=y*SCREEN_W+x;const auto gx=geometry.px[y][x];
            const bool visible=scene.has_geometry && (!scene.has_windows || (candidates.window_mask()[i]&1));
            const auto front=visible && (gx>>24) && geometry_before(candidates.front_layer()[i],candidates.front_priority()[i])
                ?gx:candidate_front.px[y][x];
            output.px[y][x]=(master_mode==1 || master_mode==2)
                ?apply_brightness(front,master_lut):(front|0xff000000u);
        }
        return;
    }
    static const bool fast_alpha=std::getenv("PPU_REFERENCE_COMPOSE")==nullptr;
    if(fast_alpha && !scene.has_geometry && !scene.has_windows && scene.has_obj_alpha &&
       (mode==0 || (mode==1 && !(scene.blend&0x3f))) && master_mode!=1 && master_mode!=2){
        // minigames-30fps-min (Shell Smash, Shuffle Shell): semi-transparent
        // OBJ over BGs with no other first target. The general loop reduces to
        // a copy except at semi OBJ pixels (front layer bit7), which blend
        // with an eligible second-target back pixel. Four layer bytes at once.
        const uint8_t* front_layers=candidates.front_layer();const uint8_t* back_layers=candidates.back_layer();
        const uint8_t* obj_alpha=candidates.front_obj_alpha();
        for(int y=ry0;y<ry1;++y){
            int x=rx0;
            for(;x<rx1 && (x&3);++x){
                const unsigned i=y*SCREEN_W+x;uint32_t front=candidate_front.px[y][x];
                if((front_layers[i]&0x80) && ((scene.blend>>(8+(back_layers[i]&0x7f)))&1))
                    front=obj_alpha[i]<=16?AlphaBlend(obj_alpha[i],16-obj_alpha[i])(front,candidates.back.px[y][x]):blend_pixels(front,candidates.back.px[y][x]);
                output.px[y][x]=front|0xff000000u;
            }
            for(;x<rx1;++x){
                const unsigned i=y*SCREEN_W+x;
                if(x+4<=rx1 && !(x&3)){
                    uint32_t word;std::memcpy(&word,front_layers+i,4);
                    if(!(word&0x80808080u)){
                        for(unsigned k=0;k<4;++k)output.px[y][x+k]=candidate_front.px[y][x+k]|0xff000000u;
                        x+=3;continue;
                    }
                }
                uint32_t front=candidate_front.px[y][x];
                if((front_layers[i]&0x80) && ((scene.blend>>(8+(back_layers[i]&0x7f)))&1))
                    front=obj_alpha[i]<=16?AlphaBlend(obj_alpha[i],16-obj_alpha[i])(front,candidates.back.px[y][x]):blend_pixels(front,candidates.back.px[y][x]);
                output.px[y][x]=front|0xff000000u;
            }
        }
        return;
    }
    if(fast_alpha && !scene.has_geometry && !scene.has_windows && mode==1 && !scene.has_obj_alpha &&
       master_mode!=1 && master_mode!=2){
        // Geometry-free BLDCNT mode1 without semi OBJ/windows/master: the
        // general loop below reduces to this two-target test per pixel.
        const uint8_t* front_layers=candidates.front_layer();const uint8_t* back_layers=candidates.back_layer();
        for(int y=ry0;y<ry1;++y)for(int x=rx0;x<rx1;++x){
            const unsigned i=y*SCREEN_W+x;
            uint32_t front=candidate_front.px[y][x];
            if((scene.blend>>(front_layers[i]&0x7f))&1 && (scene.blend>>(8+(back_layers[i]&0x7f)))&1)
                front=blend_pixels(front,candidates.back.px[y][x]);
            output.px[y][x]=front|0xff000000u;
        }
        return;
    }
    for(int y=ry0;y<ry1;++y)for(int x=rx0;x<rx1;++x){
        const unsigned i=y*SCREEN_W+x;
        uint32_t front=candidate_front.px[y][x];
        unsigned layer=candidates.front_layer()[i]&0x7f;
        const auto gx=geometry.px[y][x];
        const unsigned window=scene.has_windows?candidates.window_mask()[i]:0x3f;
        const bool gx_visible=scene.has_geometry && (window&1) && (gx>>24);
        const bool gx_front=gx_visible && geometry_before(layer,candidates.front_priority()[i]);
        if(gx_front){front=gx;layer=0;}
        // Only alpha blending needs the immediately underlying pixel. Keep
        // its priority/colour loads out of opaque and brightness-only pixels.
        const bool semi_obj=!gx_front && (candidates.front_layer()[i]&0x80);
        bool blended=false;
        if(semi_obj || (gx_front && native_blend_possible) || ((window&32) && mode==1 && (scene.blend&(1u<<layer)))){
            unsigned back_layer=(gx_front?candidates.front_layer()[i]:candidates.back_layer()[i])&0x7f;
            const bool gx_back=!gx_front && gx_visible && geometry_before(back_layer,candidates.back_priority()[i]);
            if(gx_back)back_layer=0;
            if(scene.blend&(1u<<(8+back_layer))){
                const uint32_t back=gx_front?candidate_front.px[y][x]:(gx_back?gx:candidates.back.px[y][x]);
                if(gx_front)front=native_geometry_blend(front,back);
                else if(semi_obj && candidates.front_obj_alpha()[i]<=16)
                    front=AlphaBlend(candidates.front_obj_alpha()[i],16-candidates.front_obj_alpha()[i])(front,back);
                else front=blend_pixels(front,back);
                blended=true;
            }
        }
        if(!blended && (window&32) && (mode==2||mode==3) && (scene.blend&(1u<<layer)))front=apply_brightness(front,layer_lut);
        if(master_mode==1 || master_mode==2)
            front=apply_brightness(front,master_lut);
        output.px[y][x]=front|0xff000000u;
    }
}

void ppu_compose_prepared(const PpuPreparedScene& scene,const Framebuffer& geometry,Framebuffer& output) {
    if(scene.resolved){output=scene.front;return;}
    compose_candidates({scene,scene.front},geometry,output,true);
}
void ppu_compose_candidates(PpuPreparedCandidates candidates,const Framebuffer& geometry,
                            Framebuffer& output,bool master) {
    compose_candidates(candidates,geometry,output,master);
    // minigames-30fps-min: windowless planes + binary window cut.
    if(const auto* cut=candidates.state.window_cut)for(unsigned r=0;r<cut->count;++r){
        const auto& rect=cut->rects[r];
        for(unsigned y=rect.y0;y<rect.y1;++y)std::fill_n(&output.px[y][rect.x0],rect.x1-rect.x0,cut->color);
    }
}
bool ppu_windows_binary(Engine eng){return windows_binary(eng);}
bool ppu_window_cut_unchanged(Engine eng){
    WindowCutScope scope;
    return window_cut_signature(eng)==g_window_cut_issued[eng];
}

// Preserve the original entry point and its render/blanking behavior.
void ppu_scanout_scene(Engine eng,Framebuffer& fb) {
    const auto disp=rd32(kEngines[eng].reg_base);
    static Framebuffer geometry;
    const bool visible=!(disp&0x80) && ((disp>>16)&3)!=0;
    if(visible && eng==ENGINE_A && (disp&8) && (disp&(1u<<8))) {
        std::memset(&geometry,0,sizeof geometry);
        gx_render(geometry);
    }
    ppu_scanout_scene_with_geometry(eng,fb,geometry);
}

bool ppu_capture_view(Engine eng,PpuCaptureView& view){
    if(eng!=ENGINE_B)return false;
    static const bool disabled=std::getenv("PLAYER_NO_GPU_CAPTURE")!=nullptr;
    if(disabled)return false;
    const uint32_t reg=kEngines[eng].reg_base;
    const uint32_t dispcnt=rd32(reg);
    if(((dispcnt>>16)&3)!=1 || (dispcnt&0x80) || (dispcnt&0xe000))return false;
    auto rd8=[](uint32_t a){return *static_cast<volatile uint8_t*>(host_ptr(a));};
    const unsigned layers=(dispcnt>>8)&0x1f;
    const uint16_t blend=rd16(reg+0x50);
    const unsigned effect=(blend>>6)&3;
    view.master=rd16(reg+0x6c);
    if(layers==0x04){
        // BG2 extended direct-colour bitmap, 256x256, identity affine map.
        if((dispcnt&7)!=5 || (effect && (blend&0x04)))return false;
        const uint16_t cnt=rd16(reg+0x0c);
        if((cnt&0xc4)!=0x84 || (cnt>>14)!=1)return false;
        if(rd16(reg+0x20)!=0x100 || rd16(reg+0x22)!=0 || rd16(reg+0x24)!=0 || rd16(reg+0x26)!=0x100 ||
           (rd32(reg+0x28)&0x0fffffff)!=0 || (rd32(reg+0x2c)&0x0fffffff)!=0)return false;
        // Sub BG = bank C only (H/I would overlay it).
        if((rd8(0x04000242)&0x87)!=0x84 || (rd8(0x04000248)&0x83)==0x81 || (rd8(0x04000249)&0x83)==0x81)return false;
        const unsigned offset=(cnt&0x1f00)<<6;
        if(offset+0x18000>0x20000)return false;
        view.bank=2;view.byte_offset=offset;return true;
    }
    if(layers!=0x10 || (dispcnt&0x60)!=0x20 || (effect && (blend&0x10)))return false;
    // Sub OBJ = bank D only (I would overlay it).
    if((rd8(0x04000243)&0x87)!=0x84 || (rd8(0x04000249)&0x83)==0x82)return false;
    const uint32_t oam=0x07000400u;
    uint32_t cells[24]{};int base=-1,grid_priority=-1,grid_last=-1;
    struct Other {int index,priority;};Other others[128];unsigned other_count=0;
    for(int i=0;i<128;++i){
        const uint16_t a0=rd16(oam+i*8u),a1=rd16(oam+i*8u+2),a2=rd16(oam+i*8u+4);
        const bool affine=a0&0x100;
        if(!affine && (a0&0x200))continue; // disabled
        const int priority=(a2>>10)&3,shape=(a0>>14)&3,size=(a1>>14)&3;
        bool grid=!affine && ((a0>>10)&3)==3 && shape!=3 && !(a1&0x3000) && (a2>>12)==15;
        if(grid){
            const int w=kObjSizes[shape][size][0],h=kObjSizes[shape][size][1];
            const int x=a1&0x1ff,y=a0&0xff;
            const unsigned tile=a2&0x3ff;
            const int at=int((tile&31)*16+(tile&0x3e0)*128)-(y*256+x)*2;
            grid=!(x&7) && !(y&7) && x+w<=256 && y+h<=192 && at>=0 && (base<0 || at==base) &&
                 (grid_priority<0 || priority==grid_priority);
            if(grid){
                base=at;grid_priority=priority;grid_last=i;
                for(int cy=y/8;cy<(y+h)/8;++cy)for(int cx=x/8;cx<(x+w)/8;++cx)cells[cy]|=1u<<cx;
                continue;
            }
        }
        others[other_count++]={i,priority};
    }
    if(base<0 || base+0x18000>0x20000)return false;
    for(unsigned cy=0;cy<24;++cy)if(cells[cy]!=0xffffffffu)return false;
    // Every other OBJ is behind an opaque grid pixel wherever it is visible.
    for(unsigned k=0;k<other_count;++k)
        if(others[k].priority<grid_priority || (others[k].priority==grid_priority && others[k].index<grid_last))return false;
    view.bank=3;view.byte_offset=unsigned(base);return true;
}

bool ppu_write_bmp(const char *path, const Framebuffer &fb) {
    std::FILE *f = std::fopen(path, "wb");
    if (!f) return false;

    const uint32_t stride = SCREEN_W * 3;
    const uint32_t pad = (4 - (stride & 3)) & 3;
    const uint32_t img = (stride + pad) * SCREEN_H;
    const uint32_t off = 54;

    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    const uint32_t total = off + img;
    std::memcpy(hdr + 2, &total, 4);
    std::memcpy(hdr + 10, &off, 4);
    const uint32_t dib = 40;
    std::memcpy(hdr + 14, &dib, 4);
    const int32_t w = SCREEN_W, h = SCREEN_H;
    std::memcpy(hdr + 18, &w, 4);
    std::memcpy(hdr + 22, &h, 4);
    const uint16_t planes = 1, bpp = 24;
    std::memcpy(hdr + 26, &planes, 2);
    std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &img, 4);
    std::fwrite(hdr, 1, sizeof hdr, f);

    const uint8_t zero[3] = {0, 0, 0};
    for (int y = SCREEN_H - 1; y >= 0; --y) {          // BMP rows run bottom-up
        for (int x = 0; x < SCREEN_W; ++x) {
            const uint32_t c = fb.px[y][x];
            const uint8_t bgr[3] = {static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8),
                                    static_cast<uint8_t>(c >> 16)};
            std::fwrite(bgr, 1, 3, f);
        }
        if (pad) std::fwrite(zero, 1, pad, f);
    }
    std::fclose(f);
    return true;
}

}  // namespace ntr
