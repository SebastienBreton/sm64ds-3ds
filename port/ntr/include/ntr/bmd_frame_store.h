#pragma once
#include "platform/3ds/prepared_mesh_view.h"
#include "ntr/gx_geometry_packet.h"
#include <type_traits>
#include <cstring>
#include <cstdlib>

namespace bmd_native {
// Existing Planner is the only persisted GPU geometry representation. RawPacket
// survives only for an unsupported legacy segment's cold CPU fallback.
struct LegacyLayout {native_mesh::Planner plan;};
struct LegacyFallback {
    size_t occurrence=0;std::shared_ptr<const native_mesh::Packet> packet;
    ntr::HostVector<std::shared_ptr<const void>> texture_owners;
};
class FrameStore {
    prepared_mesh::Instances instances_;
    ntr::HostVector<LegacyFallback> refused_;
    prepared_mesh::Composer composer_;
    std::shared_ptr<const prepared_mesh::Frame> frame_;
    uint64_t revision_=0,composed_revision_=~uint64_t(0);
    // castle-30fps: legacy (uncompiled) occurrences get a fresh identity per
    // frame, so any frame containing one rebuilt the composed topology and
    // the mixed-clip plan. An occurrence whose plan geometry equals one of the
    // previous frame's reuses that layout (owner+identity); its per-frame
    // attributes/matrices/batches still come from the current instance.
    struct RecycledLayout {std::shared_ptr<LegacyLayout> owner;uint64_t identity=0;};
    ntr::HostVector<RecycledLayout> legacy_previous_,legacy_current_;
    static bool same_layout(const native_mesh::Planner& a,const native_mesh::Planner& b){
        if(a.positions.size()!=b.positions.size() || a.indices.size()!=b.indices.size() || a.runs.size()!=b.runs.size())return false;
        if(!a.positions.empty() && std::memcmp(a.positions.data(),b.positions.data(),a.positions.size()*sizeof(native_mesh::Position)))return false;
        if(!a.indices.empty() && std::memcmp(a.indices.data(),b.indices.data(),a.indices.size()*sizeof(uint16_t)))return false;
        for(size_t i=0;i<a.runs.size();++i){const auto& x=a.runs[i];const auto& y=b.runs[i];
            if(x.matrix_count!=y.matrix_count || x.matrices!=y.matrices || x.batch.first!=y.batch.first || x.batch.count!=y.batch.count ||
               x.batch.polygon_attr!=y.batch.polygon_attr || x.batch.tw!=y.batch.tw || x.batch.th!=y.batch.th || x.batch.wrap!=y.batch.wrap)return false;}
        return true;
    }
public:
    uint64_t revision()const{return revision_;}
    size_t occurrences()const{return instances_.size();}
    const auto& instances()const{return instances_;}
    size_t triangle_count() const {
        size_t total=0,refused=0;
        for(size_t i=0;i<instances_.size();++i){
            if(refused<refused_.size() && refused_[refused].occurrence==i){
                total+=refused_[refused++].packet->triangles().size();continue;
            }
            for(const auto& run:instances_[i].layout.plan->runs)total+=size_t(run.batch.count)/3;
        }
        return total;
    }
    void clear(){instances_.clear();refused_.clear();frame_.reset();++revision_;composed_revision_=~uint64_t(0);
        legacy_previous_.swap(legacy_current_);legacy_current_.clear();}
    // Called before admitting any live state command. No logical frame change.
    bool reserve_occurrence() noexcept {
        try { instances_.reserve(instances_.size()+1); return true; }
        catch (const std::bad_alloc&) { return false; }
    }
    // The synchronous admission contract excludes nested submissions.
    bool append_reserved(prepared_mesh::Instance&& instance) noexcept {
        static_assert(std::is_nothrow_move_constructible<prepared_mesh::Instance>::value, "Instance move must not allocate");
        if (instances_.size()==instances_.capacity()) return false;
        instances_.push_back(std::move(instance));++revision_;frame_.reset();return true;
    }
    void append(prepared_mesh::Instance instance){instances_.push_back(std::move(instance));++revision_;frame_.reset();}
    template<class TextureLeases> bool append_legacy(const native_mesh::Packet& packet,uint64_t identity,TextureLeases& textures){
        if(packet.triangles().empty())return true;
        // Commit of either a compiled or refused occurrence must not allocate.
        instances_.reserve(instances_.size()+1);
        refused_.reserve(refused_.size()+1);
        auto owner=std::allocate_shared<LegacyLayout>(ntr::HostAllocator<LegacyLayout>{});
        prepared_mesh::Instance instance;
        bool valid=owner->plan.build(packet);
        if(valid){
            static const bool recycle=std::getenv("PLAYER_NO_LEGACY_LAYOUT_RECYCLE")==nullptr;
            if(recycle)for(auto& old:legacy_previous_)if(old.owner && same_layout(old.owner->plan,owner->plan)){
                // Attributes are per frame: keep the fresh ones, reuse geometry.
                old.owner->plan.attributes=std::move(owner->plan.attributes);
                owner=std::move(old.owner);identity=old.identity;break;
            }
            legacy_current_.push_back({owner,identity});
            instance.layout={owner,&owner->plan,identity,packet.matrices().size()};
            instance.attributes=std::move(owner->plan.attributes);instance.matrices=packet.matrices();
            instance.wide_verified=true; // admitted packet: wide matrices verified per vertex
            for(const auto& run:owner->plan.runs){
                auto b=run.batch;if(!textures.bind(b,instance.texture_owners)){valid=false;break;}
                instance.batches.push_back(b);
            }
        }
        if(!valid){
            auto raw=std::allocate_shared<native_mesh::Packet>(ntr::HostAllocator<native_mesh::Packet>{});raw->clear(packet.requested());
            ntr::HostVector<std::shared_ptr<const void>> owners;
            for(const auto& matrix:packet.matrices())raw->matrix(matrix.fixed);
            for(const auto& vertex:packet.vertices())raw->vertex(vertex);
            for(const auto& t:packet.triangles()){
                const auto& m=t.material;V3dsBatch b{m.tex,m.tw,m.th,m.wrap,0,0,m.texture_version==0,m.polygon_attr,m.texture_version};
                // Resource failure does not append/clear a partially snapshotted
                // legacy packet; caller must keep the original current packet.
                if(!textures.bind(b,owners))return false;
                raw->triangle(t.id[0],t.id[1],t.id[2],{b.tex,b.tw,b.th,b.wrap,b.polygon_attr,b.texture_version});
            }
            refused_.push_back({instances_.size(),raw,std::move(owners)});instance={};
        }
        append(std::move(instance));return valid;
    }
    std::shared_ptr<const prepared_mesh::Frame> prepared(){
        if(!refused_.empty())return {};
        if(composed_revision_!=revision_){frame_=composer_.compose(instances_);composed_revision_=revision_;}
        return frame_;
    }
    // The frame prepared() would return now without recomposing (or null).
    const prepared_mesh::Frame* current()const{return refused_.empty() && composed_revision_==revision_?frame_.get():nullptr;}
    uint64_t topology_builds()const{return composer_.builds();}
    uint64_t topology_hits()const{return composer_.hits();}
    static ntr::GxVertex cpu_vertex(const native_mesh::Position& p,const native_mesh::Attribute& a,const native_mesh::Matrix& m){
        float c[4];for(unsigned j=0;j<4;++j){
            const int64_t sum=int64_t(p.x)*m.fixed[j]+int64_t(p.y)*m.fixed[4+j]+int64_t(p.z)*m.fixed[8+j]+4096LL*m.fixed[12+j];
            c[j]=int32_t(sum>>12)/4096.f;
        }
        ntr::GxVertex v{};v.cx=c[0];v.cy=c[1];v.cz=c[2];v.cw=v.w=c[3];const float iw=std::fabs(c[3])>1e-6f?1.f/c[3]:0.f;
        v.x=(c[0]*iw+1.f)*128.f;v.y=(1.f-(c[1]*iw+1.f)*.5f)*192.f;v.z=(c[2]*iw+1.f)*.5f;
        v.u=a.u;v.v=a.v;v.color=a.color;return v;
    }
    static uint32_t append_raw(ntr::gx_geometry::Packet& out,const native_mesh::Packet& packet){
        const uint32_t base=out.vertices().size();
        for(uint32_t i=0;i<packet.vertices().size();++i)out.vertex(packet.cpu_vertex(i));
        for(const auto& t:packet.triangles())out.triangle(base+t.id[0],base+t.id[1],base+t.id[2],t.material);
        return base;
    }
    // Lazy exact existing native-mesh CPU reconstruction, never on GPU success.
    // Current raw vertices retain stable IDs for an unfinished strip on fallback.
    uint32_t cpu_packet(ntr::gx_geometry::Packet& out,const native_mesh::Packet& current)const{
        out.clear();size_t refused=0;
        for(size_t i=0;i<instances_.size();++i){
            if(refused<refused_.size() && refused_[refused].occurrence==i){append_raw(out,*refused_[refused++].packet);continue;}
            const auto& instance=instances_[i];const auto& plan=*instance.layout.plan;
            for(size_t r=0;r<plan.runs.size();++r){const auto& run=plan.runs[r];const auto& b=instance.batches[r];
                ntr::gx_geometry::Material material{b.tex,b.tw,b.th,b.wrap,b.polygon_attr,b.texture_version};
                for(int k=0;k<run.batch.count;k+=3){uint32_t id[3];
                    for(unsigned corner=0;corner<3;++corner){
                        const auto index=plan.indices[run.batch.first+k+corner];const auto& p=plan.positions[index];
                        auto attribute=instance.attributes[index];
                        // gpu-geometry-ingame GpuLight: exact CPU colour of a packed normal.
                        if(instance.lit && gx_gpu_ingame::packed_normal(attribute.color))attribute.color=gx_gpu_ingame::lit_argb(instance.env,attribute.color);
                        id[corner]=out.vertex(cpu_vertex(p,attribute,instance.matrices[run.matrices[p.matrix_offset/4]]));
                    }
                    out.triangle(id[0],id[1],id[2],material);
                }
            }
        }
        return append_raw(out,current);
    }
};
}
