#include "platform/3ds/native_mesh_api.h"
#include "ntr/clip_sparse_product.h"
namespace bmd_native {size_t triangle_count();void new_frame();void reset();bool build_cpu(ntr::gx_geometry::Packet&,const native_mesh::Packet&);extern uint32_t current_raw_base;}
// DS geometry engine: command decode, matrix stacks, vertex assembly, raster.
//
// Command encoding and parameter counts from GBATEK. Matrices are retained for
// host rendering; the combined clip transform and vertex arithmetic follow the
// DS fixed12 truncation before producing float coordinates for GPU consumers.

#include "ntr/gx.h"
#include "ntr/gx_geometry_packet.h"
#include "ntr/gx_gpu_geometry.h"
#include "ntr/mmio.h"
#include "ntr/fixed_direction_matrix.h"

#include "ntr/texture.h"
#include "ntr/gx_texture_views.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace ntr {
namespace {

constexpr float FX12 = 1.0f / 4096.0f;

// --- matrices ---------------------------------------------------------------
// DS matrices are row-vector convention: v' = v * M, translation in row 3.
// The DS accumulator wraps modulo 2^64. Bits12..43 become the signed32 result.
int32_t fixed12_accumulator_result(uint64_t sum) {
    const uint32_t bits = static_cast<uint32_t>(sum >> 12);
    int32_t result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

struct Mat {
    // raw is authoritative DS fixed12; f() is a derived float view, computed on
    // demand so matrix stack copies stay at 64 bytes.
    int32_t raw[16];
    float f(int i) const {return raw[i]*FX12;}
    void refresh(){}
    static Mat identity(){Mat r{};r.raw[0]=r.raw[5]=r.raw[10]=r.raw[15]=4096;r.refresh();return r;}
};
Mat mul(const Mat& a,const Mat& b){
    Mat r{};
    for(int i=0;i<4;++i)for(int j=0;j<4;++j){
        uint64_t sum = 0;
        for (int k = 0; k < 4; ++k)
            sum += static_cast<uint64_t>(int64_t(a.raw[i*4+k]) * b.raw[k*4+j]);
        r.raw[i*4+j] = fixed12_accumulator_result(sum);
    }
    r.refresh();return r;
}

// Products whose left operand has DS identity rows: the wrapping accumulator
// returns (4096*x)>>12 == x exactly, so those rows are copied and only the
// non-trivial rows are accumulated (gpu-geometry-menu, bit-identical to mul).
Mat mul_translate(const uint32_t* t,const Mat& b){
    Mat r=b;
    for(int j=0;j<4;++j){
        uint64_t sum=static_cast<uint64_t>(4096LL*b.raw[12+j]);
        for(int k=0;k<3;++k)sum+=static_cast<uint64_t>(int64_t(int32_t(t[k]))*b.raw[k*4+j]);
        r.raw[12+j]=fixed12_accumulator_result(sum);
    }
    return r;
}
Mat mul_scale(const uint32_t* s,const Mat& b){
    Mat r=b;
    for(int i=0;i<3;++i)for(int j=0;j<4;++j)
        r.raw[i*4+j]=fixed12_accumulator_result(static_cast<uint64_t>(int64_t(int32_t(s[i]))*b.raw[i*4+j]));
    return r;
}
// m: rows 0..2 have column 3 == 0; row 3 is (m30, m31, m32, 4096) when affine_row3,
// else (0, 0, 0, 4096) (3x3).
Mat mul_affine(const Mat& m,const Mat& b,bool affine_row3){
    Mat r=b;
    for(int i=0;i<(affine_row3?4:3);++i)for(int j=0;j<4;++j){
        uint64_t sum=i==3?static_cast<uint64_t>(4096LL*b.raw[12+j]):0;
        for(int k=0;k<3;++k)sum+=static_cast<uint64_t>(int64_t(m.raw[i*4+k])*b.raw[k*4+j]);
        r.raw[i*4+j]=fixed12_accumulator_result(sum);
    }
    return r;
}

struct Vec4 { float x, y, z, w; };

// --- engine state -----------------------------------------------------------
enum MtxMode { MTX_PROJ = 0, MTX_POS = 1, MTX_POSVEC = 2, MTX_TEX = 3 };

struct State {
    int mode = MTX_POS;
    Mat proj = Mat::identity();
    Mat pos = Mat::identity();
    Mat vec = Mat::identity();
    Mat tex = Mat::identity();
    int32_t clip[16]{};
    bool clip_dirty = true;

    Mat proj_stack[2];
    Mat pos_stack[32];
    Mat vec_stack[32];
    int proj_sp = 0, pos_sp = 0;

    uint32_t color = 0xFFFFFFFFu;
    float u = 0, v = 0;                 // current TEXCOORD, in texels
    int16_t raw_texcoord[2]{};          // untransformed signed 12.4 coordinates
    int32_t texture_fixed[16]{};
    bool texture_dirty = true;
    const uint32_t *tex_rgba = nullptr; // bound texture (Mat tex above is the
    int tw = 0, th = 0;                 // texture *matrix* -- different thing)
    uint64_t texture_version = 0;
    uint32_t wrap = 3;                  // repeat/flip bits; harness binds keep repeat
    uint32_t texmode = 0;               // TEXIMAGE_PARAM bits 30..31: coord transform
    int prim = -1;                 // BEGIN_VTXS type, -1 when not inside a primitive
    int16_t vx = 0, vy = 0, vz = 0;
    HostVector<GxVertex> strip;   // vertices accumulated in the current primitive
    int strip_parity = 0;

    int vp_x = 0, vp_y = 0, vp_w = SCREEN_W, vp_h = SCREEN_H;

    // DS lighting keeps five-bit materials and signed eleven-bit directions.
    int diffuse[3] = {31, 31, 31}, ambient[3] = {0, 0, 0};
    int emission[3] = {0, 0, 0}, specular[3] = {0, 0, 0};
    bool use_shininess = false;
    uint8_t shininess[128]{};
    struct Light { int dx, dy, dz; int r, g, b; int spec_recip; } lights[4] = {};
    // Per light: diffuse*colour, specular*colour and (ambient<<9)*colour.
    // Integer products distribute exactly, so NORMAL can use them directly.
    struct LightTerms { int diffuse[3], specular[3], ambient[3]; } terms[4] = {};
    bool terms_dirty = true;
    uint32_t light_mask = 0;
    uint32_t polygon_attr = 0x001f0080; // legacy unconfigured front/opaque default
    uint32_t active_polygon_attr = 0x001f0080;

    HostVector<GxTriangle> tris;
};

State g;
bool g_direct=false;
gx_geometry::Packet g_packet;
HostVector<uint32_t> g_strip_ids;
native_mesh::Packet mesh_packet;
gx_geometry::Packet mesh_cpu;
uint64_t mesh_cpu_generation=UINT64_MAX;
HostVector<uint32_t> mesh_strip;
bool mesh_allow=false,mesh_matrix_valid=false;
native_mesh::Convergence mesh_convergence;
uint32_t mesh_matrix=0;
void mesh_fallback(){if(bmd_native::build_cpu(mesh_cpu,mesh_packet))return;if(mesh_cpu_generation!=mesh_packet.generation()){mesh_packet.cpu_packet(mesh_cpu);mesh_cpu_generation=mesh_packet.generation();}}
void mesh_disable(){
 if(!mesh_packet.requested())return;
 mesh_fallback();g_packet=mesh_cpu;g.strip.clear();g_strip_ids.clear();
 for(auto id:mesh_strip){g.strip.push_back(mesh_packet.cpu_vertex(id));g_strip_ids.push_back(bmd_native::current_raw_base+id);}
 mesh_strip.clear();mesh_packet.clear(false);mesh_cpu_generation=UINT64_MAX;
}
void mesh_emit(uint32_t a,uint32_t b,uint32_t c){mesh_packet.triangle(a,b,c,
 {g.tex_rgba,g.tw,g.th,g.wrap,g.active_polygon_attr,g.texture_version});}
void mesh_push(uint32_t id){
 mesh_strip.push_back(id);const size_t n=mesh_strip.size();auto at=[&](size_t i){return mesh_strip[i];};
 switch(g.prim){
 case 0:if(n==3){mesh_emit(at(0),at(1),at(2));mesh_strip.clear();}break;
 case 1:if(n==4){mesh_emit(at(0),at(1),at(2));mesh_emit(at(0),at(2),at(3));mesh_strip.clear();}break;
 case 2:if(n>=3){if((n-3)&1)mesh_emit(at(n-2),at(n-3),at(n-1));else mesh_emit(at(n-3),at(n-2),at(n-1));}break;
 case 3:if(n>=4&&!(n%2)){mesh_emit(at(n-4),at(n-3),at(n-1));mesh_emit(at(n-4),at(n-1),at(n-2));}break;
 }
}

int g_store_count;

uint32_t bgr555_to_argb(uint16_t c) {
    const uint32_t r = c & 0x1F, gg = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
    return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((gg << 3 | gg >> 2) << 8)
           | (b << 3 | b >> 2);
}

// DS toon shading (face-toy): polygon mode 2 with DISP3DCNT bit 1 clear makes
// the rasterizer replace the vertex colour by ToonTable[vertex red] before the
// texture modulation (GBATEK "Toon/Highlight", melonDS RenderToonTable[vr>>1]).
// Latched at BEGIN_VTXS (the title/menu heads use it with a grey ramp table).
// Highlight mode (DISP3DCNT bit 1 set, file-select castle): every vertex colour
// component is the red one and ToonTable[red] is ADDED after the texture
// modulation (melonDS GPU3D_Soft). g_highlight: the GPU geometry path records a
// highlight env (grey base pass + additive table pass, gx_gpu_backend.inc); the
// CPU path only applies the grey base (no additive term).
bool g_toon=false;
bool g_highlight=false;
uint16_t g_toon_table[32];
// menu-regression: BEGIN_VTXS is hot (hundreds per menu frame) and 32 generic
// io_read calls per latch cost ~5 ms/frame of game thread. TOON_TABLE
// (0x04000380-0x040003BF) is a plain IO mirror (io_write -> raw_write, io_read
// -> raw_read: no VRAM/capture/VCOUNT special case), so the latch compares the
// mirror bytes with the last copy and only rebuilds the masked table when they
// changed; g_toon_version then lets toon_env() skip its per-vertex table memcmp.
// Same values as before. GX_TOON_SLOW_LATCH=1 restores the old per-latch reads.
uint16_t g_toon_raw[32];
uint32_t g_toon_version=0;
void toon_latch(){
    static const bool disabled=std::getenv("GX_NO_TOON")!=nullptr; // A/B toggle
    static const bool slow=std::getenv("GX_TOON_SLOW_LATCH")!=nullptr;
    if(slow){
        const bool mode2=!disabled && ((g.active_polygon_attr>>4)&3)==2 && io_ready();
        const bool hl=mode2 && (io_read(0x04000060,2)&2);
        g_toon=mode2&&!hl;g_highlight=hl;
        if(!mode2)return;
        for(unsigned i=0;i<32;++i)g_toon_table[i]=static_cast<uint16_t>(io_read(0x04000380+2*i,2)&0x7fff);
        ++g_toon_version;return;
    }
    // DISP3DCNT (0x04000060) is a plain mirror register too (io_read -> raw_read).
    const bool mode2=!disabled && ((g.active_polygon_attr>>4)&3)==2 && io_ready();
    const bool hl=mode2 && (*static_cast<const volatile uint8_t*>(host_ptr(0x04000060u))&2);
    g_toon=mode2&&!hl;g_highlight=hl;
    if(!mode2)return;
    const void* mirror=host_ptr(0x04000380u);
    if(!std::memcmp(g_toon_raw,mirror,sizeof g_toon_raw) && g_toon_version)return;
    std::memcpy(g_toon_raw,mirror,sizeof g_toon_raw);
    for(unsigned i=0;i<32;++i)g_toon_table[i]=static_cast<uint16_t>(g_toon_raw[i]&0x7fff);
    ++g_toon_version;
}
inline uint16_t toon_bgr555(uint16_t c){return g_toon_table[c&31];}
inline uint32_t toon_argb(uint32_t c){return (c&0xff000000u)|(bgr555_to_argb(g_toon_table[(c>>19)&31])&0x00ffffffu);}
inline uint32_t grey_red_argb(uint32_t c){const uint32_t r=(c>>16)&255;return (c&0xff000000u)|(r<<16)|(r<<8)|r;}

int signed11(int v) {
    v &= 2047;
    return v >= 1024 ? v - 2048 : v;
}

// LIGHT_VECTOR latches the current directional matrix; NORMAL uses the matrix
// at its own command. Hardware truncates to signed 11 bits, without normalizing.
void lighting_vector(uint32_t packed, int out[3], bool negate) {
    int raw[3];for(int k=0;k<3;++k){raw[k]=(packed>>(10*k))&1023;if(raw[k]>=512)raw[k]-=1024;}
    for(int j=0;j<3;++j){int64_t sum=0;for(int k=0;k<3;++k)sum+=int64_t(raw[k])*g.vec.raw[k*4+j];const int value=int(sum>>12);const unsigned wrapped=unsigned(negate?-value:value)&2047u;out[j]=wrapped>=1024?int(wrapped)-2048:int(wrapped);}

}

void ensure_mesh_clip(){
    if(g.clip_dirty) {
        clip_sparse::multiply(g.pos.raw,g.proj.raw,g.clip);
        g.clip_dirty=false;
    }
    if(mesh_packet.requested()&&!mesh_matrix_valid){mesh_matrix=mesh_packet.matrix(g.clip);mesh_matrix_valid=true;}
}

// Project a model-space vertex all the way to screen space.
GxVertex project(int16_t x, int16_t y, int16_t z) {
    // Hardware combines position and projection in fixed12 BEFORE vertex
    // multiplication. Two floating-point transforms move the letter's left
    // edge across an integer pixel boundary midway down the screen.
    ensure_mesh_clip();
    auto component=[&](int j) {
        const int64_t sum=static_cast<int64_t>(x)*g.clip[j]
            +static_cast<int64_t>(y)*g.clip[4+j]
            +static_cast<int64_t>(z)*g.clip[8+j]
            +4096LL*g.clip[12+j];
        return static_cast<int32_t>(sum>>12)*FX12;
    };
    const Vec4 c{component(0),component(1),component(2),component(3)};

    GxVertex out{};
    out.w = c.w;
    out.cx = c.x; out.cy = c.y; out.cz = c.z; out.cw = c.w;
    const float iw = (std::fabs(c.w) > 1e-6f) ? 1.0f / c.w : 0.0f;
    // DS screen y runs bottom-up; the framebuffer is top-down.
    out.x = (c.x * iw + 1.0f) * 0.5f * g.vp_w + g.vp_x;
    out.y = (1.0f - (c.y * iw + 1.0f) * 0.5f) * g.vp_h + g.vp_y;
    out.z = (c.z * iw + 1.0f) * 0.5f;
    out.u = g.u;
    out.v = g.v;
    out.color = g_toon ? toon_argb(g.color) : g_highlight ? grey_red_argb(g.color) : g.color;
    return out;
}

int wrap_coord(int i, int size, bool repeat, bool flip) {
    if (!repeat) return i < 0 ? 0 : (i >= size ? size - 1 : i);
    if (!flip) {
        i %= size;
        return i < 0 ? i + size : i;
    }
    const int period = size * 2;
    i %= period;
    if (i < 0) i += period;
    return i < size ? i : period - 1 - i;
}

void emit_tri(const GxVertex &a,const GxVertex &b,const GxVertex &c){
    if(!g_direct){
    GxTriangle t;
    t.v[0] = a; t.v[1] = b; t.v[2] = c;
    t.tex = g.tex_rgba; t.tw = g.tw; t.th = g.th; t.wrap = g.wrap;
    t.texture_version = g.texture_version;
    t.polygon_attr = g.active_polygon_attr;
    g.tris.push_back(t);

        return;
    }
    const auto* base=g.strip.data();
    g_packet.triangle(g_strip_ids.at(&a-base),g_strip_ids.at(&b-base),g_strip_ids.at(&c-base),
        {g.tex_rgba,g.tw,g.th,g.wrap,g.active_polygon_attr,g.texture_version});
}

// Assemble according to the active BEGIN_VTXS primitive type.
void push_vertex(const GxVertex &v) {
    g.strip.push_back(v);if(g_direct)g_strip_ids.push_back(g_packet.vertex(v));
    const size_t n = g.strip.size();
    switch (g.prim) {
        case 0:                                        // separate triangles
            if (n == 3) { emit_tri(g.strip[0], g.strip[1], g.strip[2]); g.strip.clear();g_strip_ids.clear(); }
            break;
        case 1:                                        // separate quads
            if (n == 4) {
                emit_tri(g.strip[0], g.strip[1], g.strip[2]);
                emit_tri(g.strip[0], g.strip[2], g.strip[3]);
                g.strip.clear();g_strip_ids.clear();
            }
            break;
        case 2:                                        // triangle strip
            if (n >= 3) {
                const GxVertex &p0 = g.strip[n - 3], &p1 = g.strip[n - 2], &p2 = g.strip[n - 1];
                if ((n - 3) & 1) emit_tri(p1, p0, p2);  // alternate winding
                else emit_tri(p0, p1, p2);
            }
            break;
        case 3:                                        // quad strip
            if (n >= 4 && (n % 2) == 0) {
                const GxVertex &p0 = g.strip[n - 4], &p1 = g.strip[n - 3];
                const GxVertex &p2 = g.strip[n - 2], &p3 = g.strip[n - 1];
                emit_tri(p0, p1, p3);
                emit_tri(p0, p3, p2);
            }
            break;
        default: break;
    }
}

// DS generated coordinates are signed16 with four fractional bits. NORMAL
// latches mode2 coordinates; mode3 instead evaluates each raw vertex. The
// texture matrix translation applies only to mode1 (TEXCOORD source).
void generated_texcoord(int x,int y,int z,int w,unsigned shift,bool add_raw,
                        float& u,float& v) {
    if(g.texture_dirty){
        for(unsigned i=0;i<16;++i)g.texture_fixed[i]=g.tex.raw[i];
        g.texture_dirty=false;
    }
    float result[2];
    for(unsigned j=0;j<2;++j){
        const int64_t sum=int64_t(x)*g.texture_fixed[j]+int64_t(y)*g.texture_fixed[4+j]
            +int64_t(z)*g.texture_fixed[8+j]+int64_t(w)*g.texture_fixed[12+j];
        const int16_t fixed=static_cast<int16_t>((sum>>shift)+(add_raw?g.raw_texcoord[j]:0));
        result[j]=fixed/16.f;
    }
    u=result[0];v=result[1];
}

#include "gx_gpu_record.inc" // GPU geometry mode (gpu-geometry-menu)

void vertex(int16_t x, int16_t y, int16_t z) {
    g.vx = x; g.vy = y; g.vz = z;
    if (g.prim >= 0) {
        if(gpu_rec::active && gpu_rec::vertex(x,y,z))return;
        if(mesh_packet.requested()){
            ensure_mesh_clip();
            if(g.vp_x!=0||g.vp_y!=0||g.vp_w!=256||g.vp_h!=192)mesh_packet.refuse(native_mesh::Refusal::Viewport);
            if(g.proj.f(3)!=0.f||g.proj.f(7)!=0.f||g.proj.f(11)==0.f||g.proj.f(15)!=0.f)mesh_packet.refuse(native_mesh::Refusal::Projection);
            float u=g.u,v=g.v;if(g.texmode==3)generated_texcoord(x,y,z,0,24,true,u,v);
            const auto id=mesh_packet.vertex({x,y,z,g.color,u,v,mesh_matrix,g.vp_x,g.vp_y,g.vp_w,g.vp_h});
            mesh_push(id);return;
        }
        auto v=project(x,y,z);
        if(g.texmode==3)generated_texcoord(x,y,z,0,24,true,v.u,v.v);
        push_vertex(v);
    }
}

// --- command execution ------------------------------------------------------
void load_mtx(Mat& dst,const uint32_t* p,int n){
    Mat m=Mat::identity();
    if(n==16){for(int i=0;i<16;++i)m.raw[i]=static_cast<int32_t>(p[i]);}
    else{for(int r=0;r<4;++r)for(int c=0;c<3;++c)m.raw[r*4+c]=static_cast<int32_t>(p[r*3+c]);m.raw[3]=m.raw[7]=m.raw[11]=0;m.raw[15]=4096;}
    m.refresh();dst=m;
}
Mat mat_3x3(const uint32_t* p){Mat m=Mat::identity();for(int r=0;r<3;++r)for(int c=0;c<3;++c)m.raw[r*4+c]=static_cast<int32_t>(p[r*3+c]);m.refresh();return m;}

void exec(uint8_t cmd, const uint32_t *p, int np) {
    if(gpu_rec::active)gpu_rec::command(cmd,p);
    if(cmd>=0x10 && cmd<=0x1c){g.clip_dirty=true;g.texture_dirty=true;mesh_matrix_valid=false;}
    (void)np;
    switch (cmd) {
        case 0x00: break;                                        // NOP
        case 0x10: g.mode = p[0] & 3; break;                     // MTX_MODE
        case 0x11:                                               // MTX_PUSH
            if (g.mode == MTX_PROJ) { if (g.proj_sp < 1) g.proj_stack[g.proj_sp++] = g.proj; }
            else if (g.pos_sp < 31) { g.pos_stack[g.pos_sp] = g.pos; g.vec_stack[g.pos_sp] = g.vec; ++g.pos_sp; }
            break;
        case 0x12: {                                             // MTX_POP
            int n = static_cast<int32_t>(p[0] << 26) >> 26;      // signed 6-bit
            if (g.mode == MTX_PROJ) { if (g.proj_sp > 0) g.proj = g.proj_stack[--g.proj_sp]; }
            else { g.pos_sp -= n; if (g.pos_sp < 0) g.pos_sp = 0;
                   if (g.pos_sp < 31) { g.pos = g.pos_stack[g.pos_sp]; g.vec = g.vec_stack[g.pos_sp]; } }
            break;
        }
        case 0x13: {                                             // MTX_STORE
            ++g_store_count;
            const int i = p[0] & 31;
            if (g.mode == MTX_PROJ) g.proj_stack[0] = g.proj;
            else { g.pos_stack[i] = g.pos; g.vec_stack[i] = g.vec; }
            break;
        }
        case 0x14: {                                             // MTX_RESTORE
            const int i = p[0] & 31;
            if (g.mode == MTX_PROJ) g.proj = g.proj_stack[0];
            else { g.pos = g.pos_stack[i]; g.vec = g.vec_stack[i]; }
            break;
        }
        case 0x15:                                               // MTX_IDENTITY
            if (g.mode == MTX_PROJ) g.proj = Mat::identity();
            else if (g.mode == MTX_TEX) g.tex = Mat::identity();
            else { g.pos = Mat::identity(); if (g.mode == MTX_POSVEC) g.vec = Mat::identity(); }
            break;
        case 0x16: case 0x17: {                                  // MTX_LOAD_4x4 / 4x3
            Mat m; load_mtx(m, p, cmd == 0x16 ? 16 : 12);
            if (g.mode == MTX_PROJ) g.proj = m;
            else if (g.mode == MTX_TEX) g.tex = m;
            else { g.pos = m; if (g.mode == MTX_POSVEC) g.vec = m; }
            break;
        }
        case 0x18: case 0x19: case 0x1A: {                       // MTX_MULT_4x4 / 4x3 / 3x3
            Mat m;
            if (cmd == 0x1A) m = mat_3x3(p);
            else load_mtx(m, p, cmd == 0x18 ? 16 : 12);
            auto product=[&](const Mat& b){return cmd==0x18?mul(m,b):mul_affine(m,b,cmd==0x19);};
            if (g.mode == MTX_PROJ) g.proj = product(g.proj);
            else if (g.mode == MTX_TEX) g.tex = product(g.tex);
            else { g.pos = product(g.pos); if (g.mode == MTX_POSVEC) g.vec = product(g.vec); }
            break;
        }
        case 0x1B: {                                             // MTX_SCALE
            if (g.mode == MTX_PROJ) g.proj = mul_scale(p, g.proj);
            else if (g.mode == MTX_TEX) g.tex = mul_scale(p, g.tex);
            else g.pos = mul_scale(p, g.pos);
            break;
        }
        case 0x1C: {                                             // MTX_TRANS
            if (g.mode == MTX_PROJ) g.proj = mul_translate(p, g.proj);
            else if (g.mode == MTX_TEX) g.tex = mul_translate(p, g.tex);
            else { g.pos = mul_translate(p, g.pos); if (g.mode == MTX_POSVEC) g.vec = mul_translate(p, g.vec); }
            break;
        }
        case 0x20: g.color = bgr555_to_argb(static_cast<uint16_t>(p[0] & 0x7FFF)); break;
        case 0x21: {                                             // NORMAL
            if(gpu_rec::active){gpu_rec::normal(p[0]);break;}
            if(g.texmode==2){
                int raw[3];
                for(unsigned i=0;i<3;++i){raw[i]=(p[0]>>(10*i))&1023;if(raw[i]>=512)raw[i]-=1024;}
                generated_texcoord(raw[0],raw[1],raw[2],0,21,true,g.u,g.v);
            }
            if (g.terms_dirty) {
                for (int i = 0; i < 4; ++i) {
                    const State::Light &L = g.lights[i];
                    const int lc[3] = {L.r, L.g, L.b};
                    for (int k = 0; k < 3; ++k) {
                        g.terms[i].diffuse[k] = g.diffuse[k] * lc[k];
                        g.terms[i].specular[k] = g.specular[k] * lc[k];
                        g.terms[i].ambient[k] = (g.ambient[k] << 9) * lc[k];
                    }
                }
                g.terms_dirty = false;
            }
            int normal[3];
            lighting_vector(p[0], normal, false);
            uint32_t c[3] = {uint32_t(g.emission[0]) << 14,
                             uint32_t(g.emission[1]) << 14,
                             uint32_t(g.emission[2]) << 14};
            for (int i = 0; i < 4; ++i) {
                if (!((g.light_mask >> i) & 1)) continue;
                const State::Light &L = g.lights[i];
                const int dot = ((L.dx * normal[0]) >> 9)
                              + ((L.dy * normal[1]) >> 9)
                              + ((L.dz * normal[2]) >> 9);
                int shine = 0;
                if (dot > 0) {
                    const int halfway = signed11(dot + normal[2]);
                    const int square = ((halfway * halfway) >> 10) & 1023;
                    shine = ((square * L.spec_recip) >> 8) - 512;
                    if (shine < 0) shine = 0;
                    else {
                        // The hardware wraps to signed14 before saturation.
                        shine &= 16383;
                        if (shine >= 8192) shine -= 16384;
                        shine = std::max(0, std::min(511, shine));
                    }
                }
                // Table entry zero also applies to unlit/back-facing normals.
                if (g.use_shininess) shine = int(g.shininess[shine >> 2]) << 1;
                const State::LightTerms &T = g.terms[i];
                if (dot > 0) {
                    const int d = signed11(dot);
                    for (int k = 0; k < 3; ++k) c[k] += (T.diffuse[k] * d) & 0xfffff;
                }
                for (int k = 0; k < 3; ++k) c[k] += T.specular[k] * shine + T.ambient[k];
            }
            uint16_t color = 0;
            for (int k = 0; k < 3; ++k) {
                const uint32_t value = c[k] >> 14;
                color |= (value > 31 ? 31 : value) << (5 * k);
            }
            g.color = bgr555_to_argb(color);
            break;
        }
        case 0x22: {                                             // TEXCOORD (1.4 fx)
            if(gpu_rec::active){gpu_rec::texcoord(p[0]);break;}
            g.raw_texcoord[0]=static_cast<int16_t>(p[0]&0xffff);
            g.raw_texcoord[1]=static_cast<int16_t>(p[0]>>16);
            if (g.texmode == 1) {
                generated_texcoord(g.raw_texcoord[0],g.raw_texcoord[1],1,1,12,false,g.u,g.v);
            } else {
                g.u = g.raw_texcoord[0]/16.f;
                g.v = g.raw_texcoord[1]/16.f;
            }
            break;
        }
        case 0x23:                                               // VTX_16
            vertex(static_cast<int16_t>(p[0] & 0xFFFF), static_cast<int16_t>(p[0] >> 16),
                   static_cast<int16_t>(p[1] & 0xFFFF));
            break;
        case 0x24: {                                             // VTX_10  (s4.6 -> 4.12)
            auto s10 = [](uint32_t v) { const int x=int(v&1023u); return static_cast<int16_t>((x>=512?x-1024:x)*64); };
            vertex(s10(p[0] & 0x3FF), s10((p[0] >> 10) & 0x3FF), s10((p[0] >> 20) & 0x3FF));
            break;
        }
        case 0x25: vertex(static_cast<int16_t>(p[0] & 0xFFFF), static_cast<int16_t>(p[0] >> 16), g.vz); break;
        case 0x26: vertex(static_cast<int16_t>(p[0] & 0xFFFF), g.vy, static_cast<int16_t>(p[0] >> 16)); break;
        case 0x27: vertex(g.vx, static_cast<int16_t>(p[0] & 0xFFFF), static_cast<int16_t>(p[0] >> 16)); break;
        case 0x28: {                                             // VTX_DIFF
            auto d10 = [](uint32_t v) { return static_cast<int32_t>(v << 22) >> 22; };
            vertex(static_cast<int16_t>(g.vx + d10(p[0] & 0x3FF)),
                   static_cast<int16_t>(g.vy + d10((p[0] >> 10) & 0x3FF)),
                   static_cast<int16_t>(g.vz + d10((p[0] >> 20) & 0x3FF)));
            break;
        }
        case 0x29: g.polygon_attr = p[0]; break;                  // POLYGON_ATTR
        case 0x2A: gx_teximage_param(p[0]); break;               // TEXIMAGE_PARAM
        case 0x2B: gx_pltt_base(p[0]); break;                    // PLTT_BASE
        case 0x30: {                                             // DIF_AMB
            auto unpack = [](uint32_t v, int *o) {
                o[0] = (v & 0x1F);
                o[1] = ((v >> 5) & 0x1F);
                o[2] = ((v >> 10) & 0x1F);
            };
            g.terms_dirty = true;
            unpack(p[0] & 0x7FFF, g.diffuse);
            unpack((p[0] >> 16) & 0x7FFF, g.ambient);
            // Bit 15 sets the vertex colour to the diffuse colour immediately,
            // which is what an unlit polygon then draws with.
            if ((p[0] >> 15) & 1) {
                g.color = bgr555_to_argb(p[0] & 0x7fff);
            }
            break;
        }
        case 0x31: {                                             // SPE_EMI
            g.terms_dirty = true;
            g.specular[0] = p[0] & 31;
            g.specular[1] = (p[0] >> 5) & 31;
            g.specular[2] = (p[0] >> 10) & 31;
            g.use_shininess = (p[0] & 0x8000) != 0;
            g.emission[0] = ((p[0] >> 16) & 0x1F);
            g.emission[1] = ((p[0] >> 21) & 0x1F);
            g.emission[2] = ((p[0] >> 26) & 0x1F);
            break;
        }
        case 0x32: {                                             // LIGHT_VECTOR
            const int i = (p[0] >> 30) & 3;
            int direction[3];
            lighting_vector(p[0], direction, true);
            g.lights[i].dx = direction[0];
            g.lights[i].dy = direction[1];
            g.lights[i].dz = direction[2];
            // For the reciprocal, sign-extension precedes negation. At the
            // signed11 boundary this differs from the diffuse direction.
            lighting_vector(p[0], direction, false);
            const int denominator = 512 - direction[2];
            g.lights[i].spec_recip = denominator ? 262144 / denominator : 0;
            break;
        }
        case 0x33: {                                             // LIGHT_COLOR
            const int i = (p[0] >> 30) & 3;
            g.terms_dirty = true;
            g.lights[i].r = (p[0] & 0x1F);
            g.lights[i].g = ((p[0] >> 5) & 0x1F);
            g.lights[i].b = ((p[0] >> 10) & 0x1F);
            break;
        }
        case 0x34:                                              // SHININESS
            for (unsigned i = 0; i < 128; ++i)
                g.shininess[i] = (p[i / 4] >> (8 * (i % 4))) & 255;
            break;
        case 0x40:                                               // BEGIN_VTXS
            g.prim = p[0] & 3;
            g.active_polygon_attr = g.polygon_attr;
            g.light_mask = g.active_polygon_attr & 15;
            toon_latch();
            g.strip.clear();g_strip_ids.clear();mesh_strip.clear();
            g.strip_parity = 0;
            break;
        case 0x41: g.prim = -1; g.strip.clear();g_strip_ids.clear();mesh_strip.clear(); break;          // END_VTXS
        case 0x50: break;                                        // SWAP_BUFFERS
        case 0x60:                                               // VIEWPORT
            g.vp_x = p[0] & 0xFF;
            g.vp_y = (p[0] >> 8) & 0xFF;
            g.vp_w = ((p[0] >> 16) & 0xFF) - g.vp_x + 1;
            g.vp_h = ((p[0] >> 24) & 0xFF) - g.vp_y + 1;
            break;
        default: break;
    }
}

// GBATEK parameter counts, indexed by command byte.
int param_count_slow(uint8_t cmd) {
    switch (cmd) {
        case 0x00: case 0x11: case 0x15: case 0x41: return 0;
        case 0x16: case 0x18: return 16;
        case 0x17: case 0x19: return 12;
        case 0x1A: return 9;
        case 0x1B: case 0x1C: case 0x70: return 3;
        case 0x23: case 0x71: return 2;
        case 0x34: return 32;
        default: return 1;
    }
}

const auto g_param_counts=[]{struct T{uint8_t n[256];} t{};for(int i=0;i<256;++i)t.n[i]=uint8_t(param_count_slow(uint8_t(i)));return t;}();
inline int param_count(uint8_t cmd) { return g_param_counts.n[cmd]; }

// --- packed FIFO state machine ---------------------------------------------
uint8_t g_queue[4];
int g_queued = 0, g_qpos = 0;
uint32_t g_params[32];
int g_have = 0;

void feed(uint32_t word) {
    if (g_queued == 0) {                       // expecting a command word
        for (int i = 0; i < 4; ++i) g_queue[i] = static_cast<uint8_t>(word >> (i * 8));
        g_queued = 4;
        g_qpos = 0;
        g_have = 0;
        // Commands taking no parameters execute immediately.
        while (g_qpos < g_queued && param_count(g_queue[g_qpos]) == 0) {
            exec(g_queue[g_qpos], nullptr, 0);
            ++g_qpos;
        }
        if (g_qpos >= g_queued) g_queued = 0;
        return;
    }
    const uint8_t cmd = g_queue[g_qpos];
    g_params[g_have++] = word;
    if (g_have >= param_count(cmd)) {
        exec(cmd, g_params, g_have);
        g_have = 0;
        ++g_qpos;
        while (g_qpos < g_queued && param_count(g_queue[g_qpos]) == 0) {
            exec(g_queue[g_qpos], nullptr, 0);
            ++g_qpos;
        }
        if (g_qpos >= g_queued) g_queued = 0;
    }
}

// --- direct port writes -----------------------------------------------------
uint8_t g_port_cmd = 0;
uint32_t g_port_params[32];
int g_port_have = 0;

}  // namespace

void gx_stream_note(uint32_t w);
void gx_write_fifo(uint32_t word) { if(!gpu_rec::active)gx_stream_note(word); feed(word); }

void gx_set_matrix_slot(int slot, const float m[16]) {
    if (slot < 0 || slot >= 32) return;
    Mat mm;
    for(int i=0;i<16;++i){const float scaled=m[i]*4096.f;const int32_t whole=int32_t(scaled);const float fraction=scaled-float(whole);mm.raw[i]=whole+(fraction>=.5f)-(fraction<=-.5f);}
    mm.refresh();
    g.pos_stack[slot] = mm;gpu_rec::invalidate_slots();gpu_rec::entry=-1;
    g.vec_stack[slot] = mm;
}

void gx_set_material(uint32_t dif_amb, uint32_t spe_emi) {
    exec(0x30, &dif_amb, 1);
    exec(0x31, &spe_emi, 1);
}

void gx_set_light(int index, float dx, float dy, float dz, uint32_t bgr555) {
    if (index < 0 || index > 3) return;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-6f) { dx /= len; dy /= len; dz /= len; }
    // Direct harness helper takes an eye-space direction, unlike LIGHT_VECTOR.
    g.lights[index] = {static_cast<int>(-dx * 512), static_cast<int>(-dy * 512),
                       static_cast<int>(-dz * 512), int(bgr555 & 31),
                       int((bgr555 >> 5) & 31), int((bgr555 >> 10) & 31), 0};
    const int denominator = 512 - static_cast<int>(dz * 512);
    g.lights[index].spec_recip = denominator ? 262144 / denominator : 0;
    g.terms_dirty = true;gpu_rec::env=-1;
}

