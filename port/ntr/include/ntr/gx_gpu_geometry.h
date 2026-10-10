#pragma once
// GPU geometry mode (gpu-geometry-menu pilot). The DS geometry engine keeps
// decoding every command and tracking every matrix on the CPU, but vertices are
// recorded in model space with their DS attributes instead of being projected
// and lit. The PICA200 vertex shader (platform/3ds/gx_gpu.v.pica) performs the
// clip transform, the DS viewport snap and DS lighting. The scene policy
// (game_main: title/main menu only) enables it via gx_gpu_configure(true);
// SM64DS_GX_GPU=0 forces the CPU path everywhere.
//
// Recording is lossless: gx_gpu_reconstruct() rebuilds the exact CPU-path
// triangles (same fixed-point projection and lighting) for captures, the
// CPU rasteriser and packet-hash exactness checks. Anything the recorder does
// not support (external polygons) demotes the frame to the CPU path, exactly.
#include "ntr/gx.h"
#include "ntr/gx_geometry_packet.h"
#include "ntr/host_allocator.h"
#include <cstdint>
namespace ntr::gx_gpu {
// GPU attribute layout, 24 bytes, three GPU_SHORT x4 attributes, read by the
// PICA directly from the recording buffer (linear memory, no copy):
// v0 = model-space position (DS 4.12) and the palette slot*4 of its matrix
// entry (entry % 16), v1 = s, t (DS 12.4 texels), lit flag, lighting env id,
// v2 = lit: normal after the vector matrix (signed .9, exact DS integer
// result); unlit: 5-bit r, g, b; w = the matrix entry id.
// Every recorded triangle references entries spanning fewer than 16 ids, so
// one 16-slot palette window (slot = entry % 16) covers it.
struct Vertex {
    std::int16_t x,y,z,slot;
    std::int16_t s,t,lit,env;
    std::int16_t a,b,c,entry;
};
constexpr unsigned palette_slots=16;
static_assert(sizeof(Vertex)==24,"GPU vertex layout");
// DS fixed12 clip matrix (position x projection, row-vector convention) and the
// viewport (x, y, width, height) in effect for the vertices that use it.
struct Entry {std::int32_t clip[16];std::int16_t viewport[4];};
// Raw DS lighting inputs latched at NORMAL time; enough to recompute the exact
// integer colour on the CPU and to derive shader uniforms.
struct Env {   // no implicit padding: compared with memcmp
    std::uint8_t dif[3],amb[3],spe[3],emi[3];
    std::uint8_t mask,toon,pad[2];   // toon: polygon mode 2 toon shading (colour = table[red])
    struct Light {std::int32_t recip;std::int16_t dx,dy,dz,pad0;std::uint8_t r,g,b,pad1;} light[4];
    std::uint16_t table[32];         // DS toon table (BGR555) when toon, else zero
};
static_assert(sizeof(Env)==144,"Env layout");
struct Triangle {std::uint32_t id[3];std::uint32_t material;};
// Fixed-capacity vertex storage from the platform allocator (GPU-readable).
struct VertexStore {
    Vertex* data_=nullptr;std::size_t size_=0,capacity_=0;
    std::size_t size()const{return size_;}
    bool empty()const{return !size_;}
    const Vertex* data()const{return data_;}
    Vertex* data(){return data_;}
    const Vertex& operator[](std::size_t i)const{return data_[i];}
    Vertex& operator[](std::size_t i){return data_[i];}
    void clear(){size_=0;}
};
struct Frame {
    VertexStore vertices;
    HostVector<Triangle> triangles;
    HostVector<Entry> entries;
    HostVector<Env> envs;
    HostVector<gx_geometry::Material> materials;
    std::uint64_t generation=0;
    void clear(){
        vertices.clear();triangles.clear();entries.clear();envs.clear();materials.clear();++generation;
    }
};
// Exact DS integer lighting (gx.cpp NORMAL) for a transformed normal; 15-bit BGR.
std::uint16_t light_color(const Env& env,const int normal[3],const std::uint8_t* shininess=nullptr,bool use_shininess=false);
}
namespace ntr {
// Allow GPU geometry recording from the next gx_begin_frame (scene policy).
void gx_gpu_configure(bool allowed);
// Platform storage for the recorded vertices (3 frames x capacity vertices):
// GPU-readable memory (3DS: linearAlloc). Without it GPU mode stays off.
void gx_gpu_storage(void* (*allocate)(std::size_t bytes),std::size_t vertex_capacity);
bool gx_gpu_recording();
// Presentation side: the frame to draw, or null when the last recorded frame
// used the CPU path. Swaps buffers only when a new frame was recorded, so a
// repeated presentation of the same update returns the same frame. The caller
// must have drained any reader of the previously published frame.
const gx_gpu::Frame* gx_gpu_publish();
// Display-list words DMA'd to the geometry FIFO (same semantics as calling
// gx_write_fifo for each word; decoded in place while recording for the GPU).
void gx_write_fifo_block(const uint32_t* words,int count);
// CPU reconstruction (exact CPU-path vertices) of a recorded frame.
void gx_gpu_reconstruct(const gx_gpu::Frame& frame,gx_geometry::Packet& out);
}
