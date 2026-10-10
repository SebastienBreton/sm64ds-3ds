#include "platform/3ds/gx_geometry_arena.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>
#include <array>
using namespace ntr::gx_geometry;
#define REQUIRE(c) do{if(!(c)){std::fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#c);std::exit(1);}}while(0)
struct Reference {std::vector<V3dsVert> vertices;std::vector<V3dsBatch> batches;std::vector<float> depths;};
// The original player_present conversion loop, followed by the original viewer
// native pixel-centre loop. This remains independent of the candidate cache.
static Reference reference(const std::vector<ntr::GxTriangle>& triangles){
    Reference result;auto& vertices=result.vertices;auto& batches=result.batches;auto& visible_depths=result.depths;
    for(const auto& t:triangles){
        if(batches.empty() || batches.back().tex!=t.tex || batches.back().wrap!=t.wrap ||
            batches.back().tw!=t.tw || batches.back().th!=t.th || batches.back().polygon_attr!=t.polygon_attr ||
            batches.back().texture_version!=t.texture_version)
            batches.push_back({t.tex,t.tw,t.th,t.wrap,(int)vertices.size(),0,false,t.polygon_attr,t.texture_version});
        DsGpuVertex polygon[8];int n=0;
        for(const auto& v:t.v)polygon[n++]={{v.cx,v.cy,v.cz,v.cw,
            ((v.color>>16)&255)/255.f,((v.color>>8)&255)/255.f,(v.color&255)/255.f,(v.color>>24)/255.f,
            t.tw?v.u/t.tw:0.f,t.th?v.v/t.th:0.f},v.x,v.y};
        n=ds_gpu_clip_viewport(polygon);
        for(int k=0;k<n;++k)if(polygon[k].gpu.w>1e-5f && polygon[k].screen_x>=0 && polygon[k].screen_x<256 && polygon[k].screen_y>=0 && polygon[k].screen_y<192)
            visible_depths.push_back(polygon[k].gpu.w);
        for(int k=1;k+1<n;++k){
            vertices.push_back(polygon[0].gpu);vertices.push_back(polygon[k].gpu);vertices.push_back(polygon[k+1].gpu);batches.back().count+=3;
        }
    }
    for(auto& v:vertices){v.x+=v.w/256.f;v.y-=v.w/192.f;}
    return result;
}
static std::uint64_t checksum=1469598103934665603ull,triangles_checked=0,vertices_checked=0,reuses=0,conversions=0,clipped=0;
static void hash(const void* p,std::size_t size){const auto* b=static_cast<const unsigned char*>(p);while(size--)checksum=(checksum^*b++)*1099511628211ull;}
static void compare(Packet& packet,Builder& builder,std::uint64_t token){
    std::vector<ntr::GxTriangle> expanded;packet.reconstruct(expanded);
    auto expected=reference(expanded);
    std::vector<V3dsVert> arena(expected.vertices.size()+1);
    std::vector<V3dsBatch> batches(expected.batches.size()+1);
    std::vector<float> depths(expected.depths.size()+1);
    REQUIRE(builder.build(packet,{arena.data(),arena.size(),batches.data(),batches.size(),depths.data(),depths.size(),token}));
    const auto prepared=builder.prepared();REQUIRE(prepared.valid && prepared.frame_token==token && prepared.geometry_generation==packet.generation());
    REQUIRE(prepared.coordinates==CoordinateConvention::NativePixelCentered && prepared.vertices==arena.data() && prepared.batches==batches.data());
    REQUIRE(builder.vertex_count()==expected.vertices.size());REQUIRE(builder.batch_count()==expected.batches.size());REQUIRE(builder.depth_count()==expected.depths.size());
    if(!expected.vertices.empty())REQUIRE(std::memcmp(arena.data(),expected.vertices.data(),expected.vertices.size()*sizeof(V3dsVert))==0);
    if(!expected.depths.empty())REQUIRE(std::memcmp(depths.data(),expected.depths.data(),expected.depths.size()*sizeof(float))==0);
    for(std::size_t i=0;i<expected.batches.size();++i){const auto& a=batches[i];const auto& b=expected.batches[i];
        REQUIRE(a.tex==b.tex && a.tw==b.tw && a.th==b.th && a.wrap==b.wrap && a.first==b.first && a.count==b.count && a.dynamic==b.dynamic && a.polygon_attr==b.polygon_attr && a.texture_version==b.texture_version);
        hash(&a.first,sizeof(a.first));hash(&a.count,sizeof(a.count));hash(&a.polygon_attr,sizeof(a.polygon_attr));hash(&a.texture_version,sizeof(a.texture_version));
    }
    float convergence=1.f;
    if(!expected.depths.empty()){auto m=expected.depths.begin()+expected.depths.size()/2;std::nth_element(expected.depths.begin(),m,expected.depths.end());convergence=*m;}
    const float actual=builder.convergence();REQUIRE(std::memcmp(&actual,&convergence,sizeof(float))==0);
    hash(arena.data(),expected.vertices.size()*sizeof(V3dsVert));hash(&actual,sizeof(actual));
    triangles_checked+=expanded.size();vertices_checked+=expected.vertices.size();
}
static ntr::GxVertex vertex(float x,float y,float z,float w,std::uint32_t color=0xff7fab13){
    ntr::GxVertex v{};v.w=v.cw=w;v.cx=x;v.cy=y;v.cz=z;
    const float iw=std::fabs(w)>1e-6f?1.f/w:0.f;
    v.x=(x*iw+1.f)*128.f;v.y=(1.f-(y*iw+1.f)*.5f)*192.f;v.z=(z*iw+1.f)*.5f;
    v.u=x*13.f;v.v=y*17.f;v.color=color;return v;
}
#ifndef GX_GEOMETRY_ARENA_REFERENCE_ONLY
int main(){
    Builder builder;builder.reserve(128);Packet packet;packet.reserve(128,256);
    static std::uint32_t tex[64]{},other[64]{};
    std::array<Material,8> materials={Material{nullptr,0,0,0,0x1f00c0,0},
        Material{tex,8,8,3,0x0f00c0,1},Material{tex,16,8,3,0x0f00c0,1},
        Material{tex,8,16,3,0x0f00c0,1},Material{tex,8,8,3,0x0f00c0,2},
        Material{tex,8,8,3,0x01000030,2},Material{other,8,8,15,0x020f0830,0},Material{tex,8,8,3,0x00000030,0}};
    // Shared quads and repeated vertices: convergence keeps duplicates.
    for(unsigned variant=0;variant<8;++variant){packet.clear();
        const auto a=packet.vertex(vertex(-.5f,-.5f,0,1)),b=packet.vertex(vertex(.5f,-.5f,0,1)),c=packet.vertex(vertex(.5f,.5f,0,2)),d=packet.vertex(vertex(-.5f,.5f,0,4));
        for(unsigned i=0;i<32;++i){packet.triangle(a,b,c,materials[variant]);packet.triangle(a,c,d,materials[variant]);}
        compare(packet,builder,variant);
    }
    // Both clip planes, negative/zero/small w, exact boundaries and rejection.
    const float ws[]={-4.f,-1.f,0.f,1e-8f,1e-5f,.5f,1.f,2.f,8.f};
    const float zs[]={-2.f,-1.f,-.99999994f,0.f,.99999994f,1.f,2.f};
    for(float w:ws)for(float z:zs){packet.clear();
        const auto a=packet.vertex(vertex(-.5f*w,-.5f*w,z*w,w));
        const auto b=packet.vertex(vertex(.5f,-.5f,-1.f,1.f));const auto c=packet.vertex(vertex(0,.5f,1.f,1.f));
        for(unsigned m=0;m<8;++m)packet.triangle(a,b,c,materials[m]);compare(packet,builder,100+triangles_checked);
    }
    std::mt19937 random(0x47584152);
    for(unsigned frame=0;frame<1000;++frame){packet.clear();
        for(unsigned i=0;i<64;++i){const float w=std::ldexp(1.f,int(random()%8)-3);
            const float x=(int(random()%8193)-4096)/2048.f*w,y=(int(random()%8193)-4096)/2048.f*w,z=(int(random()%8193)-4096)/2048.f*w;
            auto v=vertex(x,y,z,w,random());v.x+=float(int(random()%17)-8)/16.f;v.y+=float(int(random()%17)-8)/16.f;
            packet.vertex(v);
        }
        for(unsigned i=0;i<128;++i)packet.triangle(random()%64,random()%64,random()%64,materials[(i/8+frame)%8]);
        compare(packet,builder,frame+1000);
    }
    // Capacity refusal preserves caller guards; the immutable packet survives
    // for reconstruction/retry. No silent count truncation or GPU commit.
    packet.clear();for(unsigned i=0;i<3;++i)packet.vertex(vertex(float(i)/8,0,0,1));
    packet.triangle(0,1,2,materials[0]);packet.triangle(0,1,2,materials[0]);
    std::array<V3dsVert,6> small{};small.back().x=12345;
    std::array<V3dsBatch,2> batches{};std::array<float,8> depths{};
    REQUIRE(!builder.build(packet,{small.data(),5,batches.data(),2,depths.data(),8,9000}));REQUIRE(!builder.valid());REQUIRE(small.back().x==12345);
    REQUIRE(!builder.prepared().valid);
    compare(packet,builder,9001);
    const auto previous_generation=packet.generation();packet.clear();REQUIRE(packet.generation()!=previous_generation);
    std::printf("PASS exact arena triangles=%llu output_vertices=%llu conversions=%llu reuses=%llu clipped=%llu checksum=%llu; batches/depth sequence/median/guards exact\n",
        (unsigned long long)triangles_checked,(unsigned long long)vertices_checked,(unsigned long long)conversions,(unsigned long long)reuses,(unsigned long long)clipped,(unsigned long long)checksum);
}
#endif