void gx_enable_lights(uint32_t mask) {
    g.light_mask = mask & 0xF;gpu_rec::env=-1;
    g.polygon_attr = (g.polygon_attr & ~15u) | g.light_mask;
}

void gx_bind_texture(const uint32_t *rgba, int width, int height) {
    g.tex_rgba = rgba;
    g.texture_version = 0;
    g.tw = width;
    g.th = height;
}

// --- VRAM-sourced texturing: the game path ----------------------------------
// The game binds textures by writing TEXIMAGE_PARAM / PLTT_BASE around each
// display list; the texel data was already uploaded through GX::LoadTex into
// the mapped texture-slot window and palettes through GX::LoadTexPltt. On
// hardware the slot addresses come from bank assignment; the port's HAL
// points the game's upload-base globals at these same windows, so a slot
// offset here is a plain host address.
namespace {

constexpr uintptr_t TEX_SLOT_BASE  = 0x06800000u;   // texture slots, mapped
constexpr uintptr_t PLTT_SLOT_BASE = 0x06880000u;   // palette slots, mapped

uint32_t g_teximage, g_plttbase;
struct CachedTexture {
    HostVector<uint32_t> pixels;
    HostVector<uint8_t> data, index, palette;
    size_t palette_offset = 0;
    uint64_t used_frame = 0;
    uint64_t verified_frame = UINT64_MAX; // content compared against VRAM this frame
    uint64_t version = 0;
    size_t bytes() const {
        return pixels.size()*sizeof(uint32_t)+data.size()+index.size()+palette.size();
    }
    bool matches(const TextureDesc& d) const {
        return (data.empty() || !std::memcmp(data.data(),d.data,data.size())) &&
               (index.empty() || !std::memcmp(index.data(),d.index,index.size())) &&
               (palette.empty() || !std::memcmp(palette.data(),d.pal+palette_offset,palette.size()));
    }
    void capture(const TextureDesc& d) {
        const size_t n=static_cast<size_t>(d.width)*d.height;
        const size_t encoded=d.format==7?n*2:d.format==3?n/2:
                             (d.format==2 || d.format==5)?n/4:n;
        const size_t available=std::max<int32_t>(d.data_len,0);
        data.assign(d.data,d.data+std::min(encoded,available));
        size_t palette_end=d.format==1?64:d.format==2?8:d.format==3?32:
                           d.format==4?512:d.format==6?16:0;
        if(d.format==5) {
            index.assign(d.index,d.index+encoded/2); // decoder already validated length
            palette_offset=static_cast<size_t>(std::max<int32_t>(d.pal_len,0));
            for(size_t i=0;i<index.size();i+=2) {
                const unsigned info=index[i]|(unsigned(index[i+1])<<8);
                const size_t start=(info&0x3fff)*4u;
                const unsigned mode=info>>14;
                const size_t end=start+(mode==0?6:mode==2?8:4);
                palette_offset=std::min(palette_offset,start);
                palette_end=std::max(palette_end,end);
            }
        }
        palette_end=std::min(palette_end,static_cast<size_t>(std::max<int32_t>(d.pal_len,0)));
        if(palette_end>palette_offset)
            palette.assign(d.pal+palette_offset,d.pal+palette_end);
    }
};
using CacheItem=std::pair<const uint64_t,CachedTexture>;
std::multimap<uint64_t,CachedTexture,std::less<uint64_t>,HostAllocator<CacheItem>> g_vram_tex_cache;
uint64_t g_texture_frame;
uint64_t g_texture_version; // Never reset or recycled when cache entries are evicted.
size_t g_texture_bytes;
constexpr size_t texture_cache_budget=8*1024*1024;
// Emitted triangles borrow decoded pixels until the next gx_begin_frame.
// Never evict anything referenced in this frame, even if its working set
// exceeds the budget. Snapshots retained by callers must own their pixels.
void trim_texture_cache() {
    for(auto it=g_vram_tex_cache.begin();g_texture_bytes>texture_cache_budget && it!=g_vram_tex_cache.end();){
        // The previous frame may still be presenting on another core.
        if(it->second.used_frame+1<g_texture_frame){
            g_texture_bytes-=it->second.bytes();
            it=g_vram_tex_cache.erase(it);
        }else ++it;
    }
}

GxTextureViews g_physical_texture_views;

void bind_from_vram() {
    const uint32_t fmt = (g_teximage >> 26) & 7;
    if (fmt == 0) { gx_bind_texture(nullptr, 0, 0); return; }
    // The address selects candidate immutable versions, never proves a hit.
    // Compare every encoded byte, compressed index word and relevant palette
    // byte. Sampling or a hash-only key silently reused stale DS VRAM uploads.
    const bool physical=g_physical_texture_views.publish(io_vram_banks());
    const auto* texture=physical?g_physical_texture_views.texture():static_cast<const uint8_t*>(host_ptr(TEX_SLOT_BASE));
    const auto* palette=physical?g_physical_texture_views.palette():static_cast<const uint8_t*>(host_ptr(PLTT_SLOT_BASE));
    const unsigned palette_bytes=physical?0x20000u:0x18000u;
    TextureDesc d;
    const uint32_t off = (g_teximage & 0xFFFF) << 3;
    d.width = 8 << ((g_teximage >> 20) & 7);
    d.height = 8 << ((g_teximage >> 23) & 7);
    d.format = static_cast<int>(fmt);
    d.color0_transparent = (g_teximage >> 29) & 1;
    d.data = texture + off;
    d.data_len = 0x80000 - static_cast<int32_t>(off);
    // Compressed blocks use slot1's lower index half for slots0/1 and upper
    // half for slots2/3. A linear off/2 incorrectly walks beyond slot1.
    if (fmt == 5) {
        const uint32_t encoded=uint32_t(d.width)*uint32_t(d.height)/4;
        auto index_address=[](uint32_t address){
            return 0x20000u+((address&0x1fffcu)>>1)+(address>=0x40000u?0x10000u:0u);
        };
        if(off/0x20000u!=1 && encoded<=0x20000u-(off&0x1ffffu)){
            // The ordinary single-slot case needs no staging allocation/copy.
            d.index=texture+index_address(off);
        }else{
            // Hardware wraps texel addresses at512KiB and reads zero texels
            // from slot1. Flatten these exceptional spans for the decoder
            // and its exact immutable cache snapshots.
            static HostVector<uint8_t> blocks,indices;
            blocks.resize(encoded);indices.resize(encoded/2);
            for(uint32_t byte=0;byte<encoded;byte+=4){
                const uint32_t address=(off+byte)&0x7ffffu;
                if(address>=0x20000u && address<0x40000u)
                    std::memset(blocks.data()+byte,0,4);
                else std::memcpy(blocks.data()+byte,texture+address,4);
                std::memcpy(indices.data()+byte/2,texture+index_address(address),2);
            }
            d.data=blocks.data();d.index=indices.data();
        }
        d.data_len=encoded;d.index_len=encoded/2;
    }
    if(physical && fmt!=5){
        const unsigned n=unsigned(d.width)*unsigned(d.height);
        const unsigned bytes=fmt==7?n*2:fmt==3?n/2:fmt==2?n/4:n;
        if(bytes>unsigned(d.data_len)){
            static HostVector<uint8_t> wrapped;wrapped.resize(bytes);
            for(unsigned i=0;i<bytes;++i)wrapped[i]=texture[(off+i)&0x7ffff];
            d.data=wrapped.data();d.data_len=bytes;
        }
    }
    const uint32_t pal_off = g_plttbase * (fmt == 2 ? 8u : 16u);
    d.pal = palette + (physical?(pal_off&0x1ffffu):std::min<uint32_t>(pal_off,palette_bytes));
    d.pal_len = physical?palette_bytes:(pal_off<palette_bytes?palette_bytes-static_cast<int32_t>(pal_off):0);
    const uint64_t key=(static_cast<uint64_t>(g_plttbase)<<32)|g_teximage;
    const auto candidates=g_vram_tex_cache.equal_range(key);
    auto it=candidates.first;
    // DS texture VRAM is only writable while LCDC-mapped, i.e. not while it
    // backs textures, so one exact comparison per geometry frame suffices.
    for(;it!=candidates.second;++it)if(it->second.verified_frame==g_texture_frame || it->second.matches(d))break;
    if(it==candidates.second) {
        HostVector<uint32_t> rgba;
        if (!texture_decode(d, rgba)) { gx_bind_texture(nullptr, 0, 0); return; }
        CachedTexture cached;
        cached.pixels=std::move(rgba);
        cached.capture(d);
        cached.used_frame=g_texture_frame;
        cached.version=++g_texture_version;
        g_texture_bytes+=cached.bytes();
        it = g_vram_tex_cache.emplace(key,std::move(cached));
    }
    it->second.used_frame=g_texture_frame;
    it->second.verified_frame=g_texture_frame;
    const int w = 8 << ((g_teximage >> 20) & 7), h = 8 << ((g_teximage >> 23) & 7);
    gx_bind_texture(it->second.pixels.data(), w, h);
    g.texture_version=it->second.version;
    trim_texture_cache();
}

}  // namespace

