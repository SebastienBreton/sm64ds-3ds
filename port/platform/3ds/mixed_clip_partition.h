#pragma once
// Mixed partition: retain the original immutable GPU Frame and its texture
// leases. No projection of XY for an inside triangle. CPU clipping only for
// crossing triangles. Submission must drain before replacing either stream.
#include "prepared_mesh_view.h"
#include "platform/3ds/ds_gpu_viewport.h"
#include <limits>
#include <cstdlib>
#include <cstring>
#include <array>
#include "frame_profile.h"

namespace mixed_clip {
inline int mixed_clip_failure=0; // castle-30fps diagnostics: last build() refusal site

using ntr::HostVector;
enum class Stream { NativeIndexed, CpuClipped };
struct Draw {Stream stream;uint32_t run=0,first=0,count=0;};
struct RunSpan {uint32_t first=0,count=0;};
struct Counts {
 uint64_t vertices=0,matrices=0,bounds_groups=0,bounds_inside_vertices=0,bounds_rejected_vertices=0;
 uint64_t exact_zw_vertices=0,exact_common_row_vertices=0,classification_products=0;
 uint64_t inside_triangles=0,crossing_triangles=0,rejected_triangles=0;
 uint64_t projected_crossing_vertices=0,emitted_cpu_vertices=0,nonpositive_w_cpu_triangles=0;
 uint64_t bulk_inside_runs=0,bulk_inside_triangles=0;
};
struct Frame {
 std::shared_ptr<const prepared_mesh::Frame> gpu;
 HostVector<V3dsVert> cpu;
 HostVector<Draw> draws;
 HostVector<RunSpan> run_spans;
 Counts counts;
 uint64_t source_generation=0;
};
class Builder {
 struct Bounds {int32_t lo[3]{},hi[3]{};bool seen=false;uint8_t code=255;};
 struct Zw {int32_t z=0,w=0;uint8_t code=255;bool positive_w=false;};
 struct Group {size_t first=0,last=0;uint32_t matrix=0;Bounds bounds;};
 struct GroupSpan {size_t first=0,count=0;};
 struct StaticPlan {
  std::shared_ptr<const prepared_mesh::Topology> topology;
  HostVector<uint32_t> resolved;
  HostVector<Group> groups;
  HostVector<GroupSpan> spans;
  HostVector<std::array<Bounds,16>> run_bounds;
 };
 mutable std::shared_ptr<const StaticPlan> cached_plan_;
 struct VertexState {uint32_t stamp=0,binding=UINT32_MAX,projected=UINT32_MAX;Zw zw;};
 mutable HostVector<VertexState> vertex_state_;mutable uint32_t vertex_stamp_=0;
 mutable HostVector<ntr::GxVertex> projected_scratch_;
 // castle-30fps: make_plan per composed layout. The plan of one immutable
 // layout (owner+identity) is independent of where the Composer places it;
 // a new topology only concatenates cached pieces with index/matrix/group
 // offsets, producing exactly make_plan's arrays.
 struct LayoutPlan {
  HostVector<uint32_t> resolved;HostVector<Group> groups;HostVector<GroupSpan> spans;HostVector<std::array<Bounds,16>> run_bounds;
  uint64_t used=0;
 };
 struct LayoutKey {const void* owner;uint64_t identity;const void* plan;};
 mutable HostVector<std::pair<LayoutKey,std::shared_ptr<LayoutPlan>>> layout_plans_;
 mutable uint64_t layout_clock_=0;
 static std::shared_ptr<LayoutPlan> make_layout_plan(const native_mesh::Planner& t){
  auto p=std::allocate_shared<LayoutPlan>(ntr::HostAllocator<LayoutPlan>{});
  p->resolved.resize(t.indices.size());p->spans.resize(t.runs.size());p->run_bounds.resize(t.runs.size());size_t covered=0;
  for(size_t ri=0;ri<t.runs.size();++ri){const auto& r=t.runs[ri];
   if(!r.matrix_count||r.matrix_count>16||r.batch.first<0||r.batch.count<=0||r.batch.count%3||size_t(r.batch.first)!=covered||size_t(r.batch.count)>t.indices.size()-covered)return {};
   const size_t end=covered+size_t(r.batch.count);auto& span=p->spans[ri];span.first=p->groups.size();
   for(size_t k=covered;k<end;++k){const auto id=t.indices[k];if(id>=t.positions.size())return {};const auto slot=t.positions[id].matrix_offset;
    if(slot%4||slot/4>=r.matrix_count)return {};p->resolved[k]=r.matrices[slot/4];extend_bounds(p->run_bounds[ri][slot/4],t.positions[id]);}
   for(size_t first=covered;first<end;){Group g;g.first=first;g.matrix=p->resolved[first];g.last=first;
    while(g.last<end&&g.last-first<32&&p->resolved[g.last]==g.matrix){extend_bounds(g.bounds,t.positions[t.indices[g.last]]);++g.last;}
    p->groups.push_back(g);first=g.last;
   }span.count=p->groups.size()-span.first;covered=end;
  }if(covered!=t.indices.size())return {};return p;
 }
 std::shared_ptr<const StaticPlan> make_plan_layered(std::shared_ptr<const prepared_mesh::Topology> topology)const{
  const auto& t=*topology;if(t.layouts.empty())return {};
  const uint64_t now=++layout_clock_;
  HostVector<std::shared_ptr<LayoutPlan>> pieces;pieces.reserve(t.layouts.size());
  size_t indices=0,runs=0,groups=0;
  for(const auto& ref:t.layouts){
   if(!ref.plan)return {};
   const LayoutKey key{ref.owner.get(),ref.identity,ref.plan};std::shared_ptr<LayoutPlan> piece;
   for(auto& e:layout_plans_)if(e.first.owner==key.owner&&e.first.identity==key.identity&&e.first.plan==key.plan){piece=e.second;break;}
   if(!piece){
    piece=make_layout_plan(*ref.plan);if(!piece)return {};
    // Bounded: the 3DS heap is nearly full in large levels. Drop pieces not
    // used for a while first, then the least recently used one.
    if(layout_plans_.size()>=64){
     for(size_t i=0;i<layout_plans_.size();)if(layout_plans_[i].second->used+64<now){layout_plans_[i]=layout_plans_.back();layout_plans_.pop_back();}else ++i;
     if(layout_plans_.size()>=64){size_t oldest=0;for(size_t i=1;i<layout_plans_.size();++i)if(layout_plans_[i].second->used<layout_plans_[oldest].second->used)oldest=i;
      layout_plans_[oldest]=layout_plans_.back();layout_plans_.pop_back();}
    }
    layout_plans_.push_back({key,piece});
   }
   piece->used=now;indices+=piece->resolved.size();runs+=piece->spans.size();groups+=piece->groups.size();pieces.push_back(std::move(piece));
  }
  if(indices!=t.indices.size()||runs!=t.runs.size())return {};
  auto p=std::allocate_shared<StaticPlan>(ntr::HostAllocator<StaticPlan>{});p->topology=std::move(topology);
  p->resolved.resize(indices);p->spans.resize(runs);p->run_bounds.resize(runs);p->groups.reserve(groups);
  size_t index_base=0,matrix_base=0,run_base=0;
  for(size_t li=0;li<pieces.size();++li){const auto& piece=*pieces[li];const size_t group_base=p->groups.size();
   const uint32_t mb=uint32_t(matrix_base);
   for(size_t k=0;k<piece.resolved.size();++k)p->resolved[index_base+k]=piece.resolved[k]+mb;
   for(auto g:piece.groups){g.first+=index_base;g.last+=index_base;g.matrix+=mb;p->groups.push_back(g);}
   for(size_t r=0;r<piece.spans.size();++r){p->spans[run_base+r]={piece.spans[r].first+group_base,piece.spans[r].count};p->run_bounds[run_base+r]=piece.run_bounds[r];}
   index_base+=piece.resolved.size();run_base+=piece.spans.size();matrix_base+=t.layouts[li].matrix_count;
  }
  return p;
 }
 static bool whole_run_enabled(){const char* option=std::getenv("PLAYER_MIXED_WHOLE_RUN_INSIDE");return option&&std::atoi(option)!=0;}
 static bool static_plan_enabled(){const char* option=std::getenv("PLAYER_MIXED_STATIC_CLIP_PLAN");return option&&std::atoi(option)!=0;}
 static bool same_geometry(const prepared_mesh::Topology& a,const prepared_mesh::Topology& b){
  if(&a==&b)return true;
  if(a.positions.size()!=b.positions.size()||a.indices.size()!=b.indices.size()||a.runs.size()!=b.runs.size())return false;
  if(!a.positions.empty()&&std::memcmp(a.positions.data(),b.positions.data(),a.positions.size()*sizeof(native_mesh::Position)))return false;
  if(!a.indices.empty()&&std::memcmp(a.indices.data(),b.indices.data(),a.indices.size()*sizeof(uint16_t)))return false;
  for(size_t i=0;i<a.runs.size();++i){const auto& x=a.runs[i];const auto& y=b.runs[i];
   if(x.matrix_count!=y.matrix_count||x.matrix_count>16||x.batch.first!=y.batch.first||x.batch.count!=y.batch.count)return false;
   for(unsigned k=0;k<x.matrix_count;++k)if(x.matrices[k]!=y.matrices[k])return false;
  }return true;
 }
 static inline void extend_bounds(Bounds& b,const native_mesh::Position& v){
  // castle-30fps: same result, without the per-component 'seen' branch.
  const int32_t x=v.x,y=v.y,z=v.z;
  if(!b.seen){b.lo[0]=b.hi[0]=x;b.lo[1]=b.hi[1]=y;b.lo[2]=b.hi[2]=z;b.seen=true;return;}
  if(x<b.lo[0])b.lo[0]=x;if(x>b.hi[0])b.hi[0]=x;
  if(y<b.lo[1])b.lo[1]=y;if(y>b.hi[1])b.hi[1]=y;
  if(z<b.lo[2])b.lo[2]=z;if(z>b.hi[2])b.hi[2]=z;
 }
 static std::shared_ptr<const StaticPlan> make_plan(std::shared_ptr<const prepared_mesh::Topology> topology){
  auto p=std::allocate_shared<StaticPlan>(ntr::HostAllocator<StaticPlan>{});p->topology=std::move(topology);
  const auto& t=*p->topology;p->resolved.resize(t.indices.size());p->spans.resize(t.runs.size());p->run_bounds.resize(t.runs.size());size_t covered=0;
  for(size_t ri=0;ri<t.runs.size();++ri){const auto& r=t.runs[ri];
   if(!r.matrix_count||r.matrix_count>16||r.batch.first<0||r.batch.count<=0||r.batch.count%3||size_t(r.batch.first)!=covered||size_t(r.batch.count)>t.indices.size()-covered)return {};
   const size_t end=covered+size_t(r.batch.count);auto& span=p->spans[ri];span.first=p->groups.size();
   for(size_t k=covered;k<end;++k){const auto id=t.indices[k];if(id>=t.positions.size())return {};const auto slot=t.positions[id].matrix_offset;
    if(slot%4||slot/4>=r.matrix_count)return {};p->resolved[k]=r.matrices[slot/4];extend_bounds(p->run_bounds[ri][slot/4],t.positions[id]);}
   for(size_t first=covered;first<end;){Group g;g.first=first;g.matrix=p->resolved[first];g.last=first;
    while(g.last<end&&g.last-first<32&&p->resolved[g.last]==g.matrix){extend_bounds(g.bounds,t.positions[t.indices[g.last]]);++g.last;}
    p->groups.push_back(g);first=g.last;
   }span.count=p->groups.size()-span.first;covered=end;
  }if(covered!=t.indices.size())return {};return p;
 }
public:
 mutable uint64_t static_plan_builds=0,static_plan_hits=0;
private:

 static std::pair<int64_t,int64_t> range(const Bounds& b,const native_mesh::Matrix& m,unsigned row){
  int64_t lo=4096LL*m.fixed[12+row],hi=lo;
  for(unsigned k=0;k<3;++k){const int64_t a=int64_t(b.lo[k])*m.fixed[4*k+row],c=int64_t(b.hi[k])*m.fixed[4*k+row];lo+=std::min(a,c);hi+=std::max(a,c);}
  return {lo>>12,hi>>12};
 }
 static bool bounded(std::pair<int64_t,int64_t> v){return v.first>=INT32_MIN && v.second<=INT32_MAX;}
 static bool common_row(const native_mesh::Matrix& m){
  return m.fixed[2]==m.fixed[3]&&m.fixed[6]==m.fixed[7]&&m.fixed[10]==m.fixed[11];
 }
 template<class Vertex> static int32_t row(const Vertex& v,const native_mesh::Matrix& m,unsigned j){
  return int32_t((int64_t(v.x)*m.fixed[j]+int64_t(v.y)*m.fixed[4+j]+int64_t(v.z)*m.fixed[8+j]+4096LL*m.fixed[12+j])>>12);
 }
 static uint8_t code(int32_t z,int32_t w){
  // Production converts fixed12 to float32 before the clip predicates. Keep
  // that rounding even beyond 2^24, where strict integer rejection may differ.
  const float a=float(z)/4096.f,b=float(w)/4096.f;
  return uint8_t((a<-b?1:0)|(a>b?2:0));
 }
 static DsGpuVertex convert(const ntr::GxVertex& v,const V3dsBatch& m){
  return {{v.cx,v.cy,v.cz,v.cw,((v.color>>16)&255)/255.f,((v.color>>8)&255)/255.f,(v.color&255)/255.f,(v.color>>24)/255.f,m.tw?v.u/m.tw:0.f,m.th?v.v/m.th:0.f},v.x,v.y};
 }
 static void add(Frame& f,Stream stream,uint32_t run,uint32_t first,uint32_t count){
  if(!count)return;
  if(!f.draws.empty()){auto& last=f.draws.back();if(last.stream==stream&&last.run==run&&last.first+last.count==first){last.count+=count;return;}}
  f.draws.push_back({stream,run,first,count});
 }
public:
 // Composer-published Frame is the authority. Matrix slots belong to each run,
 // and are resolved through that run's palette, never a raw source-ID guess.
 std::shared_ptr<const Frame> build(std::shared_ptr<const prepared_mesh::Frame> gpu) const try {
  if(!gpu||!gpu->topology)return (mixed_clip_failure=1,std::shared_ptr<const Frame>{});
  uint64_t tick0=frame_profile::ticks();
  const auto& topology=*gpu->topology;const auto& positions=topology.positions;
  const auto& indices=topology.indices;const auto& matrices=gpu->matrices;
  if(positions.size()>32768||indices.size()>32768||indices.size()%3||matrices.size()>32768||
     gpu->attributes.size()!=positions.size()||gpu->batches.size()!=topology.runs.size())return (mixed_clip_failure=2,std::shared_ptr<const Frame>{});
  for(const auto& attribute:gpu->attributes)if(!std::isfinite(attribute.u)||!std::isfinite(attribute.v))return (mixed_clip_failure=3,std::shared_ptr<const Frame>{});
  for(const auto& matrix:matrices)if(!gx_gpu_ingame::matrix_in_range(matrix.fixed) && !(gpu->wide_verified && native_mesh::wide_matrix_ok(matrix.fixed)))return (mixed_clip_failure=4,std::shared_ptr<const Frame>{});
  // gpu-geometry-ingame GpuClip: no CPU classification; the PICA clips near/far.
  const bool gpu_clip=gx_gpu_ingame::enabled(gx_gpu_ingame::GpuClip);
  const bool reuse_static=static_plan_enabled() && !gpu_clip;std::shared_ptr<const StaticPlan> plan;bool hit=false;
  {const uint64_t t=frame_profile::ticks();sm64ds_frame_prof[17]+=t-tick0;tick0=t;}
  if(reuse_static){
   hit=cached_plan_&&same_geometry(*cached_plan_->topology,topology);
   static const bool layered=std::getenv("PLAYER_MIXED_NO_LAYERED_PLAN")==nullptr;
   plan=hit?cached_plan_:layered?make_plan_layered(gpu->topology):make_plan(gpu->topology);
   if(!plan&&!hit&&layered)plan=make_plan(gpu->topology);
   if(!plan)return (mixed_clip_failure=5,std::shared_ptr<const Frame>{});
  }
  {const uint64_t t=frame_profile::ticks();sm64ds_frame_prof[18]+=t-tick0;tick0=t;}
  auto next=std::allocate_shared<Frame>(ntr::HostAllocator<Frame>{});next->gpu=std::move(gpu);
  next->run_spans.resize(next->gpu->batches.size());auto& counts=next->counts;
  counts.vertices=positions.size();counts.matrices=matrices.size();
  // castle-30fps: per-vertex state persists across builds and is lazily
  // reset by a build stamp (no per-frame allocation/initialisation).
  if(vertex_state_.size()<positions.size())vertex_state_.resize(positions.size());
  if(++vertex_stamp_==0){for(auto& s:vertex_state_)s.stamp=0;vertex_stamp_=1;}
  const uint32_t stamp=vertex_stamp_;VertexState* const states=vertex_state_.data();
  auto vs=[states,stamp](uint32_t id)->VertexState&{auto& s=states[id];if(s.stamp!=stamp){s=VertexState{};s.stamp=stamp;}return s;};
  HostVector<ntr::GxVertex>& projected=projected_scratch_;projected.clear();
  uint32_t common_matrix=UINT32_MAX;bool common_value=false;
  auto common=[&](uint32_t index,const native_mesh::Matrix& m){if(index!=common_matrix){common_matrix=index;common_value=common_row(m);}return common_value;};
  auto bind=[&](uint32_t id,const native_mesh::Run& run)->uint32_t{
   if(id>=positions.size())return UINT32_MAX;const auto offset=positions[id].matrix_offset;
   if(offset%4||offset/4>=run.matrix_count)return UINT32_MAX;
   const uint32_t matrix=run.matrices[offset/4];if(matrix>=matrices.size())return UINT32_MAX;
   auto& s=vs(id);if(s.binding!=matrix){s.binding=matrix;s.zw={};s.projected=UINT32_MAX;}
   return matrix;
  };
  auto extend=extend_bounds;
  auto certify=[&](Bounds& b,const native_mesh::Matrix& m){
   const auto z=range(b,m,2),w=range(b,m,3);counts.classification_products+=12;
   if(!bounded(z)||!bounded(w))return;
   const int64_t delta=int64_t(m.fixed[15])-m.fixed[14];
   if(common_row(m)&&delta>=0&&2*z.first+delta>=0&&z.first+delta>0)b.code=0;
   else if(z.first>=-w.first&&z.second<=w.first&&w.first>0)b.code=0;
   else{const float zl=float(z.first)/4096.f,zh=float(z.second)/4096.f,wl=float(w.first)/4096.f,wh=float(w.second)/4096.f;
    const bool near_all=zh<-wh,far_all=zl>wh,near_none=zl>=-wl,far_none=zh<=wl;
    if(near_all&&far_all)b.code=3;else if(near_all&&far_none)b.code=1;else if(far_all&&near_none)b.code=2;
   }
  };
  auto get_code=[&](uint32_t id,uint32_t matrix)->uint8_t{
   auto& q=vs(id).zw;if(q.code!=255)return q.code;const auto& v=positions[id];const auto& m=matrices[matrix];
   q.z=row(v,m,2);counts.classification_products+=3;const int64_t delta=int64_t(m.fixed[15])-m.fixed[14];
   if(common(matrix,m)&&int64_t(q.z)+delta>=INT32_MIN&&int64_t(q.z)+delta<=INT32_MAX){q.w=int32_t(int64_t(q.z)+delta);++counts.exact_common_row_vertices;}
   else{q.w=row(v,m,3);counts.classification_products+=3;}
   ++counts.exact_zw_vertices;q.code=code(q.z,q.w);q.positive_w=q.w>0;return q.code;
  };
  const ntr::gx_gpu::Env* run_env=nullptr; // gpu-geometry-ingame: lighting of packed normals in this run
  auto get_projected=[&](uint32_t id,uint32_t matrix)->const ntr::GxVertex&{
   auto& index=vs(id).projected;if(index==UINT32_MAX){
    const auto& v=positions[id];const auto& m=matrices[matrix];const auto& a=next->gpu->attributes[id];
    float c[4];for(unsigned j=0;j<4;++j)c[j]=row(v,m,j)/4096.f;
    ntr::GxVertex out{};out.cx=c[0];out.cy=c[1];out.cz=c[2];out.cw=out.w=c[3];
    const float iw=std::fabs(c[3])>1e-6f?1.f/c[3]:0.f;
    out.x=(c[0]*iw+1.f)*.5f*256;out.y=(1.f-(c[1]*iw+1.f)*.5f)*192;out.z=(c[2]*iw+1.f)*.5f;
    out.u=a.u;out.v=a.v;out.color=run_env&&gx_gpu_ingame::packed_normal(a.color)?gx_gpu_ingame::lit_argb(*run_env,a.color):a.color;
    index=projected.size();projected.push_back(out);++counts.projected_crossing_vertices;
   }return projected[index];
  };
  const bool bulk_runs=plan && whole_run_enabled();
  {const uint64_t t=frame_profile::ticks();sm64ds_frame_prof[19]+=t-tick0;tick0=t;}
  size_t covered=0;
  for(size_t ri=0;ri<topology.runs.size();++ri){
   const auto& run=topology.runs[ri];const auto& material=next->gpu->batches[ri];const unsigned mode=(material.polygon_attr>>4)&3;
   if(!run.matrix_count||run.matrix_count>16||material.first<0||material.count<=0||material.count%3||
      size_t(material.first)!=covered||size_t(material.count)>indices.size()-covered||
      run.batch.first!=material.first||run.batch.count!=material.count||
      (mode!=0&&mode!=3)||!(material.polygon_attr&0x1f0000)||(material.polygon_attr&(1u<<14)))return (mixed_clip_failure=6,std::shared_ptr<const Frame>{});
   for(unsigned k=0;k<run.matrix_count;++k)if(run.matrices[k]>=matrices.size())return (mixed_clip_failure=7,std::shared_ptr<const Frame>{});
   if(material.tex&&(material.tw<=0||material.th<=0||material.tw>1024||material.th>1024||next->gpu->texture_owners.empty()))return (mixed_clip_failure=8,std::shared_ptr<const Frame>{});
   const size_t end=covered+size_t(material.count);
   run_env=next->gpu->env_of(ri);
   if(gpu_clip){
    counts.inside_triangles+=size_t(material.count)/3;
    add(*next,Stream::NativeIndexed,ri,uint32_t(covered),uint32_t(material.count));covered=end;continue;
   }
   // A certificate uses current matrices and immutable positions. It only
   // bypasses the triangle loop when every referenced palette slot is inside
   // with positive W; materials/attributes retain their current Frame owners.
   uint64_t phase_tick=frame_profile::ticks();
   if(bulk_runs){
    bool entirely_inside=true;
    for(unsigned slot=0;slot<run.matrix_count;++slot){
     Bounds b=plan->run_bounds[ri][slot];if(!b.seen)continue;
     certify(b,matrices[run.matrices[slot]]);++counts.bounds_groups;
     if(b.code!=0){entirely_inside=false;break;}
    }
    if(entirely_inside){
     counts.inside_triangles+=size_t(material.count)/3;
     ++counts.bulk_inside_runs;counts.bulk_inside_triangles+=size_t(material.count)/3;
     add(*next,Stream::NativeIndexed,ri,uint32_t(covered),uint32_t(material.count));
     covered=end;sm64ds_frame_prof[26]+=frame_profile::ticks()-phase_tick;continue;
    }
   }
   {const uint64_t now=frame_profile::ticks();sm64ds_frame_prof[26]+=now-phase_tick;phase_tick=now;}
   // Stamp <=32 indexed references with one resolved matrix. Repeated vertices
   // keep their exact binding; a later run changing its palette invalidates it.
   if(plan){
    const auto span=plan->spans[ri];
    for(size_t group=span.first;group<span.first+span.count;++group){const auto& g=plan->groups[group];Bounds b=g.bounds;
     certify(b,matrices[g.matrix]);++counts.bounds_groups;
     for(size_t k=g.first;k<g.last;++k){const auto id=indices[k];auto& s=vs(id);if(s.binding!=g.matrix){s.binding=g.matrix;s.zw={};s.projected=UINT32_MAX;}
      auto& q=s.zw;if(b.code==255||q.code!=255)continue;q.code=b.code;q.positive_w=b.code==0;
      if(q.code)++counts.bounds_rejected_vertices;else ++counts.bounds_inside_vertices;
     }
    }
   }else{
   for(size_t first=covered;first<end;){
    const uint32_t matrix=bind(indices[first],run);if(matrix==UINT32_MAX)return (mixed_clip_failure=9,std::shared_ptr<const Frame>{});
    Bounds b;size_t last=first;
    while(last<end&&last-first<32){const uint32_t id=indices[last];const uint32_t selected=bind(id,run);if(selected==UINT32_MAX)return (mixed_clip_failure=10,std::shared_ptr<const Frame>{});
     if(selected!=matrix)break;extend(b,positions[id]);++last;}
    certify(b,matrices[matrix]);++counts.bounds_groups;
    if(b.code!=255)for(size_t i=first;i<last;++i){auto& q=vs(indices[i]).zw;if(q.code!=255)continue;q.code=b.code;q.positive_w=b.code==0;if(q.code)++counts.bounds_rejected_vertices;else ++counts.bounds_inside_vertices;}
    first=last;
   }
   }
   {const uint64_t now=frame_profile::ticks();sm64ds_frame_prof[24]+=now-phase_tick;phase_tick=now;}
   for(size_t first=covered;first<end;first+=3){
    uint32_t ids[3],selected[3];uint8_t either=0,both=3;bool positive_w=true;
    for(unsigned k=0;k<3;++k){ids[k]=indices[first+k];selected[k]=plan?plan->resolved[first+k]:bind(ids[k],run);if(selected[k]==UINT32_MAX)return (mixed_clip_failure=11,std::shared_ptr<const Frame>{});
     const auto value=get_code(ids[k],selected[k]);either|=value;both&=value;positive_w=positive_w&&vs(ids[k]).zw.positive_w;}
    if(both){++counts.rejected_triangles;continue;}
    if(!either&&positive_w){++counts.inside_triangles;add(*next,Stream::NativeIndexed,ri,uint32_t(first),3);continue;}
    if(!either)++counts.nonpositive_w_cpu_triangles;
    const uint64_t crossing_start=frame_profile::ticks();
    ++counts.crossing_triangles;DsGpuVertex polygon[8];for(unsigned k=0;k<3;++k)polygon[k]=convert(get_projected(ids[k],selected[k]),material);
    const int n=ds_gpu_clip_viewport(polygon);const uint32_t start=next->cpu.size();
    if(n>=3&&size_t(n-2)*3>32768-next->cpu.size())return (mixed_clip_failure=12,std::shared_ptr<const Frame>{});
    for(int k=1;k+1<n;++k)for(int index:{0,k,k+1}){auto v=polygon[index].gpu;v.x+=v.w/256.f;v.y-=v.w/192.f;next->cpu.push_back(v);}
    const uint32_t count=next->cpu.size()-start;counts.emitted_cpu_vertices+=count;add(*next,Stream::CpuClipped,ri,start,count);
    sm64ds_frame_prof[23]+=frame_profile::ticks()-crossing_start;
   }
   sm64ds_frame_prof[25]+=frame_profile::ticks()-phase_tick;
   covered=end;
  }
  if(covered!=indices.size())return (mixed_clip_failure=13,std::shared_ptr<const Frame>{});
  size_t cursor=0;for(size_t ri=0;ri<next->run_spans.size();++ri){auto& span=next->run_spans[ri];span.first=cursor;while(cursor<next->draws.size()&&next->draws[cursor].run==ri)++cursor;span.count=cursor-span.first;}
  if(cursor!=next->draws.size())return (mixed_clip_failure=14,std::shared_ptr<const Frame>{});
  if(plan){cached_plan_=std::move(plan);if(hit)++static_plan_hits;else ++static_plan_builds;}
  return next;
 }catch(const std::bad_alloc&){return (mixed_clip_failure=15,std::shared_ptr<const Frame>{});}
 std::shared_ptr<const Frame> build(const native_mesh::Packet& packet,std::shared_ptr<const prepared_mesh::Frame> gpu) const try {
  // Current raw adapter has exactly one occurrence. A compiled/mixed occurrence
  // composer requires an explicit source-ID mapping; never guess it here.
  if(!packet.admitted()||!gpu||!gpu->topology||gpu->topology->layouts.size()!=1||gpu->topology->indices.size()!=packet.triangles().size()*3)return {};
  const auto& ref=gpu->topology->layouts[0];
  if(!ref.plan||ref.plan->source_generation!=packet.generation()||ref.matrix_count!=packet.matrices().size()||gpu->batches.size()!=gpu->topology->runs.size())return {};
  auto next=std::allocate_shared<Frame>(ntr::HostAllocator<Frame>{});next->gpu=std::move(gpu);next->source_generation=packet.generation();
  next->run_spans.resize(next->gpu->batches.size());
  const auto& vertices=packet.vertices();const auto& matrices=packet.matrices();auto& counts=next->counts;counts.vertices=vertices.size();counts.matrices=matrices.size();
  const char* option=std::getenv("PLAYER_MIXED_LOCAL_AABB");const bool local=!option||std::strcmp(option,"0")!=0;
  HostVector<Bounds> bounds;HostVector<Zw> zw(vertices.size());
  HostVector<uint32_t> projected_ids(vertices.size(),UINT32_MAX);HostVector<ntr::GxVertex> projected;
  auto extend=[](Bounds& b,const native_mesh::RawVertex& v){const int32_t xyz[3]={v.x,v.y,v.z};
   for(unsigned k=0;k<3;++k){if(!b.seen)b.lo[k]=b.hi[k]=xyz[k];else{b.lo[k]=std::min(b.lo[k],xyz[k]);b.hi[k]=std::max(b.hi[k],xyz[k]);}}b.seen=true;
  };
  auto certify=[&](Bounds& b,const native_mesh::Matrix& m){
   const auto z=range(b,m,2),w=range(b,m,3);counts.classification_products+=12;
   if(!bounded(z)||!bounded(w))return;
   const int64_t delta=int64_t(m.fixed[15])-m.fixed[14];
   if(common_row(m)&&delta>=0&&2*z.first+delta>=0&&z.first+delta>0)b.code=0;
   else if(z.first>=-w.first&&z.second<=w.first&&w.first>0)b.code=0;
   else {
    // A negative W can set BOTH clip bits. Certify the complete mask, never
    // approximate one rejection plane for vertices spanning matrix/groups.
    const float zl=float(z.first)/4096.f,zh=float(z.second)/4096.f,wl=float(w.first)/4096.f,wh=float(w.second)/4096.f;
    const bool near_all=zh<-wh,far_all=zl>wh,near_none=zl>=-wl,far_none=zh<=wl;
    if(near_all&&far_all)b.code=3;
    else if(near_all&&far_none)b.code=1;
    else if(far_all&&near_none)b.code=2;
   }
  };
  if(local){
   for(size_t first=0;first<vertices.size();){
    const auto matrix=vertices[first].matrix;if(matrix>=matrices.size())return {};
    Bounds b;size_t end=first;
    while(end<vertices.size()&&end-first<32&&vertices[end].matrix==matrix){extend(b,vertices[end]);++end;}
    certify(b,matrices[matrix]);++counts.bounds_groups;
    if(b.code!=255)for(size_t vi=first;vi<end;++vi){auto& q=zw[vi];q.code=b.code;q.positive_w=b.code==0;if(q.code)++counts.bounds_rejected_vertices;else ++counts.bounds_inside_vertices;}
    first=end;
   }
  }else{
   bounds.resize(matrices.size());counts.bounds_groups=bounds.size();
   for(const auto& v:vertices){if(v.matrix>=bounds.size())return {};extend(bounds[v.matrix],v);}
   for(size_t i=0;i<bounds.size();++i)if(bounds[i].seen)certify(bounds[i],matrices[i]);
  }
  auto get_code=[&](uint32_t id)->uint8_t{
   auto& q=zw.at(id);if(q.code!=255)return q.code;const auto& v=vertices.at(id);const auto& m=matrices[v.matrix];
   if(!local){const auto& b=bounds[v.matrix];if(b.code!=255){q.code=b.code;q.positive_w=(b.code==0);if(q.code)++counts.bounds_rejected_vertices;else ++counts.bounds_inside_vertices;return q.code;}}
   q.z=row(v,m,2);counts.classification_products+=3;
   const int64_t delta=int64_t(m.fixed[15])-m.fixed[14];
   if(common_row(m)&&int64_t(q.z)+delta>=INT32_MIN&&int64_t(q.z)+delta<=INT32_MAX){q.w=int32_t(int64_t(q.z)+delta);++counts.exact_common_row_vertices;}
   else{q.w=row(v,m,3);counts.classification_products+=3;}
   ++counts.exact_zw_vertices;q.code=code(q.z,q.w);q.positive_w=q.w>0;return q.code;
  };
  auto get_projected=[&](uint32_t id)->const ntr::GxVertex&{
   auto& index=projected_ids.at(id);if(index==UINT32_MAX){index=projected.size();projected.push_back(packet.cpu_vertex(id));++counts.projected_crossing_vertices;}return projected[index];
  };
  size_t run=0;
  for(size_t ti=0;ti<packet.triangles().size();++ti){const auto& t=packet.triangles()[ti];const uint32_t first=uint32_t(ti*3);
   while(run<next->gpu->batches.size() && first>=size_t(next->gpu->batches[run].first)+size_t(next->gpu->batches[run].count))++run;
   if(run==next->gpu->batches.size())return {};const auto& material=next->gpu->batches[run];
   if(first<size_t(material.first)||size_t(first)+3>size_t(material.first)+size_t(material.count))return {};
   uint8_t either=0,both=3;bool positive_w=true;for(uint32_t id:t.id){if(id>=vertices.size())return {};const auto q=get_code(id);either|=q;both&=q;positive_w=positive_w&&zw[id].positive_w;}
   if(both){++counts.rejected_triangles;continue;}
   if(!either&&positive_w){++counts.inside_triangles;add(*next,Stream::NativeIndexed,run,first,3);continue;}
   if(!either)++counts.nonpositive_w_cpu_triangles;
   ++counts.crossing_triangles;DsGpuVertex polygon[8];for(unsigned k=0;k<3;++k)polygon[k]=convert(get_projected(t.id[k]),material);
   const int n=ds_gpu_clip_viewport(polygon);const uint32_t start=next->cpu.size();
   for(int k=1;k+1<n;++k)for(int index:{0,k,k+1}){auto v=polygon[index].gpu;v.x+=v.w/256.f;v.y-=v.w/192.f;next->cpu.push_back(v);}
   const uint32_t count=next->cpu.size()-start;counts.emitted_cpu_vertices+=count;add(*next,Stream::CpuClipped,run,start,count);
  }
  size_t cursor=0;
  for(size_t ri=0;ri<next->run_spans.size();++ri){auto& span=next->run_spans[ri];span.first=cursor;while(cursor<next->draws.size()&&next->draws[cursor].run==ri)++cursor;span.count=cursor-span.first;}
  if(cursor!=next->draws.size())return {};
  return next;
 }catch(const std::bad_alloc&){return {};}
};
}
