#pragma once
// Original GX vertices and ordered frame-local indexed geometry. No GPU ownership.
#include "ntr/gx.h"
#include "ntr/host_allocator.h"
#include <cstdint>
namespace ntr::gx_geometry {
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
}