// Texture binds run per primitive; read the diagnostic switch once.
static bool trace_textures() {
    static const bool enabled = std::getenv("NTR_TRACE_TEX") != nullptr;
    return enabled;
}

void gx_teximage_param(uint32_t v) {
    if (trace_textures()) {
        static uint32_t seen[64]; static int ns;
        bool dup = false;
        for (int i = 0; i < ns; ++i) dup |= seen[i] == v;
        if (!dup && ns < 64) {
            seen[ns++] = v;
            std::fprintf(stderr, "tex %08x: %dx%d fmt %u repeat %c%c flip %c%c c0t %u\n", v,
                8 << ((v >> 20) & 7), 8 << ((v >> 23) & 7), (v >> 26) & 7,
                (v & 0x10000) ? 'S' : '-', (v & 0x20000) ? 'T' : '-',
                (v & 0x40000) ? 'S' : '-', (v & 0x80000) ? 'T' : '-', (v >> 29) & 1);
        }
    }
    g_teximage = v;
    g.wrap = (v >> 16) & 0xF;
    g.texmode = (v >> 30) & 3;
    if(gpu_rec::active){gpu_rec::tex_dirty=true;return;} // bound before the next polygon
    bind_from_vram();
}
void gx_pltt_base(uint32_t v) {
    if (trace_textures() && v != g_plttbase)
        std::fprintf(stderr, "pltt_base %04x -> %04x\n", g_plttbase, v);
    g_plttbase = v;
    if(gpu_rec::active){gpu_rec::tex_dirty=true;return;}
    bind_from_vram();
}

