#pragma once
// Isolated direct-arena prototype; no production GX/viewer includes this file.
#include "ntr/gx.h"
#include "ntr/host_allocator.h"
#include "platform/3ds/ds_gpu_viewport.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace ntr::gx_arena_candidate {
struct Material {
    const std::uint32_t* tex=nullptr;
    int tw=0,th=0;
    std::uint32_t wrap=0,polygon_attr=0;
    std::uint64_t texture_version=0;
    static Material from(const GxTriangle& t){return {t.tex,t.tw,t.th,t.wrap,t.polygon_attr,t.texture_version};}
};
struct Triangle {std::uint32_t id[3];Material material;};
class Packet {
    HostVector<GxVertex> vertices_;
    HostVector<Triangle> triangles_;
    std::uint64_t generation_=0;
public:
    void reserve(std::size_t vertices,std::size_t triangles){vertices_.reserve(vertices);triangles_.reserve(triangles);}
    void clear(){vertices_.clear();triangles_.clear();++generation_;}
    std::uint32_t vertex(const GxVertex& v){const auto id=std::uint32_t(vertices_.size());vertices_.push_back(v);++generation_;return id;}
    void triangle(std::uint32_t a,std::uint32_t b,std::uint32_t c,Material material){triangles_.push_back({{a,b,c},material});++generation_;}
    // Geometry identity only. External texture writes, PPU/capture inputs and
    // stereo slider state are separate dependencies, never certified by this.
    std::uint64_t generation()const{return generation_;}
    const HostVector<GxVertex>& vertices()const{return vertices_;}
    const HostVector<Triangle>& triangles()const{return triangles_;}
    template<class Vector> void reconstruct(Vector& output)const{
        output.clear();output.reserve(triangles_.size());
        for(const auto& triangle:triangles_){
            GxTriangle t{};
            for(unsigned k=0;k<3;++k)t.v[k]=vertices_.at(triangle.id[k]);
            const auto& m=triangle.material;
            t.tex=m.tex;t.tw=m.tw;t.th=m.th;t.wrap=m.wrap;
            t.polygon_attr=m.polygon_attr;t.texture_version=m.texture_version;
            output.push_back(t);
        }
    }
};
// Caller owns capacities and lifetime. Prototype has no GPU calls or implicit
// arena acquisition: FPS's backend must drain/fence before supplying memory.
struct Arena {
    V3dsVert* vertices=nullptr;std::size_t vertex_capacity=0;
    V3dsBatch* batches=nullptr;std::size_t batch_capacity=0;
    float* depths=nullptr;std::size_t depth_capacity=0;
    std::uint64_t frame_token=0;
};
enum class CoordinateConvention { NativePixelCentered };
struct Prepared {
    const V3dsVert* vertices=nullptr;std::size_t vertex_count=0;
    const V3dsBatch* batches=nullptr;std::size_t batch_count=0;
    std::uint64_t frame_token=0,geometry_generation=0;
    CoordinateConvention coordinates=CoordinateConvention::NativePixelCentered;
    bool valid=false;
};
class Builder {
    struct Cached {V3dsVert value{};int tw=0,th=0;bool valid=false;};
    HostVector<Cached> cache;
    Arena arena{};
    std::size_t vertex_count_=0,batch_count_=0,depth_count_=0;
    bool valid_=true;
    std::uint64_t geometry_generation_=0;
    static DsGpuVertex convert(const GxVertex& v,int tw,int th){
        return {{v.cx,v.cy,v.cz,v.cw,
            ((v.color>>16)&255)/255.f,((v.color>>8)&255)/255.f,(v.color&255)/255.f,(v.color>>24)/255.f,
            tw?v.u/tw:0.f,th?v.v/th:0.f},v.x,v.y};
    }
    static void pixel_center(V3dsVert& v){v.x+=v.w/256.f;v.y-=v.w/192.f;}
    bool depth(const DsGpuVertex& v){
        if(v.gpu.w>1e-5f && v.screen_x>=0 && v.screen_x<256 && v.screen_y>=0 && v.screen_y<192){
            if(depth_count_==arena.depth_capacity)return valid_=false;
            arena.depths[depth_count_++]=v.gpu.w;
        }
        return true;
    }
    bool batch(Material m){
        if(!batch_count_ || arena.batches[batch_count_-1].tex!=m.tex ||
            arena.batches[batch_count_-1].tw!=m.tw || arena.batches[batch_count_-1].th!=m.th ||
            arena.batches[batch_count_-1].wrap!=m.wrap || arena.batches[batch_count_-1].polygon_attr!=m.polygon_attr ||
            arena.batches[batch_count_-1].texture_version!=m.texture_version){
            if(batch_count_==arena.batch_capacity)return valid_=false;
            arena.batches[batch_count_++]={m.tex,m.tw,m.th,m.wrap,int(vertex_count_),0,false,m.polygon_attr,m.texture_version};
        }
        return true;
    }
    bool append(V3dsVert v,bool already_centered){
        if(vertex_count_==arena.vertex_capacity)return valid_=false;
        if(!already_centered)pixel_center(v);
        arena.vertices[vertex_count_++]=v;++arena.batches[batch_count_-1].count;
        return true;
    }
public:
    std::uint64_t conversions=0,reuses=0,clipped_triangles=0;
    // Allocation permitted during setup only. begin with source_count within
    // reserved capacity does not allocate, and no emission calls allocate.
    void reserve(std::size_t source_capacity){cache.reserve(source_capacity);}
    bool begin(Arena destination,std::size_t source_count){
        arena=destination;vertex_count_=batch_count_=depth_count_=0;
        conversions=reuses=clipped_triangles=0;valid_=true;
        geometry_generation_=0;
        if(source_count>cache.capacity() || !arena.vertices || !arena.batches || !arena.depths)return valid_=false;
        cache.assign(source_count,Cached{});return true;
    }
    template<class VertexSequence> bool triangle(const VertexSequence& source,const Triangle& triangle){
        if(!valid_ || !batch(triangle.material))return false;
        const auto& m=triangle.material;
        unsigned outside_union=0;
        for(unsigned k=0;k<3;++k){
            const auto id=triangle.id[k];
            if(id>=source.size() || id>=cache.size())return valid_=false;
            const auto& v=source[id];
            outside_union|=(v.cz < -v.cw ? 1u:0u)|(v.cz > v.cw ? 2u:0u);
        }
        if(!outside_union){
            for(unsigned k=0;k<3;++k){
                const auto id=triangle.id[k];const auto& v=source[id];
                auto& c=cache[id];
                if(!c.valid || c.tw!=m.tw || c.th!=m.th){
                    auto p=convert(v,m.tw,m.th);
                    // Identical to ds_gpu_clip_viewport AFTER the established
                    // all-inside predicate. No flooring before interpolation.
                    p.gpu.x=(std::floor(p.screen_x)/128.f-1.f)*p.gpu.w;
                    p.gpu.y=(1.f-std::floor(p.screen_y)/96.f)*p.gpu.w;
                    c={p.gpu,m.tw,m.th,true};pixel_center(c.value);++conversions;
                }else ++reuses;
                // Keep every repeated original polygon vertex in the median.
                if(!depth({c.value,v.x,v.y}) || !append(c.value,true))return false;
            }
            return true;
        }
        ++clipped_triangles;
        DsGpuVertex polygon[8];
        for(unsigned k=0;k<3;++k)polygon[k]=convert(source[triangle.id[k]],m.tw,m.th);
        const int n=ds_gpu_clip_viewport(polygon);
        for(int k=0;k<n;++k)if(!depth(polygon[k]))return false;
        for(int k=1;k+1<n;++k)
            if(!append(polygon[0].gpu,false) || !append(polygon[k].gpu,false) || !append(polygon[k+1].gpu,false))return false;
        return true;
    }
    bool build(const Packet& packet,Arena destination){
        if(!begin(destination,packet.vertices().size()))return false;
        geometry_generation_=packet.generation();
        for(const auto& t:packet.triangles())if(!triangle(packet.vertices(),t))return false;
        return true;
    }
    bool valid()const{return valid_;}
    std::size_t vertex_count()const{return vertex_count_;}
    std::size_t batch_count()const{return batch_count_;}
    std::size_t depth_count()const{return depth_count_;}
    // Descriptor does not acquire, fence or commit GPU memory. The caller's
    // lease owns these bytes and texture lifetimes through all presentations.
    Prepared prepared()const{return {arena.vertices,vertex_count_,arena.batches,batch_count_,arena.frame_token,geometry_generation_,CoordinateConvention::NativePixelCentered,valid_};}
    float convergence(){
        if(!depth_count_)return 1.f;
        auto* middle=arena.depths+depth_count_/2;
        std::nth_element(arena.depths,middle,arena.depths+depth_count_);return *middle;
    }
};
}
