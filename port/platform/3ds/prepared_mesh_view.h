#pragma once
#include "native_mesh_packet.h"
#include "ntr/gx_gpu_ingame.h"
#include <memory>
#include <utility>

namespace prepared_mesh {
using native_mesh::HostVector;
// Producer contract: plan and its positions/indices/run topology are immutable
// for this strong owner + identity. A changed source creates a new identity.
struct LayoutRef {
    std::shared_ptr<const void> owner;
    const native_mesh::Planner* plan=nullptr;
    uint64_t identity=0;
    size_t matrix_count=0;
    bool same(const LayoutRef& b)const{
        return owner.get()==b.owner.get() && plan==b.plan && identity==b.identity && matrix_count==b.matrix_count;
    }
};
struct Instance {
    LayoutRef layout;
    HostVector<native_mesh::Attribute> attributes;
    HostVector<native_mesh::Matrix> matrices;
    HostVector<V3dsBatch> batches;
    // Every nonnull texture binding must remain immutable/alive through these
    // leases, including while a frame is HELD. These are resource owners, not
    // arbitrary generation tokens. The consumer additionally freezes resources.
    HostVector<std::shared_ptr<const void>> texture_owners;
    // gpu-geometry-ingame GpuLight: attribute colours with A < 255 hold packed
    // transformed normals lit with this DS lighting state (gx_gpu_ingame.h).
    ntr::gx_gpu::Env env{};
    bool lit=false;
    // castle-min30: legacy (raw GX) occurrence whose packet checked every
    // vertex of each out-of-range matrix (native_mesh::Packet::vertex).
    bool wide_verified=false;
};
using Instances=HostVector<Instance>;
struct Topology {
    HostVector<LayoutRef> layouts;
    HostVector<native_mesh::Position> positions;
    HostVector<uint16_t> indices;
    HostVector<native_mesh::Run> runs;
};
struct Frame {
    bool wide_verified=false; // castle-min30: out-of-range matrices were verified per vertex by compose()
    std::shared_ptr<const Topology> topology;
    HostVector<native_mesh::Attribute> attributes;
    HostVector<native_mesh::Matrix> matrices;
    HostVector<V3dsBatch> batches;
    HostVector<std::shared_ptr<const void>> texture_owners;
    // gpu-geometry-ingame GpuLight: lighting state per batch (index into envs,
    // -1 none); empty when no occurrence carries packed normals.
    HostVector<int16_t> batch_env;
    HostVector<ntr::gx_gpu::Env> envs;
    const ntr::gx_gpu::Env* env_of(size_t batch)const{
        return batch<batch_env.size() && batch_env[batch]>=0?&envs[size_t(batch_env[batch])]:nullptr;
    }
};
class Composer {
    std::shared_ptr<const Topology> cached_;
    uint64_t builds_=0,hits_=0;
    static bool layout_valid(const LayoutRef& ref){
        if(!ref.owner || !ref.plan || !ref.identity)return false;
        const auto& p=*ref.plan;
        if(p.positions.size()>32768 || p.indices.size()>32768)return false;
        size_t covered=0;
        for(const auto& r:p.runs){
            if(!r.matrix_count || r.matrix_count>16 || r.batch.first<0 || r.batch.count<=0 || r.batch.count%3 ||
               size_t(r.batch.first)!=covered || size_t(r.batch.count)>p.indices.size()-covered)return false;
            for(unsigned k=0;k<r.matrix_count;++k)if(r.matrices[k]>=ref.matrix_count)return false;
            for(int k=0;k<r.batch.count;++k){
                const auto index=p.indices[size_t(r.batch.first)+k];
                if(index>=p.positions.size())return false;
                const auto slot=p.positions[index].matrix_offset;
                if(slot%4 || slot/4>=r.matrix_count)return false;
            }
            covered+=r.batch.count;
        }
        return covered==p.indices.size();
    }
public:
    uint64_t builds()const{return builds_;}uint64_t hits()const{return hits_;}
    std::shared_ptr<const Frame> compose(const Instances& input){
        bool same=bool(cached_) && cached_->layouts.size()==input.size();
        if(same)for(size_t i=0;i<input.size();++i)if(!cached_->layouts[i].same(input[i].layout)){same=false;break;}
        size_t vertices=0,indices=0,matrix_count=0;
        // Refuse before changing the retained topology or returning a frame.
        for(const auto& instance:input){
            const auto& ref=instance.layout;
            if(!ref.owner || !ref.plan || !ref.identity || (!same && !layout_valid(ref)))return {};
            const auto& p=*ref.plan;
            if(p.positions.size()>32768-vertices || p.indices.size()>32768-indices || ref.matrix_count>32768-matrix_count ||
               instance.attributes.size()!=p.positions.size() || instance.matrices.size()!=ref.matrix_count || instance.batches.size()!=p.runs.size())return {};
            vertices+=p.positions.size();indices+=p.indices.size();matrix_count+=ref.matrix_count;
            for(const auto& a:instance.attributes)if(!std::isfinite(a.u)||!std::isfinite(a.v))return {};
            for(const auto& m:instance.matrices)if(!gx_gpu_ingame::matrix_in_range(m.fixed) &&
                !(instance.wide_verified && native_mesh::wide_matrix_ok(m.fixed)))return {};
            bool textured=false;
            for(size_t i=0;i<instance.batches.size();++i){
                const auto& b=instance.batches[i];const auto& r=p.runs[i];const unsigned mode=(b.polygon_attr>>4)&3;
                if(b.first!=r.batch.first || b.count!=r.batch.count || (mode!=0&&mode!=3) || !(b.polygon_attr&0x1f0000) || (b.polygon_attr&(1u<<14)))return {};
                if(b.tex){textured=true;if(b.tw<=0||b.th<=0||b.tw>1024||b.th>1024)return {};}
            }
            if(textured && instance.texture_owners.empty())return {};
            for(const auto& owner:instance.texture_owners)if(!owner)return {};
        }
        std::shared_ptr<Topology> next;
        if(!same){
            next=std::allocate_shared<Topology>(ntr::HostAllocator<Topology>{});next->positions.reserve(vertices);next->indices.reserve(indices);
            size_t vertex_base=0,index_base=0,matrix_base=0;
            for(const auto& instance:input){
                const auto& p=*instance.layout.plan;next->layouts.push_back(instance.layout);
                next->positions.insert(next->positions.end(),p.positions.begin(),p.positions.end());
                for(auto index:p.indices)next->indices.push_back(uint16_t(vertex_base+index));
                for(auto run:p.runs){
                    run.batch.first+=index_base;
                    // Topology must never retain borrowed texture/material data.
                    run.batch.tex=nullptr;run.batch.tw=run.batch.th=0;run.batch.texture_version=0;
                    for(unsigned k=0;k<run.matrix_count;++k)run.matrices[k]+=matrix_base;
                    next->runs.push_back(run);
                }
                vertex_base+=p.positions.size();index_base+=p.indices.size();matrix_base+=instance.layout.matrix_count;
            }
        }
        auto frame=std::allocate_shared<Frame>(ntr::HostAllocator<Frame>{});frame->topology=same?cached_:next;
        frame->attributes.reserve(vertices);frame->matrices.reserve(matrix_count);
        size_t index_base=0;
        bool any_lit=false;size_t lit_count=0,batch_count=0;
        for(const auto& instance:input){any_lit|=instance.lit;lit_count+=instance.lit;batch_count+=instance.batches.size();}
        if(lit_count>=0x7fff)return {};
        if(any_lit){frame->batch_env.reserve(batch_count);frame->envs.reserve(lit_count);}
        for(const auto& instance:input){
            if(any_lit){
                int16_t env=-1;
                if(instance.lit){env=int16_t(frame->envs.size());frame->envs.push_back(instance.env);}
                frame->batch_env.insert(frame->batch_env.end(),instance.batches.size(),env);
            }
            frame->attributes.insert(frame->attributes.end(),instance.attributes.begin(),instance.attributes.end());
            frame->matrices.insert(frame->matrices.end(),instance.matrices.begin(),instance.matrices.end());
            for(auto batch:instance.batches){batch.first+=index_base;frame->batches.push_back(batch);}
            frame->texture_owners.insert(frame->texture_owners.end(),instance.texture_owners.begin(),instance.texture_owners.end());
            index_base+=instance.layout.plan->indices.size();
        }
        for(const auto& instance:input)frame->wide_verified|=instance.wide_verified;
        if(same)++hits_;else{cached_=next;++builds_;}
        return frame;
    }
};
}