// Diagnostic: a running hash of every word entering the engine, so a smoke
// can tell "the game emitted a different stream" from "the decode ignored
// it". Reset returns the previous value.
static uint32_t g_stream_hash = 2166136261u;
void gx_stream_note(uint32_t w) { g_stream_hash = (g_stream_hash ^ w) * 16777619u; }
uint32_t gx_stream_hash_reset()
{
    const uint32_t h = g_stream_hash;
    g_stream_hash = 2166136261u;
    return h;
}

int gx_store_count_reset() { int n = g_store_count; g_store_count = 0; return n; }

uint32_t gx_state_hash()
{
    // Keep the diagnostic API's float-view byte domain independent of Mat storage.
    uint32_t h = 2166136261u;
    auto matrix = [&](const Mat& m) {
        float view[16];
        for (int i = 0; i < 16; ++i) view[i] = m.f(i);
        const auto* bytes = reinterpret_cast<const unsigned char*>(view);
        for (size_t i = 0; i < sizeof(view); ++i) h = (h ^ bytes[i]) * 16777619u;
    };
    matrix(g.pos);
    for (unsigned i = 0; i < 4; ++i) matrix(g.pos_stack[i]);
    return h;
}

void gx_write_port(uint32_t addr, uint32_t value) {
    if(!gpu_rec::active)gx_stream_note(addr ^ value);
    const uint8_t cmd = static_cast<uint8_t>((addr - 0x04000400u) >> 2);
    const int need = param_count(cmd);
    if (need <= 1) {
        if(gpu_rec::active)exec_gpu(cmd, &value, 1);else
        exec(cmd, &value, 1);
        return;
    }
    // Multi-parameter ports are written repeatedly; accumulate until satisfied.
    if (cmd != g_port_cmd) { g_port_cmd = cmd; g_port_have = 0; }
    if (g_port_have < 32) g_port_params[g_port_have++] = value;
    if (g_port_have >= need) {
        exec(cmd, g_port_params, g_port_have);
        g_port_have = 0;
        g_port_cmd = 0;
    }
}

