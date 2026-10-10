// GPU geometry mode public API (gpu-geometry-menu). Included at the end of
// gx.cpp, outside its anonymous namespace. See gx_gpu_geometry.h.
namespace ntr {
namespace gx_gpu {
std::uint16_t light_color(const Env& e,const int normal[3],const std::uint8_t* shininess,bool use_shininess){
    std::uint32_t c[3]={std::uint32_t(e.emi[0])<<14,std::uint32_t(e.emi[1])<<14,std::uint32_t(e.emi[2])<<14};
    for(int i=0;i<4;++i){
        if(!((e.mask>>i)&1))continue;
        const auto& L=e.light[i];
        const int lc[3]={L.r,L.g,L.b};
        const int dot=((L.dx*normal[0])>>9)+((L.dy*normal[1])>>9)+((L.dz*normal[2])>>9);
        int shine=0;
        if(dot>0){
            const int halfway=signed11(dot+normal[2]);
            const int square=((halfway*halfway)>>10)&1023;
            shine=((square*L.recip)>>8)-512;
            if(shine<0)shine=0;
            else{shine&=16383;if(shine>=8192)shine-=16384;shine=std::max(0,std::min(511,shine));}
        }
        if(use_shininess && shininess)shine=int(shininess[shine>>2])<<1;
        if(dot>0){const int d=signed11(dot);for(int k=0;k<3;++k)c[k]+=(e.dif[k]*lc[k]*d)&0xfffff;}
        for(int k=0;k<3;++k)c[k]+=e.spe[k]*lc[k]*shine+((e.amb[k]<<9)*lc[k]);
    }
    std::uint16_t color=0;
    for(int k=0;k<3;++k){const std::uint32_t value=c[k]>>14;color|=(value>31?31:value)<<(5*k);}
    // toon env (face-toy): ToonTable[red]; highlight env: grey red base (the
    // additive ToonTable[red] term is a GPU pass, not part of the vertex colour)
    return e.toon==1?e.table[color&31]:e.toon==2?std::uint16_t((color&31)*0x421u):color;
}
}

void gx_gpu_storage(void* (*allocate)(std::size_t),std::size_t vertex_capacity){
    if(gpu_rec::storage_ready)return;
    gpu_rec::storage_allocate=allocate;gpu_rec::storage_capacity=vertex_capacity;
}
// GPU-mode command execution: the frequent simple commands without the generic
// exec() prologue (same state changes as exec + gpu_rec::command; matrix-mode
// and restore do not touch the texture matrix, so its cache stays valid).
namespace {
void exec_gpu(uint8_t cmd,const uint32_t* p,int np){
    switch(cmd){
    case 0x10: g.mode=p[0]&3;return;                                   // MTX_MODE
    case 0x14:                                                            // MTX_RESTORE
        gpu_rec::command(cmd,p);g.clip_dirty=true;mesh_matrix_valid=false;
        if(g.mode==MTX_PROJ)g.proj=g.proj_stack[0];
        else{const int i=p[0]&31;g.pos=g.pos_stack[i];g.vec=g.vec_stack[i];}
        return;
    case 0x20: gpu_rec::set_unlit(p[0]&0x7fff);g.color=bgr555_to_argb(static_cast<uint16_t>(p[0]&0x7FFF));return;
    case 0x21: gpu_rec::normal(p[0]);return;
    case 0x22: gpu_rec::texcoord(p[0]);return;
    case 0x23: vertex(static_cast<int16_t>(p[0]&0xFFFF),static_cast<int16_t>(p[0]>>16),static_cast<int16_t>(p[1]&0xFFFF));return;
    case 0x29: g.polygon_attr=p[0];return;
    case 0x2A: gx_teximage_param(p[0]);return;
    case 0x2B: gx_pltt_base(p[0]);return;
    case 0x40:                                                            // BEGIN_VTXS
        gpu_rec::command(cmd,p);
        g.prim=p[0]&3;g.active_polygon_attr=g.polygon_attr;g.light_mask=g.active_polygon_attr&15;toon_latch();
        g.strip.clear();g_strip_ids.clear();mesh_strip.clear();g.strip_parity=0;
        return;
    case 0x41: gpu_rec::strip.clear();g.prim=-1;g.strip.clear();g_strip_ids.clear();mesh_strip.clear();return;
    default: exec(cmd,p,np);return;
    }
}
}
// Display-list DMA into the FIFO (runtime.cpp copy_words). In GPU mode the
// words are decoded in place: no per-word call, no parameter copy, and the hot
// vertex commands go straight to the recorder. Same command semantics as
// feed() (packed command bytes, zero-parameter commands execute at once); a
// command whose parameters cross the block end continues in feed().
void gx_write_fifo_block(const uint32_t* words,int n){
    if(!gpu_rec::active){for(int i=0;i<n;++i)gx_write_fifo(words[i]);return;}
    int i=0;
    while(i<n && g_queued)feed(words[i++]);
    while(i<n){
        const uint32_t packed=words[i++];
        for(int c=0;c<4;++c){
            const uint8_t cmd=uint8_t(packed>>(8*c));
            const int np=param_count(cmd);
            if(!np){if(cmd)exec(cmd,nullptr,0);continue;}
            if(i+np>n){
                // Hand the rest of this packed word to the state machine.
                for(int k=0;k<4;++k)g_queue[k]=uint8_t(packed>>(8*k));
                g_queued=4;g_qpos=c;g_have=0;
                while(i<n)feed(words[i++]);
                return;
            }
            const uint32_t* p=words+i;i+=np;
            exec_gpu(cmd,p,np);
            if(!gpu_rec::active){
                // Demoted mid-list: the rest goes through the CPU path.
                for(int k=c+1;k<4;++k){const uint8_t rest=uint8_t(packed>>(8*k));if(param_count(rest)){
                    for(int q=0;q<4;++q)g_queue[q]=uint8_t(packed>>(8*q));
                    g_queued=4;g_qpos=k;g_have=0;break;}else if(rest)exec(rest,nullptr,0);}
                while(i<n)gx_write_fifo(words[i++]);
                return;
            }
        }
    }
}
void gx_gpu_configure(bool allowed){
    // Default on wherever the scene policy allows it; SM64DS_GX_GPU=0 keeps
    // the CPU geometry path everywhere.
    static const bool option=[]{const char* s=std::getenv("SM64DS_GX_GPU");return !(s && std::atoi(s)==0);}();
    gpu_rec::option=option;gpu_rec::allowed=allowed;
}
bool gx_gpu_recording(){return gpu_rec::active;}
const gx_gpu::Frame* gx_gpu_publish(){return gpu_rec::publish();}
void gx_gpu_reconstruct(const gx_gpu::Frame& f,gx_geometry::Packet& out){
    out.clear();out.reserve(f.vertices.size(),f.triangles.size());
    for(std::size_t i=0;i<f.vertices.size();++i){const auto& v=f.vertices[i];
        // Same arithmetic as project(), with the entry's clip matrix and viewport.
        const auto& e=f.entries[v.entry];const std::int32_t* clip=e.clip;
        const std::int64_t x=v.x,y=v.y,z=v.z;
        auto component=[&](int j){
            const std::int64_t sum=x*clip[j]+y*clip[4+j]+z*clip[8+j]+4096LL*clip[12+j];
            return static_cast<std::int32_t>(sum>>12)*FX12;
        };
        const Vec4 c{component(0),component(1),component(2),component(3)};
        GxVertex o{};
        o.w=c.w;o.cx=c.x;o.cy=c.y;o.cz=c.z;o.cw=c.w;
        const float iw=(std::fabs(c.w)>1e-6f)?1.0f/c.w:0.0f;
        const int vp_x=e.viewport[0],vp_y=e.viewport[1],vp_w=e.viewport[2],vp_h=e.viewport[3];
        o.x=(c.x*iw+1.0f)*0.5f*vp_w+vp_x;
        o.y=(1.0f-(c.y*iw+1.0f)*0.5f)*vp_h+vp_y;
        o.z=(c.z*iw+1.0f)*0.5f;
        o.u=v.s/16.f;o.v=v.t/16.f;
        if(v.lit){const int n[3]={v.a,v.b,v.c};o.color=bgr555_to_argb(gx_gpu::light_color(f.envs[v.env],n));}
        else o.color=bgr555_to_argb(std::uint16_t(v.a|(v.b<<5)|(v.c<<10)));
        out.vertex(o);
    }
    for(const auto& t:f.triangles)out.triangle(t.id[0],t.id[1],t.id[2],f.materials[t.material]);
}
namespace {
// Deferred TEXIMAGE/PLTT bind (GPU mode). bind_from_vram verifies the encoded
// bytes of a texture once per geometry frame; with the physical VRAM bus the
// texture/palette view generations prove the bytes unchanged since a previous
// bind of the same parameters, so that bind's result is reused as is. The
// cache entry stays pinned exactly as bind_from_vram would pin it.
struct BindMemo {uint32_t teximage,pltt;uint64_t texture_generation,palette_generation,frame;CachedTexture* entry;int w,h;};
BindMemo bind_memo[16];unsigned bind_memo_next=0;
void gpu_lazy_bind(){
    auto* banks=io_vram_banks();
    const uint32_t fmt=(g_teximage>>26)&7;
    if(!banks || !fmt){bind_from_vram();return;}
    const uint64_t tg=banks->view_generation(VramView::Texture),pg=banks->view_generation(VramView::TexturePalette);
    for(auto& m:bind_memo){
        if(m.entry && m.teximage==g_teximage && m.pltt==g_plttbase && m.texture_generation==tg &&
           m.palette_generation==pg && m.frame+1>=g_texture_frame){
            m.entry->used_frame=g_texture_frame;m.entry->verified_frame=g_texture_frame;m.frame=g_texture_frame;
            gx_bind_texture(m.entry->pixels.data(),m.w,m.h);g.texture_version=m.entry->version;
            return;
        }
    }
    bind_from_vram();
    if(!g.tex_rgba)return;
    // Find the entry bind_from_vram just bound (same pixels and version).
    const uint64_t key=(static_cast<uint64_t>(g_plttbase)<<32)|g_teximage;
    const auto range=g_vram_tex_cache.equal_range(key);
    for(auto it=range.first;it!=range.second;++it)if(it->second.pixels.data()==g.tex_rgba){
        bind_memo[bind_memo_next++&15]={g_teximage,g_plttbase,tg,pg,g_texture_frame,&it->second,g.tw,g.th};break;}
}
// Exact demotion of the frame being recorded to the CPU direct path.
void gpu_demote_to_cpu(){
    if(!gpu_rec::active)return;
    if(gpu_rec::tex_dirty){gpu_rec::tex_dirty=false;gpu_lazy_bind();}
    gx_gpu_reconstruct(*gpu_rec::rec,g_packet);
    g.strip.clear();g_strip_ids.clear();
    // Same count (parity) as the CPU strip; only the last four are ever read.
    const auto& st=gpu_rec::strip;
    for(std::uint32_t k=0;k<st.n;++k){const std::uint32_t id=st[std::max(k,st.first())];g.strip.push_back(g_packet.vertices()[id]);g_strip_ids.push_back(id);}
    auto& a=gpu_rec::attr;
    if(a.lit){const int n[3]={a.a,a.b,a.c};g.color=bgr555_to_argb(gx_gpu::light_color(gpu_rec::rec->envs[a.env],n));}
    g.u=a.s/16.f;g.v=a.t/16.f;
    gpu_rec::active=false;gpu_rec::gpu_frame=false;gpu_rec::strip.clear();++gpu_rec::demoted;
    if(a.lit){const auto c=g.color;gpu_rec::set_unlit(((c>>19)&31)|(((c>>11)&31)<<5)|(((c>>3)&31)<<10));}
}
}
}  // namespace ntr
