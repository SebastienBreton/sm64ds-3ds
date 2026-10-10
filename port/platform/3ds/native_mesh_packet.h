#pragma once
#include "ntr/gx_geometry_packet.h"
#include "ntr/gx_gpu_ingame.h"
#include "platform/3ds/viewer_3ds.h"
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
namespace native_mesh {
using ntr::HostVector;
using Material=ntr::gx_geometry::Material;
struct Matrix {int32_t fixed[16]{};};
struct RawVertex {int16_t x=0,y=0,z=0;uint32_t color=0;float u=0,v=0;uint32_t matrix=0;int vp_x=0,vp_y=0,vp_w=256,vp_h=192;};
struct Triangle {uint32_t id[3]{};Material material;};
// castle-min30: hard bound of a per-vertex verified wide matrix (see Packet::wide_).
inline bool wide_matrix_ok(const int32_t* fixed){for(unsigned i=0;i<16;++i)if(fixed[i]<=-0x40000000||fixed[i]>=0x40000000)return false;return true;}
enum class Refusal {None,ExactDomain,Viewport,Projection,MatrixRange,UnsupportedMaterial,Capacity,PrimitiveCarry,ExternalGeometry};
struct Counters {uint64_t vertices=0,triangles=0,combined_matrices=0,project_skipped=0,fallback_vertices=0;};
class Packet {
 HostVector<RawVertex> vertices_;HostVector<Triangle> triangles_;HostVector<Matrix> matrices_;
 // castle-min30: matrices beyond the WideRange limit (huge shadow-volume
 // stretches, e.g. the painting-entry wipe) are admitted per vertex: only
 // when every exact DS product (int64 sum >>12) of each vertex fits int32,
 // i.e. the DS result has no wrap and equals the float transform.
 HostVector<uint8_t> wide_;
 uint64_t generation_=0;Refusal refusal_=Refusal::None;bool requested_=false;
 static bool wide_enabled(){static const bool value=[](){const char* s=std::getenv("SM64DS_MESH_WIDE_VERTEX_CHECK");return !(s&&*s=='0');}();return value;}
public:
 Counters counts;
 void clear(bool requested){vertices_.clear();triangles_.clear();matrices_.clear();wide_.clear();counts={};refusal_=Refusal::None;requested_=requested;++generation_;}
 void refuse(Refusal r){if(refusal_==Refusal::None)refusal_=r;}
 bool requested()const{return requested_;}bool admitted()const{return requested_ && refusal_==Refusal::None;}
 Refusal refusal()const{return refusal_;}uint64_t generation()const{return generation_;}
 uint32_t matrix(const int32_t* fixed){
  ++counts.combined_matrices;
  // Only exact equality of all current words can reuse the last binding.
  // No address/hash-only admission and no quadratic all-palette search.
  if(!matrices_.empty()&&!std::memcmp(matrices_.back().fixed,fixed,64))return matrices_.size()-1;
  Matrix m;std::memcpy(m.fixed,fixed,64);matrices_.push_back(m);
  bool wide=false;
  if(!gx_gpu_ingame::matrix_in_range(m.fixed)){ // +-0x1000000 (WideRange: 0x8000000)
   wide=wide_enabled() && gx_gpu_ingame::enabled(gx_gpu_ingame::WideRange);
   wide=wide && wide_matrix_ok(m.fixed);
   if(!wide)refuse(Refusal::MatrixRange);
  }
  if(wide||!wide_.empty()){wide_.resize(matrices_.size(),0);wide_.back()=wide;}
  return matrices_.size()-1;
 }
 uint32_t vertex(RawVertex v){
  ++counts.vertices;++counts.project_skipped;
  if(v.matrix<wide_.size()&&wide_[v.matrix]){
   const auto& m=matrices_[v.matrix].fixed;
   for(unsigned j=0;j<4;++j){const int64_t s=(int64_t(v.x)*m[j]+int64_t(v.y)*m[4+j]+int64_t(v.z)*m[8+j]+4096LL*m[12+j])>>12;
    if(s!=int64_t(int32_t(s))){refuse(Refusal::MatrixRange);break;}}
  }
  vertices_.push_back(v);++generation_;return vertices_.size()-1;}
 void triangle(uint32_t a,uint32_t b,uint32_t c,Material m){
  const unsigned mode=(m.polygon_attr>>4)&3;
  if((mode!=0&&mode!=3)||!(m.polygon_attr&0x1f0000)||(m.polygon_attr&(1u<<14)))refuse(Refusal::UnsupportedMaterial);
  triangles_.push_back({{a,b,c},m});++counts.triangles;++generation_;
 }
 const auto& vertices()const{return vertices_;}const auto& triangles()const{return triangles_;}const auto& matrices()const{return matrices_;}
 ntr::GxVertex cpu_vertex(uint32_t id)const{
  const auto& v=vertices_.at(id);const auto& m=matrices_.at(v.matrix);
  float c[4];for(unsigned j=0;j<4;++j){const int64_t s=int64_t(v.x)*m.fixed[j]+int64_t(v.y)*m.fixed[4+j]+int64_t(v.z)*m.fixed[8+j]+4096LL*m.fixed[12+j];c[j]=int32_t(s>>12)/4096.f;}
  ntr::GxVertex out{};out.cx=c[0];out.cy=c[1];out.cz=c[2];out.cw=out.w=c[3];const float iw=std::fabs(c[3])>1e-6f?1.f/c[3]:0.f;
  out.x=(c[0]*iw+1.f)*.5f*v.vp_w+v.vp_x;out.y=(1.f-(c[1]*iw+1.f)*.5f)*v.vp_h+v.vp_y;out.z=(c[2]*iw+1.f)*.5f;
  out.u=v.u;out.v=v.v;out.color=v.color;return out;
 }
 void cpu_packet(ntr::gx_geometry::Packet& out){
  out.clear();out.reserve(vertices_.size(),triangles_.size());for(uint32_t i=0;i<vertices_.size();++i)out.vertex(cpu_vertex(i));
  for(const auto& t:triangles_)out.triangle(t.id[0],t.id[1],t.id[2],t.material);counts.fallback_vertices+=vertices_.size();
 }
};
// Two native vertex streams. Position/matrix-slot/index are immutable when
// byte-equal across updates; color and UV retain original CPU DS lighting.
struct Position {int16_t x,y,z;uint8_t matrix_offset,pad;};
struct Attribute {uint32_t color;float u,v;};
static_assert(sizeof(Position)==8&&sizeof(Attribute)==12);
struct Run {std::array<uint32_t,16> matrices{};unsigned matrix_count=0;V3dsBatch batch{};};
class Planner {
 struct Cache {uint32_t epoch=0,index=0;};
 struct MatrixBinding {uint32_t epoch=0;unsigned slot=0;};
 HostVector<Cache> cache_;HostVector<MatrixBinding> matrix_bindings_;uint32_t epoch_=0;
public:
 HostVector<Position> positions;HostVector<Attribute> attributes;HostVector<uint16_t> indices;HostVector<Run> runs;
 uint64_t source_generation=0;size_t duplicated_sources=0;
 bool build(const Packet& packet){
  positions.clear();attributes.clear();indices.clear();runs.clear();duplicated_sources=0;source_generation=packet.generation();
  if(!packet.admitted() || packet.vertices().size()>32768 || packet.triangles().size()>10922)return false;
  const char* option=std::getenv("PLAYER_PLANNER_MATRIX_MAP");const bool mapped=!option||std::strcmp(option,"0")!=0;
  cache_.resize(packet.vertices().size());matrix_bindings_.resize(packet.matrices().size());
  for(const auto& t:packet.triangles()){
   const auto& m=t.material;for(auto id:t.id)if(id>=packet.vertices().size())return false;
   auto equal_material=[&](const Run& r){const auto& b=r.batch;return b.tex==m.tex&&b.tw==m.tw&&b.th==m.th&&b.wrap==m.wrap&&b.polygon_attr==m.polygon_attr&&b.texture_version==m.texture_version;};
   auto additional=[&](const Run& r){unsigned n=0;uint32_t seen[3]{};for(auto id:t.id){const auto mat=packet.vertices()[id].matrix;if(mat>=matrix_bindings_.size())return 17u;
    bool found=false;if(mapped)found=matrix_bindings_[mat].epoch==epoch_;else for(unsigned k=0;k<r.matrix_count;++k)found|=r.matrices[k]==mat;
    for(unsigned k=0;k<n;++k)found|=seen[k]==mat;if(!found)seen[n++]=mat;}return n;};
   if(runs.empty()||!equal_material(runs.back())||runs.back().matrix_count+additional(runs.back())>16){
    if(++epoch_==0){for(auto& c:cache_)c={};for(auto& binding:matrix_bindings_)binding={};++epoch_;}
    Run r;r.batch={m.tex,m.tw,m.th,m.wrap,int(indices.size()),0,false,m.polygon_attr,m.texture_version};runs.push_back(r);
   }
   auto& run=runs.back();
   for(auto id:t.id){const auto& v=packet.vertices()[id];if(v.matrix>=matrix_bindings_.size())return false;
    auto& binding=matrix_bindings_[v.matrix];unsigned slot=0;
    if(mapped){slot=binding.slot;if(binding.epoch!=epoch_){slot=run.matrix_count;if(slot==16)return false;run.matrices[run.matrix_count++]=v.matrix;binding={epoch_,slot};}}
    else {for(;slot<run.matrix_count;++slot)if(run.matrices[slot]==v.matrix)break;if(slot==run.matrix_count){if(slot==16)return false;run.matrices[run.matrix_count++]=v.matrix;}}
    auto& c=cache_[id];if(c.epoch!=epoch_){if(positions.size()>=65535)return false;c={epoch_,uint32_t(positions.size())};
     positions.push_back({v.x,v.y,v.z,uint8_t(slot*4),0});attributes.push_back({v.color,v.u,v.v});}
    indices.push_back(uint16_t(c.index));++run.batch.count;
   }
  }
  duplicated_sources=positions.size()>packet.vertices().size()?positions.size()-packet.vertices().size():0;return true;
 }
};
}
