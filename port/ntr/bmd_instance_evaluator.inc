// Include after frozen GX implementation: private adapter, never shared source.
#include "ntr/bmd_chunk_transfer.h"
#include "platform/3ds/prepared_mesh_view.h"
#include "ntr/gx_gpu_ingame.h"
#include <memory>

namespace bmd_native {
struct TexturePixels {ntr::HostVector<uint32_t> pixels;};
struct TextureLease {
    const uint32_t* source=nullptr;int width=0,height=0;uint64_t version=0;
    std::shared_ptr<TexturePixels> owner;
};
// Strong, byte-owned snapshots. Exact version0 payload validation remains costed.
// Certified nonzero GX versions are never recycled; no pointer-only cache proof.
class TextureLeases {
    ntr::HostVector<TextureLease> cache_;
    size_t limit_;
public:
    explicit TextureLeases(size_t limit=8*1024*1024):limit_(limit){}
    size_t bytes()const{size_t n=0;for(const auto& e:cache_)n+=e.owner->pixels.size()*4;return n;}
    // Scene handoff (game-coverage): snapshots without a frame/GPU owner can
    // never be bound again once their scene is gone; release them instead of
    // keeping up to limit_ (8 MiB) of the newlib heap across levels.
    void retire_unused(){for(size_t i=0;i<cache_.size();)if(cache_[i].owner.use_count()==1)cache_.erase(cache_.begin()+i);else ++i;}
    bool bind(V3dsBatch& batch,ntr::HostVector<std::shared_ptr<const void>>& owners){
        if(!batch.tex)return true;
        if(batch.tw<=0 || batch.th<=0 || batch.tw>1024 || batch.th>1024)return false;
        const size_t pixels=size_t(batch.tw)*batch.th,needed=pixels*4;
        for(const auto& e:cache_){
            if(e.width!=batch.tw || e.height!=batch.th || e.version!=batch.texture_version ||
               (!e.version && e.source!=batch.tex))continue;
            if(!e.version && std::memcmp(e.owner->pixels.data(),batch.tex,needed))continue;
            owners.push_back(e.owner);batch.tex=e.owner->pixels.data();return true;
        }
        if(needed>limit_)return false;
        // Retire only snapshots without any frame/GPU owner. Live old versions
        // remain immutable even if the same external version0 source changes.
        for(size_t i=0;i<cache_.size() && bytes()>limit_-needed;){
            if(cache_[i].owner.use_count()==1)cache_.erase(cache_.begin()+i);else ++i;
        }
        if(bytes()>limit_-needed)return false;
        TextureLease e;e.source=batch.tex;e.width=batch.tw;e.height=batch.th;e.version=batch.texture_version;
        e.owner=std::allocate_shared<TexturePixels>(ntr::HostAllocator<TexturePixels>{});
        e.owner->pixels.assign(batch.tex,batch.tex+pixels);owners.push_back(e.owner);batch.tex=e.owner->pixels.data();
        cache_.push_back(std::move(e));return true;
    }
};
// gpu-geometry-ingame: bulk ("fast") evaluation of a compiled occurrence.
// A compiled list only contains MTX_RESTORE, COLOR, NORMAL, TEXCOORD,
// BEGIN/END and vertices, so every per-vertex output is a function of a few
// per-occurrence inputs: the restored slot matrices, the entry colour/texcoord,
// the lighting state and the texture matrix. FastPlan records, once per
// compiled program, which COLOR/NORMAL/TEXCOORD command each vertex inherits;
// an occurrence then evaluates every command once (not once per vertex), fills
// the attribute slots from those tables and computes each binding's clip
// matrix once. The GX state left behind is the one the per-command replay
// would leave (last restore, colour, texcoord, primitive, vertex position).
// SM64DS_GX_GPU_INGAME bit 1 (gx_gpu_ingame.h); unset = per-command replay
// (reference). With bit 2 lit vertices carry the transformed normal and the
// PICA vertex shader lights them. GI_VERIFY=1 (bit 2 clear) additionally runs
// the reference replay and compares every output and the final GX state.
namespace gi {
inline bool option(){return gx_gpu_ingame::enabled(gx_gpu_ingame::Bulk);}
inline bool verify(){static const bool value=std::getenv("GI_VERIFY")!=nullptr;return value;}
struct Stats {uint64_t fast=0,slow=0,refused_texmode=0,refused_mask=0,plan_builds=0,verified=0,mismatched=0,lit_gpu=0,lit_cpu=0;
    uint64_t refused_submits=0,refused_words=0,refusal[8]{},refused_commands[256]{};} stats;
struct FastPlan {
    // Per source vertex: 0 = entry state, else 1 + index into colors/texcoords.
    struct Source {uint16_t color=0,tex=0;};
    struct ColorOp {uint32_t param=0;uint8_t binding=32;bool normal=false,before_begin=false;};
    ntr::HostVector<Source> sources;
    ntr::HostVector<ColorOp> colors;
    ntr::HostVector<uint32_t> texcoords;
    int last_restore=-1,last_color=-1,last_texcoord=-1;
    bool any_begin=false,any_vertex=false,normal_before_begin=false,valid=false;
    int16_t vx=0,vy=0,vz=0;
    bool build(const bmd_program::Program& p){
        valid=false;sources.assign(p.source_vertices(),Source{});colors.clear();texcoords.clear();
        unsigned binding=32;int color=-1,tex=-1;bool seen_begin=false;
        for(const auto& op:p.operations()){
            switch(op.command){
            case 0x14:binding=op.parameter&31;last_restore=int(binding);break;
            case 0x20:case 0x21:{
                ColorOp c;c.param=op.parameter;c.binding=uint8_t(binding);c.normal=op.command==0x21;c.before_begin=!seen_begin;
                if(c.normal && !seen_begin)normal_before_begin=true;
                if(colors.size()>=0xfffe)return false;
                color=int(colors.size());colors.push_back(c);break;}
            case 0x22:if(texcoords.size()>=0xfffe)return false;tex=int(texcoords.size());texcoords.push_back(op.parameter);break;
            case 0x40:seen_begin=any_begin=true;break;
            case 0x41:break;
            case 0xff:{
                if(op.source>=sources.size() || op.matrix>=p.bindings().size() || p.bindings()[op.matrix]!=binding)return false;
                sources[op.source]={uint16_t(color+1),uint16_t(tex+1)};
                const auto& position=p.source_position(op.source);vx=position.x;vy=position.y;vz=position.z;any_vertex=true;break;}
            default:return false;
            }
        }
        last_color=color;last_texcoord=tex;valid=true;return true;
    }
};
// DS NORMAL x vector matrix (lighting_vector with an explicit matrix).
inline void transform_normal(const int32_t* vec,uint32_t packed,int out[3]){
    int raw[3];for(int k=0;k<3;++k){raw[k]=(packed>>(10*k))&1023;if(raw[k]>=512)raw[k]-=1024;}
    for(int j=0;j<3;++j){int64_t sum=0;for(int k=0;k<3;++k)sum+=int64_t(raw[k])*vec[k*4+j];
        const unsigned wrapped=unsigned(int(sum>>12))&2047u;out[j]=wrapped>=1024?int(wrapped)-2048:int(wrapped);}
}
inline ntr::gx_gpu::Env current_env(unsigned mask){
    using namespace ntr;gx_gpu::Env e{};
    for(int k=0;k<3;++k){e.dif[k]=uint8_t(g.diffuse[k]);e.amb[k]=uint8_t(g.ambient[k]);e.spe[k]=uint8_t(g.specular[k]);e.emi[k]=uint8_t(g.emission[k]);}
    e.mask=uint8_t(mask&15);
    for(int i=0;i<4;++i){const auto& L=g.lights[i];auto& o=e.light[i];o.recip=L.spec_recip;
        o.dx=int16_t(L.dx);o.dy=int16_t(L.dy);o.dz=int16_t(L.dz);o.r=uint8_t(L.r);o.g=uint8_t(L.g);o.b=uint8_t(L.b);}
    return e;
}
}
// One occurrence owns attributes/matrices/material and leases. Program topology
// is only read here, so two draws of the same list cannot overwrite each other.
class Evaluator {
    std::shared_ptr<bmd_program::Program> program_;
    TextureLeases& textures_;
    ntr::HostVector<bool> written_;
    V3dsBatch material_{};
    bool preflight_done_=false;
    // gpu-geometry-ingame bulk evaluation
    std::shared_ptr<gi::FastPlan>* plan_slot_=nullptr;
    const gi::FastPlan* plan_=nullptr;
    bool fast_=false,slow_verify_=false;
    ntr::HostVector<native_mesh::Attribute> verify_attributes_;
    ntr::HostVector<native_mesh::Matrix> verify_matrices_;
    // GX state fields the bulk path must reproduce (GI_VERIFY).
    struct Snap {
        uint32_t color,active_polygon_attr,light_mask;float u,v;int16_t raw[2],vx,vy,vz;int prim;int32_t pos[16],vec[16];bool clip_dirty;
        static Snap take(){using namespace ntr;Snap s;s.color=g.color;s.active_polygon_attr=g.active_polygon_attr;s.light_mask=g.light_mask;
            s.u=g.u;s.v=g.v;s.raw[0]=g.raw_texcoord[0];s.raw[1]=g.raw_texcoord[1];s.vx=g.vx;s.vy=g.vy;s.vz=g.vz;s.prim=g.prim;
            std::memcpy(s.pos,g.pos.raw,64);std::memcpy(s.vec,g.vec.raw,64);s.clip_dirty=g.clip_dirty;return s;}
        void put()const{using namespace ntr;g.color=color;g.active_polygon_attr=active_polygon_attr;g.light_mask=light_mask;
            g.u=u;g.v=v;g.raw_texcoord[0]=raw[0];g.raw_texcoord[1]=raw[1];g.vx=vx;g.vy=vy;g.vz=vz;g.prim=prim;
            std::memcpy(g.pos.raw,pos,64);std::memcpy(g.vec.raw,vec,64);g.clip_dirty=clip_dirty;}
        bool same(const Snap& o)const{return color==o.color&&active_polygon_attr==o.active_polygon_attr&&light_mask==o.light_mask&&
            std::memcmp(&u,&o.u,4)==0&&std::memcmp(&v,&o.v,4)==0&&raw[0]==o.raw[0]&&raw[1]==o.raw[1]&&vx==o.vx&&vy==o.vy&&vz==o.vz&&prim==o.prim&&
            !std::memcmp(pos,o.pos,64)&&!std::memcmp(vec,o.vec,64);}
    } verify_entry_{};
    ntr::HostVector<uint32_t> colors_;ntr::HostVector<float> uvs_;
    bool gpu_light_=false;ntr::gx_gpu::Env env_{};
public:
    prepared_mesh::Instance instance;
    bool complete=false;
    Evaluator(std::shared_ptr<bmd_program::Program> program,uint64_t identity,TextureLeases& textures,std::shared_ptr<gi::FastPlan>* plan=nullptr):
        program_(std::move(program)),textures_(textures),plan_slot_(plan){
        instance.layout={program_,&program_->layout,identity,program_->bindings().size()};
    }
    bool preflight(const bmd_program::Program& p){
        if(preflight_done_)return true;
        using namespace ntr;
        if((g.mode!=MTX_POS && g.mode!=MTX_POSVEC) || g_queued || g_have || g_port_have ||
           g.prim!=-1 || !g.strip.empty() || !mesh_strip.empty() ||
           g.vp_x || g.vp_y || g.vp_w!=256 || g.vp_h!=192 ||
           g.proj.f(3)!=0 || g.proj.f(7)!=0 || g.proj.f(11)==0 || g.proj.f(15)!=0)return false;
        const unsigned mode=(g.polygon_attr>>4)&3;
        if((mode!=0 && mode!=3) || !(g.polygon_attr&0x1f0000) || (g.polygon_attr&(1u<<14)))return false;
        if(p.layout.positions.size()>32768 || p.layout.indices.size()>32768)return false;
        material_={g.tex_rgba,g.tw,g.th,g.wrap,0,0,g.texture_version==0,g.polygon_attr,g.texture_version};
        preflight_done_=textures_.bind(material_,instance.texture_owners);
        if(preflight_done_ && gi::option() && plan_slot_){
            auto& slot=*plan_slot_;
            if(!slot){slot=std::allocate_shared<gi::FastPlan>(HostAllocator<gi::FastPlan>{});slot->build(p);++gi::stats.plan_builds;}
            plan_=slot->valid?slot.get():nullptr;
            fast_=plan_!=nullptr;
            if(fast_ && g.texmode>1){fast_=false;++gi::stats.refused_texmode;}
            // NORMAL before the first BEGIN lights with the previous BEGIN's mask.
            if(fast_ && plan_->normal_before_begin && (g.light_mask&15)!=(g.polygon_attr&15)){fast_=false;++gi::stats.refused_mask;}
            if(fast_)++gi::stats.fast;else ++gi::stats.slow;
        }
        return preflight_done_;
    }
private:
    // Evaluate every COLOR/NORMAL/TEXCOORD command once for this occurrence.
    void evaluate(const bmd_program::Program& p,ntr::HostVector<native_mesh::Attribute>& attributes,ntr::HostVector<native_mesh::Matrix>& matrices){
        using namespace ntr;const auto& plan=*plan_;
        const auto& bindings=p.bindings();
        matrices.resize(bindings.size());
        for(size_t k=0;k<bindings.size();++k){
            const unsigned slot=bindings[k];
            clip_sparse::multiply(slot>=32?g.pos.raw:g.pos_stack[slot].raw,g.proj.raw,matrices[k].fixed);
        }
        gpu_light_=gx_gpu_ingame::gpu_light();
        env_=gi::current_env(g.polygon_attr);
        const auto entry_env=gi::current_env(g.light_mask);
        colors_.resize(plan.colors.size());bool lit_packed=false;
        for(size_t i=0;i<plan.colors.size();++i){
            const auto& c=plan.colors[i];
            if(!c.normal){colors_[i]=bgr555_to_argb(uint16_t(c.param&0x7fff));continue;}
            int n[3];gi::transform_normal(c.binding>=32?g.vec.raw:g.vec_stack[c.binding].raw,c.param,n);
            const auto& e=c.before_begin?entry_env:env_;
            uint32_t packed;
            if(gpu_light_ && !c.before_begin && !g.use_shininess && gx_gpu_ingame::pack_normal(n,packed)){colors_[i]=packed;lit_packed=true;++gi::stats.lit_gpu;}
            else{colors_[i]=bgr555_to_argb(gx_gpu::light_color(e,n,g.shininess,g.use_shininess));++gi::stats.lit_cpu;}
        }
        uvs_.resize(plan.texcoords.size()*2);
        for(size_t i=0;i<plan.texcoords.size();++i){
            const uint32_t param=plan.texcoords[i];const int16_t s=int16_t(param&0xffff),t=int16_t(param>>16);
            if(g.texmode==1)generated_texcoord(s,t,1,1,12,false,uvs_[2*i],uvs_[2*i+1]);
            else{uvs_[2*i]=s/16.f;uvs_[2*i+1]=t/16.f;}
        }
        if(lit_packed && &attributes==&instance.attributes){instance.env=env_;instance.lit=true;}
        attributes.resize(p.layout.positions.size());
        const uint32_t entry_color=g.color;const float eu=g.u,ev=g.v;
        for(size_t s=0;s<plan.sources.size();++s){
            const auto& src=plan.sources[s];
            native_mesh::Attribute a{src.color?colors_[src.color-1]:entry_color,src.tex?uvs_[2*(src.tex-1)]:eu,src.tex?uvs_[2*(src.tex-1)+1]:ev};
            for(auto output:p.output_slots(unsigned(s)))attributes[output]=a;
        }
    }
    // The GX state the per-command replay leaves behind.
    void apply_final(const bmd_program::Program& p){
        using namespace ntr;const auto& plan=*plan_;
        if(plan.last_restore>=0){
            g.pos=g.pos_stack[plan.last_restore];g.vec=g.vec_stack[plan.last_restore];
            g.clip_dirty=true;g.texture_dirty=true;mesh_matrix_valid=false;
        }
        if(plan.last_color>=0){
            const auto& c=plan.colors[plan.last_color];
            if(!c.normal)g.color=bgr555_to_argb(uint16_t(c.param&0x7fff));
            else if(gpu_light_ && gx_gpu_ingame::packed_normal(colors_[plan.last_color])){
                int n[3];gx_gpu_ingame::unpack_normal(colors_[plan.last_color],n);
                g.color=bgr555_to_argb(gx_gpu::light_color(c.before_begin?gi::current_env(g.light_mask):env_,n,g.shininess,g.use_shininess));
            }else g.color=colors_[plan.last_color];
            if(c.normal)g.terms_dirty=true;
        }
        if(plan.last_texcoord>=0){
            const uint32_t param=plan.texcoords[plan.last_texcoord];
            g.raw_texcoord[0]=int16_t(param&0xffff);g.raw_texcoord[1]=int16_t(param>>16);
            g.u=uvs_[2*plan.last_texcoord];g.v=uvs_[2*plan.last_texcoord+1];
        }
        if(plan.any_begin){
            g.prim=-1;g.active_polygon_attr=g.polygon_attr;g.light_mask=g.active_polygon_attr&15;
            g.strip.clear();g_strip_ids.clear();mesh_strip.clear();g.strip_parity=0;
        }
        if(plan.any_vertex){g.vx=plan.vx;g.vy=plan.vy;g.vz=plan.vz;}
        (void)p;
    }
public:
    void begin(const bmd_program::Program& p){
        instance.attributes.resize(p.layout.positions.size());instance.matrices.resize(p.bindings().size());
        written_.assign(p.bindings().size(),false);instance.batches.clear();
        instance.batches.reserve(p.layout.runs.size());complete=false;
        slow_verify_=false;
        if(fast_){
            if(gi::verify() && !gx_gpu_ingame::gpu_light()){slow_verify_=true;verify_entry_=Snap::take();evaluate(p,verify_attributes_,verify_matrices_);}
            else evaluate(p,instance.attributes,instance.matrices);
        }
    }
    void state(uint8_t command,uint32_t parameter){if(fast_ && !slow_verify_)return;ntr::exec(command,&parameter,command==0x41?0:1);}
    void vertex(const bmd_program::Op& op,bmd_program::Program& p){
        if(fast_ && !slow_verify_)return;
        using namespace ntr;const auto& position=p.source_position(op.source);
        g.vx=position.x;g.vy=position.y;g.vz=position.z;ensure_mesh_clip();
        if(!written_[op.matrix]){
            std::memcpy(instance.matrices[op.matrix].fixed,g.clip,64);written_[op.matrix]=true;
        }
        float u=g.u,v=g.v;if(g.texmode==3)generated_texcoord(position.x,position.y,position.z,0,24,true,u,v);
        for(auto output:p.output_slots(op.source))instance.attributes[output]={g.color,u,v};
    }
    void checkpoint(bmd_program::Program& p,const bmd_program::DecoderExit& d,unsigned previous){
        using namespace ntr;for(unsigned i=0;i<4;++i)g_queue[i]=d.queue[i];
        g_queued=d.queued;g_qpos=d.qpos;g_have=d.have;
        for(unsigned i=0;i<d.params_written;++i)g_params[i]=d.params[i];
        for(unsigned i=previous;i<d.words_consumed;++i)gx_stream_note(p.words()[i]);
    }
    void finish_chunked(bmd_program::Program& p){
        if(fast_ && !slow_verify_)apply_final(p);
        if(slow_verify_){
            using namespace ntr;
            bool same=verify_attributes_.size()==instance.attributes.size() && verify_matrices_.size()==instance.matrices.size() &&
                !std::memcmp(verify_attributes_.data(),instance.attributes.data(),instance.attributes.size()*sizeof(native_mesh::Attribute)) &&
                !std::memcmp(verify_matrices_.data(),instance.matrices.data(),instance.matrices.size()*sizeof(native_mesh::Matrix));
            // Final state: keep the reference, re-derive it from the entry state with the plan.
            const Snap reference=Snap::take();verify_entry_.put();apply_final(p);
            const Snap bulk=Snap::take();reference.put();
            if(!bulk.same(reference)){same=false;if(gi::stats.mismatched<8)std::printf("GI_VERIFY final state mismatch color=%08x/%08x prim=%d/%d\n",unsigned(reference.color),unsigned(bulk.color),reference.prim,bulk.prim);}
            ++gi::stats.verified;
            if(!same){
                if(gi::stats.mismatched<8){
                    for(size_t i=0;i<instance.attributes.size();++i){const auto& x=instance.attributes[i];const auto& y=verify_attributes_[i];
                        if(std::memcmp(&x,&y,sizeof x)){std::printf("GI_VERIFY attr mismatch slot=%u ref=%08x,%g,%g fast=%08x,%g,%g texmode=%u\n",unsigned(i),unsigned(x.color),x.u,x.v,unsigned(y.color),y.u,y.v,unsigned(g.texmode));break;}}
                    for(size_t i=0;i<instance.matrices.size();++i)if(std::memcmp(&instance.matrices[i],&verify_matrices_[i],64)){std::printf("GI_VERIFY matrix mismatch %u\n",unsigned(i));break;}
                }
                ++gi::stats.mismatched;
            }
        }
        for(const auto& r:p.layout.runs){auto b=material_;b.first=r.batch.first;b.count=r.batch.count;instance.batches.push_back(b);}
        complete=true;
    }
};
}
