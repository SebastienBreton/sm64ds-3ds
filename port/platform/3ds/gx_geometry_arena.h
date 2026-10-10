#pragma once
// Caller must acquire/drain GPU readers before any arena or batch write.
#include "ntr/gx_geometry_packet.h"
#include "platform/3ds/ds_gpu_viewport.h"
#include <algorithm>
namespace ntr::gx_geometry {
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
    struct Cached {V3dsVert value{};int tw=0,th=0;std::uint32_t stamp=0;};
    HostVector<Cached> cache;
    std::uint32_t stamp_=0; // Cached entries are valid only for the current begin().
    Arena arena{};
    std::size_t vertex_count_=0,batch_count_=0,depth_count_=0;
    bool valid_=true;
    std::uint64_t geometry_generation_=0;
    // Bit-identical to the former per-channel c/255.f and u/tw divisions:
    // the table holds those exact quotients, and power-of-two reciprocals are
    // exact, so multiplication rounds identically.
    static const float* unit_table(){
        static const auto table=[]{struct T{float v[256];} t{};for(int i=0;i<256;++i)t.v[i]=i/255.f;return t;}();
        return table.v;
    }
    static float texel_scale(float u,int size){
        if(!size)return 0.f;
        if(!(size&(size-1)) && size<=4096)return u*reciprocal(size);
        return u/size;
    }
    // Exact reciprocals of the powers of two up to 4096.
    static float reciprocal(int size){
        static const auto table=[]{struct T{float v[13];} t{};for(int i=0;i<13;++i)t.v[i]=1.f/float(1<<i);return t;}();
        return table.v[31-__builtin_clz(unsigned(size))];
    }
    static DsGpuVertex convert(const GxVertex& v,int tw,int th){
        const float* unit=unit_table();
        return {{v.cx,v.cy,v.cz,v.cw,
            unit[(v.color>>16)&255],unit[(v.color>>8)&255],unit[v.color&255],unit[v.color>>24],
            texel_scale(v.u,tw),texel_scale(v.v,th)},v.x,v.y};
    }
    // Exact std::floor for |x|<2^31 (screen coordinates); NaN/huge fall back.
    static float fast_floor(float x){
        if(!(x>-2147483648.f && x<2147483648.f))return std::floor(x);
        const float t=float(int(x));return t>x?t-1.f:t;
    }
    static void pixel_center(V3dsVert& v){v.x+=v.w*(1.f/256.f);v.y-=v.w/192.f;}
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
    Cached& inside_vertex(const GxVertex& v,std::uint32_t id,const Material& m){
        auto& c=cache[id];
        if(c.stamp!=stamp_ || c.tw!=m.tw || c.th!=m.th){
            auto p=convert(v,m.tw,m.th);
            // Identical to ds_gpu_clip_viewport AFTER the established
            // all-inside predicate. No flooring before interpolation.
            p.gpu.x=(fast_floor(p.screen_x)*(1.f/128.f)-1.f)*p.gpu.w;
            p.gpu.y=(1.f-fast_floor(p.screen_y)/96.f)*p.gpu.w;
            c={p.gpu,m.tw,m.th,stamp_};pixel_center(c.value);
        }
        return c;
    }
    // Per source vertex: bit0/1 = outside near/far, bit2 = contributes depth.
    HostVector<std::uint8_t> flags;
public:
    // Allocation permitted during setup only. begin with source_count within
    // reserved capacity does not allocate, and no emission calls allocate.
    void reserve(std::size_t source_capacity){cache.reserve(source_capacity);flags.reserve(source_capacity);}
    bool begin(Arena destination,std::size_t source_count){
        arena=destination;vertex_count_=batch_count_=depth_count_=0;
        valid_=true;
        geometry_generation_=0;
        if(source_count>cache.capacity() || !arena.vertices || !arena.batches || !arena.depths)return valid_=false;
        if(cache.size()<source_count)cache.resize(source_count);
        if(++stamp_==0){for(auto& c:cache)c.stamp=0;stamp_=1;}
        return true;
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
                auto& c=inside_vertex(v,id,m);
                // Keep every repeated original polygon vertex in the median.
                if(!depth({c.value,v.x,v.y}) || !append(c.value,true))return false;
            }
            return true;
        }
            DsGpuVertex polygon[8];
        for(unsigned k=0;k<3;++k)polygon[k]=convert(source[triangle.id[k]],m.tw,m.th);
        const int n=ds_gpu_clip_viewport(polygon);
        for(int k=0;k<n;++k)if(!depth(polygon[k]))return false;
        for(int k=1;k+1<n;++k)
            if(!append(polygon[0].gpu,false) || !append(polygon[k].gpu,false) || !append(polygon[k+1].gpu,false))return false;
        return true;
    }
    // Same output as calling triangle() for each packet triangle. Vertex
    // predicates are evaluated once per source vertex instead of per corner.
    bool build(const Packet& packet,Arena destination){
        const auto& source=packet.vertices();
        const std::size_t count=source.size();
        if(!begin(destination,count) || count>flags.capacity())return valid_=false;
        geometry_generation_=packet.generation();
        flags.resize(count);
        for(std::size_t i=0;i<count;++i){
            const auto& v=source[i];
            flags[i]=(v.cz < -v.cw ? 1u:0u)|(v.cz > v.cw ? 2u:0u)|
                ((v.cw>1e-5f && v.x>=0 && v.x<256 && v.y>=0 && v.y<192) ? 4u:0u);
        }
        for(const auto& t:packet.triangles()){
            const auto a=t.id[0],b=t.id[1],c=t.id[2];
            if(a>=count || b>=count || c>=count || ((flags[a]|flags[b]|flags[c])&3u)){
                if(!triangle(source,t))return false;
                continue;
            }
            if(!batch(t.material))return false;
            if(arena.vertex_capacity-vertex_count_<3)return valid_=false;
            for(unsigned k=0;k<3;++k){
                const auto id=t.id[k];
                const auto& value=inside_vertex(source[id],id,t.material).value;
                if(flags[id]&4u){
                    if(depth_count_==arena.depth_capacity)return valid_=false;
                    arena.depths[depth_count_++]=value.w;
                }
                arena.vertices[vertex_count_++]=value;
            }
            arena.batches[batch_count_-1].count+=3;
        }
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
