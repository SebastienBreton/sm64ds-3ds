#pragma once
// Isolated lease resources. Caller must drain/acquire BEFORE freeze mutates
// these records, and keep them alive through centre plus both stereo eyes.
#include "platform/3ds/gx_geometry_arena.h"
#include <cstring>
#include <limits>
namespace ntr::gx_geometry {
class Resources {
    struct Entry {
        const std::uint32_t* source=nullptr;
        int width=0,height=0;
        std::uint64_t version=0;
        HostVector<std::uint32_t> pixels;
        bool used=false;
        bool key(const V3dsBatch& b)const{
            return width==b.tw && height==b.th && version==b.texture_version &&
                (version || source==b.tex);
        }
    };
    HostVector<Entry> entries;
    std::size_t byte_budget;
    static bool size(const V3dsBatch& b,std::size_t& count){
        if(!b.tex){count=0;return true;}
        if(b.tw<=0 || b.th<=0)return false;
        const auto w=std::size_t(b.tw),h=std::size_t(b.th);
        if(w>std::numeric_limits<std::size_t>::max()/h/sizeof(std::uint32_t))return false;
        count=w*h;return true;
    }
public:
    explicit Resources(std::size_t budget=8*1024*1024):byte_budget(budget){}
    // Caller already proved the same persistent Packet/generation and lease.
    // Validate each external payload once, including every byte.
    bool external_unchanged()const{
        for(const auto& e:entries)if(!e.version && std::memcmp(e.pixels.data(),e.source,e.pixels.size()*4))return false;
        return true;
    }
    // No hashes or sampled validation. Immutable nonzero versions use the
    // existing never-recycled decoded identity; external version0 uses every
    // original byte on each requested reuse.
    bool matches(const Packet& packet)const{
        for(const auto& triangle:packet.triangles()){
            const auto& m=triangle.material;if(!m.tex)continue;
            const V3dsBatch b{m.tex,m.tw,m.th,m.wrap,0,0,false,m.polygon_attr,m.texture_version};
            const Entry* found=nullptr;for(const auto& e:entries)if(e.key(b)){found=&e;break;}
            if(!found)return false;
            if(!m.texture_version && std::memcmp(found->pixels.data(),m.tex,found->pixels.size()*4))return false;
        }
        return true;
    }
    bool freeze(V3dsBatch* batches,std::size_t count){
        if(count && !batches)return false;
        // Input must be fresh GX bindings, not our previous rewritten batches.
        // Refuse a self-owned source before retiring any snapshot it points to.
        for(std::size_t i=0;i<count;++i)for(const auto& e:entries)
            if(batches[i].tex==e.pixels.data())return false;
        // Check the full working set before copying or changing any batch.
        std::size_t required=0;
        for(std::size_t i=0;i<count;++i){const auto& b=batches[i];std::size_t pixels=0;
            if(!size(b,pixels))return false;
            bool first=true;for(std::size_t j=0;j<i;++j){const auto& previous=batches[j];
                if(previous.tex==b.tex && previous.tw==b.tw && previous.th==b.th && previous.texture_version==b.texture_version){first=false;break;}}
            if(first){const auto bytes=pixels*4;if(bytes>byte_budget-required)return false;required+=bytes;}
        }
        // Retire old snapshots only after the caller's GPU drain. Pre-mark all
        // required entries before erasing so live pointers keep their storage.
        for(auto& e:entries){e.used=false;for(std::size_t i=0;i<count;++i)if(batches[i].tex && e.key(batches[i])){e.used=true;break;}}
        entries.erase(std::remove_if(entries.begin(),entries.end(),[](const Entry& e){return !e.used;}),entries.end());
        for(std::size_t i=0;i<count;++i){auto& b=batches[i];if(!b.tex)continue;
            Entry* found=nullptr;for(auto& e:entries)if(e.key(b)){found=&e;break;}
            if(!found){Entry e;e.source=b.tex;e.width=b.tw;e.height=b.th;e.version=b.texture_version;
                const auto pixels=std::size_t(b.tw)*b.th;e.pixels.assign(b.tex,b.tex+pixels);e.used=true;
                entries.push_back(std::move(e));found=&entries.back();
            }else if(!b.texture_version){
                if(std::memcmp(found->pixels.data(),b.tex,found->pixels.size()*4))
                    std::memcpy(found->pixels.data(),b.tex,found->pixels.size()*4);
            }
            b.tex=found->pixels.data();
        }
        return true;
    }
    std::size_t bytes()const{std::size_t total=0;for(const auto& e:entries)total+=e.pixels.size()*4;return total;}
};
}