void gx_begin_frame() {
    clip_sparse::configure();
    // A primitive crossing a logical frame retains exact CPU fallback state.
    // This first spike admits complete BEGIN/END intervals only.
    gpu_rec::begin_frame(g_direct,mesh_allow&&g_direct,!g.strip.empty());
    const bool carry=!mesh_strip.empty()||!g.strip.empty();if(carry)mesh_disable();
    bmd_native::new_frame();
    mesh_packet.clear(mesh_allow&&g_direct&&!carry);mesh_strip.clear();mesh_matrix_valid=false;mesh_cpu_generation=UINT64_MAX;

    g.tris.clear();g_packet.clear();g_strip_ids.clear();
    // An unfinished strip is DS state and survives frame boundaries.
    if(g_direct)for(const auto& v:g.strip)g_strip_ids.push_back(g_packet.vertex(v));
    ++g_texture_frame;
    // GX retains material state across frame boundaries. A draw may reuse the
    // currently bound texture without issuing another TEXIMAGE command.
    for(auto& entry:g_vram_tex_cache)if(entry.second.pixels.data()==g.tex_rgba)
        entry.second.used_frame=g_texture_frame;
    trim_texture_cache();
}
void gx_texture_cache_usage(size_t& entries,size_t& bytes){entries=g_vram_tex_cache.size();bytes=g_texture_bytes;}
// game-coverage (2026-10-09): at a scene/level handoff every decoded texture
// of the previous scene is dead weight (the budget above only counted part of
// the per-entry footprint and kept up to ~16 MB of the newlib heap across
// levels). Drop entries not referenced by the current or previous frame,
// exactly the entries trim_texture_cache may evict.
void gx_texture_cache_scene_trim(){
    for(auto it=g_vram_tex_cache.begin();it!=g_vram_tex_cache.end();){
        if(it->second.used_frame+1<g_texture_frame){g_texture_bytes-=it->second.bytes();it=g_vram_tex_cache.erase(it);}
        else ++it;
    }
}

