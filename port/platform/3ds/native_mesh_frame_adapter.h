#pragma once
#include "prepared_mesh_view.h"
#include <algorithm>

namespace native_mesh {
// Snapshot at the completed raw-frame boundary, before gx_begin_frame can
// retire decoded textures. No mutable raw Packet address is a raster key.
// capture is restricted to the root producer global Packet, whose lifetime is
// the process and whose generation never resets on gx_reset/clear. Reset the
// driver adapter on gx_reset and fallback backend changes. A destroyed/recreated
// arbitrary Packet at the same address is NOT admitted by this contract.
class FrameAdapter {
    struct Pixels {ntr::HostVector<uint32_t> data;};
    struct Texture {
        const uint32_t* source=nullptr;
        int width=0,height=0;uint64_t version=0;
        std::weak_ptr<const Pixels> owner;
    };
    struct Layout {Planner plan;};
    ntr::HostVector<Texture> textures_;
    ntr::HostVector<Texture> external_;
    const Packet* source_=nullptr;uint64_t generation_=UINT64_MAX,identity_=0;
    std::shared_ptr<const prepared_mesh::Frame> frame_;
    prepared_mesh::Composer composer_;
    size_t budget_;
    bool external_unchanged()const {
        for(const auto& t:external_){auto p=t.owner.lock();
            if(!p || std::memcmp(p->data.data(),t.source,p->data.size()*4))return false;
        }return true;
    }
    bool bind(V3dsBatch& b,ntr::HostVector<std::shared_ptr<const void>>& owners){
        if(!b.tex)return true;
        if(b.tw<=0||b.th<=0||b.tw>1024||b.th>1024)return false;
        const size_t size=size_t(b.tw)*b.th,bytes=size*4;
        std::shared_ptr<const Pixels> selected;
        Texture record{b.tex,b.tw,b.th,b.texture_version,{}};
        textures_.erase(std::remove_if(textures_.begin(),textures_.end(),[](const Texture& t){return t.owner.expired();}),textures_.end());
        for(const auto& t:textures_){
            if(t.width!=b.tw||t.height!=b.th||t.version!=b.texture_version||(!t.version&&t.source!=b.tex))continue;
            auto p=t.owner.lock();if(!p)continue;
            if(!t.version&&std::memcmp(p->data.data(),b.tex,bytes))continue;
            selected=p;record=t;break;
        }
        if(!selected){
            size_t live=0;for(const auto& t:textures_)if(auto p=t.owner.lock())live+=p->data.size()*4;
            if(bytes>budget_ || live>budget_-bytes)return false;
            auto p=std::allocate_shared<Pixels>(ntr::HostAllocator<Pixels>{});p->data.assign(b.tex,b.tex+size);
            selected=p;record.owner=p;textures_.push_back(record);
        }
        // Fresh request source, not a previous rewritten snapshot, validates
        // external version0 on the next HELD capture. Its caller lifetime must
        // continue until generation/reset; nonzero GX epochs need no reread.
        if(!b.texture_version){record.source=b.tex;record.owner=selected;external_.push_back(record);}
        owners.push_back(selected);b.tex=selected->data.data();return true;
    }
public:
    struct Publication {
        std::shared_ptr<const prepared_mesh::Frame> frame;
        float convergence=0;
        uint64_t geometry_generation=0;
        explicit operator bool()const{return bool(frame);}
    };
    uint64_t builds=0,held_hits=0,refused=0;
    explicit FrameAdapter(size_t budget=8*1024*1024):budget_(budget){}
    void reset(){frame_.reset();composer_=prepared_mesh::Composer{};source_=nullptr;generation_=UINT64_MAX;external_.clear();}
    Publication capture(const Packet& packet,float published_convergence) try {
        if(!packet.admitted() || !std::isfinite(published_convergence)){++refused;return {};}
        if(source_==&packet && generation_==packet.generation() && frame_ && external_unchanged()){
            ++held_hits;return {frame_,published_convergence,generation_};
        }
        // Build transactionally. Preserve the previous publication and all its
        // immutable bytes on resource/capacity failure; driver uses raw fallback.
        auto layout=std::allocate_shared<Layout>(ntr::HostAllocator<Layout>{});
        if(!layout->plan.build(packet)){++refused;return {};}
        prepared_mesh::Instance instance;
        instance.layout={layout,&layout->plan,++identity_,packet.matrices().size()};
        instance.attributes=layout->plan.attributes;
        instance.matrices.assign(packet.matrices().begin(),packet.matrices().end());
        auto old_external=std::move(external_);external_.clear();
        for(const auto& run:layout->plan.runs){auto batch=run.batch;
            if(!bind(batch,instance.texture_owners)){external_=std::move(old_external);++refused;return {};}
            instance.batches.push_back(batch);
        }
        prepared_mesh::Instances instances;instances.push_back(std::move(instance));
        auto next=composer_.compose(instances);
        if(!next){external_=std::move(old_external);++refused;return {};}
        frame_=std::move(next);source_=&packet;generation_=packet.generation();++builds;
        return {frame_,published_convergence,generation_};
    }catch(const std::bad_alloc&){
        // CPU-side allocation failure does not mutate GPU storage. Retire the
        // source key, so no later reuse can trust incomplete external metadata.
        source_=nullptr;generation_=UINT64_MAX;external_.clear();++refused;return {};
    }
};
}