void gx_reset() {
    clip_sparse::configure();
    bmd_native::reset();
    mesh_packet.clear(false);mesh_strip.clear();mesh_matrix_valid=false;mesh_cpu_generation=UINT64_MAX;mesh_convergence.reset();
    g_physical_texture_views.invalidate();
    g = State{};g_packet.clear();g_strip_ids.clear();gpu_rec::reset();
    // VRAM-decoded texture cache. Entries are keyed on the texel bytes too
    // (bind_from_vram), so a replaced upload can never hit a stale entry;
    // clearing every frame only re-decoded everything and recycled the
    // buffers' addresses under consumers that cache by pointer. Evict stale
    // frames only when the decoded-byte budget is exceeded.
    ++g_texture_frame;trim_texture_cache();
    g_teximage = g_plttbase = 0;
    // The stack slots must start as identity, not zero. Model display lists open
    // with MTX_RESTORE against a slot the *scene* filled in earlier; rendering a
    // model on its own would otherwise load an all-zero matrix and collapse every
    // vertex onto the origin -- geometry decodes fine and nothing draws.
    for (int i = 0; i < 32; ++i) {
        g.pos_stack[i] = Mat::identity();
        g.vec_stack[i] = Mat::identity();
    }
    g.proj_stack[0] = g.proj_stack[1] = Mat::identity();
    g_queued = g_qpos = g_have = 0;
    g_port_cmd = 0; g_port_have = 0;
}

const GxTriangle *gx_polygons(size_t &count) {
    if(gpu_rec::gpu_frame){static gx_geometry::Packet cpu;gx_gpu_reconstruct(*gpu_rec::last(),cpu);cpu.reconstruct(g.tris);count=g.tris.size();return g.tris.empty()?nullptr:g.tris.data();}
    if(mesh_packet.requested()){mesh_fallback();mesh_cpu.reconstruct(g.tris);}else if(g_direct)g_packet.reconstruct(g.tris);
    count = g.tris.size();
    return g.tris.empty() ? nullptr : g.tris.data();
}

void gx_append_polygons(const GxTriangle *triangles, size_t count) {
    if(gpu_rec::active)gpu_demote_to_cpu();
    if(mesh_packet.requested()){mesh_packet.refuse(native_mesh::Refusal::ExternalGeometry);mesh_disable();}
    if(!g_direct){if(count)g.tris.insert(g.tris.end(),triangles,triangles+count);return;}
    for(size_t i=0;i<count;++i){const auto& t=triangles[i];
        uint32_t ids[3];for(unsigned k=0;k<3;++k)ids[k]=g_packet.vertex(t.v[k]);
        g_packet.triangle(ids[0],ids[1],ids[2],gx_geometry::Material::from(t));
    }
}

void gx_render(Framebuffer &fb) {
    if(gpu_rec::gpu_frame){static gx_geometry::Packet cpu;gx_gpu_reconstruct(*gpu_rec::last(),cpu);cpu.reconstruct(g.tris);}else
    if(mesh_packet.requested()){mesh_fallback();mesh_cpu.reconstruct(g.tris);}else if(g_direct)g_packet.reconstruct(g.tris);
    const uint32_t display=io_ready()?static_cast<uint32_t>(io_read(0x04000060,2)):0;
    const unsigned alpha_reference=(display&4)?static_cast<unsigned>(io_read(0x04000340,2))&31:0;
    static float depth[SCREEN_H][SCREEN_W];
    static uint8_t translucent_id[SCREEN_H][SCREEN_W],opaque_id[SCREEN_H][SCREEN_W];
    static uint8_t shadow_mask[SCREEN_H][SCREEN_W];
    std::memset(opaque_id,0x3f,sizeof opaque_id);
    std::memset(shadow_mask,0,sizeof shadow_mask);
    bool previous_shadow_mask=false;
    std::memset(translucent_id,0xff,sizeof translucent_id);
    for (int y = 0; y < SCREEN_H; ++y)
        for (int x = 0; x < SCREEN_W; ++x) depth[y][x] = 1e30f;

    // Clip before dividing by w. Sky polygons often straddle the eye plane;
    // discarding an entire triangle there removes visible clouds. Interpolate
    // screen*w so this also preserves the viewport used at submission time.
    static HostVector<GxTriangle> clipped;
    clipped.clear();
    clipped.reserve(g.tris.size());
    for (const auto& source : g.tris) {
        GxVertex polygon[8], output[8];
        int n=3;
        for(int i=0;i<3;++i)polygon[i]=source.v[i];
        for(int plane=0;plane<2 && n;++plane) {
            auto distance=[plane](const GxVertex& v){return plane ? v.w-v.cz : v.w+v.cz;};
            int m=0;auto a=polygon[n-1];float da=distance(a);
            for(int i=0;i<n;++i) {
                const auto& b=polygon[i];const float db=distance(b);
                if((da>=0)!=(db>=0)) {
                    const float f=da/(da-db);GxVertex v{};
#define MIX(field) (a.field+(b.field-a.field)*f)
                    v.w=MIX(w);v.cx=MIX(cx);v.cy=MIX(cy);v.cz=MIX(cz);v.cw=MIX(cw);
                    v.u=MIX(u);v.v=MIX(v);
#undef MIX
                    if(v.w>1e-8f) {
                        v.x=(a.x*a.w+(b.x*b.w-a.x*a.w)*f)/v.w;
                        v.y=(a.y*a.w+(b.y*b.w-a.y*a.w)*f)/v.w;
                        v.z=(v.cz/v.w+1.f)*.5f;
                    }
                    for(unsigned shift:{0u,8u,16u,24u}) {
                        const float ac=(a.color>>shift)&255,bc=(b.color>>shift)&255;
                        v.color|=static_cast<uint32_t>(ac+(bc-ac)*f+.5f)<<shift;
                    }
                    output[m++]=v;
                }
                if(db>=0)output[m++]=b;
                a=b;da=db;
            }
            n=m;for(int i=0;i<n;++i)polygon[i]=output[i];
        }
        for(int i=1;i+1<n;++i) {
            GxTriangle t=source;t.v[0]=polygon[0];t.v[1]=polygon[i];t.v[2]=polygon[i+1];
            // The DS software reference consumes integer viewport positions.
            // Retaining subpixels makes nominally straight UI edges step by
            // one pixel (notably the opening letter). Keep clip-space values
            // untouched for the high-resolution 3DS presentation path.
            for(auto& v:t.v){v.x=std::floor(v.x);v.y=std::floor(v.y);}
            clipped.push_back(t);
        }
    }
    for (const GxTriangle &t : clipped) {
        const unsigned polygon_id=(t.polygon_attr>>24)&63;
        const bool shadow=((t.polygon_attr>>4)&3)==3;
        const bool mask=shadow && polygon_id==0;
        if(mask && !previous_shadow_mask)std::memset(shadow_mask,0,sizeof shadow_mask);
        previous_shadow_mask=mask;
        const GxVertex &a = t.v[0], &b = t.v[1], &c = t.v[2];
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-6f) continue;
        // Degenerate intersections at the eye have no finite screen position.
        if (a.w <= 1e-4f || b.w <= 1e-4f || c.w <= 1e-4f) continue;

        // DS bit7 enables front faces, bit6 back faces. Menu water grids
        // deliberately alternate winding and enable both sides.
        if (area < 0.0f ? !(t.polygon_attr & 0x80) : !(t.polygon_attr & 0x40)) continue;

        int minx = static_cast<int>(std::floor(std::fmin(a.x, std::fmin(b.x, c.x))));
        int maxx = static_cast<int>(std::ceil(std::fmax(a.x, std::fmax(b.x, c.x))));
        int miny = static_cast<int>(std::floor(std::fmin(a.y, std::fmin(b.y, c.y))));
        int maxy = static_cast<int>(std::ceil(std::fmax(a.y, std::fmax(b.y, c.y))));
        if (minx < 0) minx = 0;
        if (miny < 0) miny = 0;
        if (maxx > SCREEN_W - 1) maxx = SCREEN_W - 1;
        if (maxy > SCREEN_H - 1) maxy = SCREEN_H - 1;

        for (int y = miny; y <= maxy; ++y) {
            for (int x = minx; x <= maxx; ++x) {
                const float px = x, py = y;
                float w0 = ((b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x)) / area;
                float w1 = ((c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x)) / area;
                float w2 = ((a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x)) / area;
                // Accept either winding; back-face culling is a POLYGON_ATTR job.
                if (!((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)))
                    continue;
                auto included_edge=[area](const GxVertex& a,const GxVertex& b) {
                    const float dx=b.x-a.x,dy=b.y-a.y;
                    return area>0 ? (dy<0 || (dy==0 && dx>0)) : (dy>0 || (dy==0 && dx<0));
                };
                if((w0==0 && !included_edge(a,b)) || (w1==0 && !included_edge(b,c)) || (w2==0 && !included_edge(c,a)))continue;
                const float s0 = w1, s1 = w2, s2 = w0;   // screen-space barycentrics
                const float z = s0 * a.z + s1 * b.z + s2 * c.z;   // z/w is affine in screen space
                // Perspective-correct weights for attributes: interpolate a/w
                // and 1/w, then divide. Under an affine projection (w == 1)
                // this reduces to the screen-space weights exactly.
                const float q0 = s0 / a.w, q1 = s1 / b.w, q2 = s2 / c.w;
                const float qs = q0 + q1 + q2;
                const float l0 = q0 / qs, l1 = q1 / qs, l2 = q2 / qs;
                // Shadow ID0 describes a volume, not a visible surface. Mark
                // only its depth-failing fragments; the following nonzero-ID
                // shadow pass intersects that mask with the receiver surface.
                if(mask){if(z>=depth[y][x])shadow_mask[y][x]=1;continue;}
                if(shadow && !shadow_mask[y][x])continue;
                if (z >= depth[y][x]) continue;
                // Depth is written only after the texel passes the alpha test
                // below -- a transparent texel must not occlude what is behind it.
                // Texture first; the vertex colour modulates it.
                uint32_t texel = 0xFFFFFFFFu;
                if (t.tex && t.tw > 0 && t.th > 0) {
                    const float uu = l0 * a.u + l1 * b.u + l2 * c.u;
                    const float vv = l0 * a.v + l1 * b.v + l2 * c.v;
                    // GBATEK TEXIMAGE_PARAM: bit16/17 repeat S/T, bit18/19
                    // flip S/T (mirror every other repeat), else clamp. SM64DS
                    // relies on flip: Mario's face texture holds ONE eye and
                    // the hardware mirrors it into the second.
                    const int ui = wrap_coord(static_cast<int>(std::floor(uu)), t.tw,
                                              t.wrap & 1, t.wrap & 4);
                    const int vi = wrap_coord(static_cast<int>(std::floor(vv)), t.th,
                                              t.wrap & 2, t.wrap & 8);
                    texel = t.tex[vi * t.tw + ui];
                    if ((texel >> 24) == 0) continue;      // transparent texel
                }

                // Round, do not truncate: barycentrics sum to 0.9999 rather than
                // exactly 1, and a truncating cast bands a flat surface 255/254.
                auto ch = [&](int sh) {
                    const float v = l0 * ((a.color >> sh) & 0xFF)
                                    + l1 * ((b.color >> sh) & 0xFF)
                                    + l2 * ((c.color >> sh) & 0xFF);
                    const float m = ((texel >> sh) & 0xFF) / 255.0f;
                    const int i = static_cast<int>(v * m + 0.5f);
                    return static_cast<uint32_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
                };
                const unsigned polygon_alpha=(t.polygon_attr>>16)&31;
                const unsigned texture_alpha=(texel>>24)>>3;
                const unsigned alpha=((texture_alpha+1)*(polygon_alpha+1)-1)>>5;
                if(alpha<=alpha_reference)continue;
                const unsigned id=(t.polygon_attr>>24)&63;
                const bool translucent=alpha<31 || shadow;
                if(translucent && translucent_id[y][x]==id)continue;
                if(shadow && translucent_id[y][x]==0xff && opaque_id[y][x]==id)continue;
                uint32_t color=(ch(16)<<16)|(ch(8)<<8)|ch(0);
                const auto old=fb.px[y][x];
                if(alpha<31 && old>>24 && (display&8)) {
                    uint32_t blended=0;
                    for(unsigned shift:{0u,8u,16u}) {
                        const unsigned src=((color>>shift)&255)>>2;
                        const unsigned dst=((old>>shift)&255)>>2;
                        const unsigned c=(src*(alpha+1)+dst*(31-alpha))>>5;
                        blended|=((c<<2)|(c>>4))<<shift;
                    }
                    color=blended;
                }
                const unsigned out_alpha=std::max(alpha,static_cast<unsigned>((old>>24)>>3));
                fb.px[y][x]=color|((out_alpha<<3|out_alpha>>2)<<24);
                if(!translucent || (t.polygon_attr&(1u<<11)))depth[y][x]=z;
                if(translucent)translucent_id[y][x]=id;
                else {opaque_id[y][x]=id;translucent_id[y][x]=0xff;}
            }
        }
    }
}

void gx_direct_configure(bool enabled){g_direct=enabled;}
bool gx_direct_enabled(){return g_direct;}
const gx_geometry::Packet& gx_direct_packet(){if(mesh_packet.requested()){mesh_fallback();return mesh_cpu;}return g_packet;}
size_t gx_direct_triangle_count(){if(mesh_packet.requested())return bmd_native::triangle_count()+mesh_packet.triangles().size();return g_direct?g_packet.triangles().size():g.tris.size();}

bool gx_native_mesh_capture_raw(const char* path){
 FILE* f=std::fopen(path,"wb");if(!f)return false;
 const bool raw=mesh_packet.requested();const auto& cpu=g_packet;
 const uint32_t nv=raw?mesh_packet.vertices().size():cpu.vertices().size(),nt=raw?mesh_packet.triangles().size():cpu.triangles().size();
 const uint32_t header[]={0x4d475831u,1u,uint32_t(raw),uint32_t(raw&&mesh_packet.admitted()),uint32_t(raw?mesh_packet.refusal():native_mesh::Refusal::None),uint32_t(raw?mesh_packet.matrices().size():0),nv,nt};
 bool ok=std::fwrite(header,sizeof header,1,f)==1;
 for(const int32_t* words:{g.pos.raw,g.proj.raw,g.vec.raw,g.tex.raw,g.clip})ok=std::fwrite(words,64,1,f)==1&&ok;
 if(raw){
  for(const auto& m:mesh_packet.matrices())ok=std::fwrite(m.fixed,64,1,f)==1&&ok;
  for(const auto& v:mesh_packet.vertices()){
   uint32_t words[]={uint32_t(int32_t(v.x)),uint32_t(int32_t(v.y)),uint32_t(int32_t(v.z)),v.matrix,v.color,0,0,uint32_t(v.vp_x),uint32_t(v.vp_y),uint32_t(v.vp_w),uint32_t(v.vp_h)};
   std::memcpy(words+5,&v.u,4);std::memcpy(words+6,&v.v,4);ok=std::fwrite(words,sizeof words,1,f)==1&&ok;
  }
 }
 static_assert(sizeof(GxVertex)==44&&sizeof(native_mesh::Matrix)==64);
 for(uint32_t i=0;i<nv;++i){const auto v=raw?mesh_packet.cpu_vertex(i):cpu.vertices()[i];ok=std::fwrite(&v,sizeof v,1,f)==1&&ok;}
 auto triangle=[&](const auto& t){const auto& m=t.material;const uint32_t words[]={t.id[0],t.id[1],t.id[2],m.polygon_attr,uint32_t(m.tw),uint32_t(m.th),m.wrap,uint32_t(m.texture_version),uint32_t(m.texture_version>>32)};ok=std::fwrite(words,sizeof words,1,f)==1&&ok;};
 if(raw)for(const auto& t:mesh_packet.triangles())triangle(t);else for(const auto& t:cpu.triangles())triangle(t);
 ok=std::fclose(f)==0&&ok;
 std::printf("Matraw capture raw32=1 rawMesh=%u admitted=%u refusal=%u matrices=%u vertices=%u triangles=%u success=%u\n",header[2],header[3],header[4],header[5],nv,nt,unsigned(ok));return ok;
}
void gx_native_mesh_allow(bool value){mesh_allow=value;}
const native_mesh::Packet& gx_native_mesh_packet(){return mesh_packet;}
void gx_native_mesh_camera_target(float distance){mesh_convergence.update(distance,g.proj.f(11));}
void gx_native_mesh_camera_cut(){mesh_convergence.cut();}
bool gx_native_mesh_camera_valid(){return mesh_convergence.valid();}
float gx_native_mesh_convergence(){return mesh_convergence.value();}
float gx_native_mesh_convergence_or(float fallback){return mesh_convergence.valid()?mesh_convergence.value():fallback;}
float gx_native_mesh_projection_w_scale(){return g.proj.f(11);}
}  // namespace ntr

#include "bmd_instance_evaluator.inc"
#include "bmd_sdk_bridge.inc"
#include "gx_gpu_api.inc"
