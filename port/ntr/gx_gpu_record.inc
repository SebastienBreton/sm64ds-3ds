// GPU geometry mode recorder (gpu-geometry-menu). Included by gx.cpp inside its
// anonymous namespace, after the matrix/lighting helpers. See gx_gpu_geometry.h.
//
// What stays on the CPU: command decode, every matrix stack operation, the clip
// matrix of each distinct matrix state (one fixed12 product per entry, cached
// per stack slot so skinned meshes that MTX_RESTORE a bone per vertex reuse it),
// texture binds, texture coordinates (DS 12.4 integer results, exact) and the
// normal x vector-matrix product (exact signed .9, latched by NORMAL).
// What moves to the PICA200: the per-vertex clip transform, 1/w, the DS
// viewport transform/snap and the per-light DS lighting/colour accumulation.
// The recording path performs no floating-point conversion (VFP conversions
// are expensive on the ARM11 and in Azahar guest time).
void bind_from_vram();
void gpu_demote_to_cpu();
void gpu_lazy_bind();
void exec_gpu(std::uint8_t cmd,const std::uint32_t* p,int np);
namespace gpu_rec {
using ntr::gx_gpu::Vertex;using ntr::gx_gpu::Frame;using ntr::gx_gpu::Env;
// Three frames rotate: recorded, published (presentation worker), and the one
// the GPU may still read (the worker's next acquire waits for it).
Frame frames[3];
Frame* rec=&frames[0];          // being recorded by the game thread
Frame* pub=&frames[1];          // last published to the presentation side
Frame* flight=&frames[2];       // previously published, possibly still read by the GPU
void* (*storage_allocate)(std::size_t)=nullptr;
std::size_t storage_capacity=0;
bool storage_ready=false;
bool allowed=false;             // scene policy (gx_gpu_configure)
bool option=false;              // !(SM64DS_GX_GPU=0)
bool active=false;              // this frame records for the GPU
bool fresh=false;               // rec holds a frame not yet published
bool gpu_frame=false;           // rec is (being) recorded in GPU mode
bool published_gpu=false;       // pub is a GPU frame
bool tex_dirty=false;           // TEXIMAGE/PLTT written, bind before the next polygon
int entry=-1;                   // entry of the current pos x proj + viewport, -1 = recompute
int source_slot=-1;             // current pos/vec equal pos_stack[slot] since restore/store
int slot_entry[32];
int env=-1;                     // env of the current lighting state, -1 = rebuild
// Current per-vertex attributes (s, t, lit, env, a, b, c, pad): DS state.
struct Attr {std::int16_t s,t,lit,env,a,b,c,pad;} attr{0,0,0,0,31,31,31,0};
int material=-1;
// Open primitive: the last four vertex ids (ring, strip[k] == ring[k&3]) and
// the number of vertices since BEGIN (strip parity), as push_vertex sees them.
struct Strip {
    std::uint32_t ring[4];std::uint32_t n=0;
    void clear(){n=0;}
    bool empty()const{return !n;}
    std::uint32_t operator[](std::uint32_t k)const{return ring[k&3];}
    std::uint32_t first()const{return n>4?n-4:0;}
} strip;
bool material_check=true;       // BEGIN latched attributes or a texture was bound
bool vec_check=true;            // vector matrix may have changed
bool vec_small=false;           // |vec 3x3| < 2^20: 32-bit normal transform is exact
std::uint64_t frames_recorded=0,fallback_light=0,demoted=0;

Frame* last(){return fresh?rec:pub;}
void invalidate_slots(){for(auto& e:slot_entry)e=-1;}
void reset(){
    active=fresh=published_gpu=gpu_frame=tex_dirty=false;entry=-1;source_slot=-1;env=-1;material=-1;material_check=true;
    attr=Attr{0,0,0,0,31,31,31,0};strip.clear();invalidate_slots();
    for(auto& f:frames)f.clear();
}
bool ensure_storage(){
    if(storage_ready)return true;
    if(!storage_allocate || !storage_capacity)return false;
    for(auto& f:frames){
        if(!f.vertices.data_){f.vertices.data_=static_cast<Vertex*>(storage_allocate(storage_capacity*sizeof(Vertex)));
            if(!f.vertices.data_)return false;f.vertices.capacity_=storage_capacity;}
    }
    return storage_ready=true;
}
void set_unlit(std::uint32_t bgr15){attr.a=bgr15&31;attr.b=(bgr15>>5)&31;attr.c=(bgr15>>10)&31;attr.lit=0;attr.env=0;}

// Exact integer twin of generated_texcoord (DS 12.4 result, no float).
void texcoord_fixed(int x,int y,int z,int w,unsigned shift,bool add_raw){
    if(g.texture_dirty){for(unsigned i=0;i<16;++i)g.texture_fixed[i]=g.tex.raw[i];g.texture_dirty=false;}
    for(unsigned j=0;j<2;++j){
        const std::int64_t sum=std::int64_t(x)*g.texture_fixed[j]+std::int64_t(y)*g.texture_fixed[4+j]
            +std::int64_t(z)*g.texture_fixed[8+j]+std::int64_t(w)*g.texture_fixed[12+j];
        const std::int16_t fixed=static_cast<std::int16_t>((sum>>shift)+(add_raw?g.raw_texcoord[j]:0));
        (j?attr.t:attr.s)=fixed;
    }
}

// Pre-execution hook for every decoded command while recording.
inline void command(std::uint8_t cmd,const std::uint32_t* p){
    switch(cmd){
    case 0x11: // MTX_PUSH: the pushed slot equals the current matrices.
        if(g.mode!=MTX_PROJ && g.pos_sp<31)slot_entry[g.pos_sp]=entry;
        break;
    case 0x12: entry=-1;source_slot=-1;vec_check=true;if(g.mode==MTX_PROJ)invalidate_slots();break;
    case 0x13: if(g.mode!=MTX_PROJ){const int i=p[0]&31;slot_entry[i]=entry;source_slot=i;}break;
    case 0x14:
        if(g.mode==MTX_PROJ){entry=-1;invalidate_slots();}
        else{const int i=p[0]&31;entry=slot_entry[i];source_slot=i;vec_check=true;}
        break;
    case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C:
        if(g.mode==MTX_PROJ){entry=-1;invalidate_slots();}
        else if(g.mode!=MTX_TEX){entry=-1;source_slot=-1;vec_check=true;}
        break;
    case 0x20: set_unlit(p[0]&0x7fff);break;
    case 0x30: env=-1;if((p[0]>>15)&1)set_unlit(p[0]&0x7fff);break;
    case 0x31: case 0x32: case 0x33: env=-1;break;
    case 0x40:
        // BEGIN latches the light mask; keep the env when the mask is unchanged.
        if(env>=0 && rec->envs[env].mask!=(g.polygon_attr&15))env=-1;
        strip.clear();material_check=true;if(tex_dirty){tex_dirty=false;gpu_lazy_bind();}
        break;
    case 0x41: strip.clear();break;
    case 0x60: entry=-1;invalidate_slots();break; // viewport is part of the entry
    default: break;
    }
}

// TEXCOORD replacement (DS 12.4 integers; the float g.u/g.v are not needed).
inline void texcoord(std::uint32_t packed){
    g.raw_texcoord[0]=static_cast<std::int16_t>(packed&0xffff);
    g.raw_texcoord[1]=static_cast<std::int16_t>(packed>>16);
    if(g.texmode==1)texcoord_fixed(g.raw_texcoord[0],g.raw_texcoord[1],1,1,12,false);
    else{attr.s=g.raw_texcoord[0];attr.t=g.raw_texcoord[1];}
}

int current_entry(){
    if(entry>=0)return entry;
    if(g.clip_dirty){clip_sparse::multiply(g.pos.raw,g.proj.raw,g.clip);g.clip_dirty=false;}
    ntr::gx_gpu::Entry e;std::memcpy(e.clip,g.clip,sizeof e.clip);
    e.viewport[0]=std::int16_t(g.vp_x);e.viewport[1]=std::int16_t(g.vp_y);e.viewport[2]=std::int16_t(g.vp_w);e.viewport[3]=std::int16_t(g.vp_h);
    entry=int(rec->entries.size());rec->entries.push_back(e);
    if(source_slot>=0)slot_entry[source_slot]=entry;
    return entry;
}
int current_env(){
    if(env>=0)return env;
    Env e{};
    for(int k=0;k<3;++k){e.dif[k]=g.diffuse[k];e.amb[k]=g.ambient[k];e.spe[k]=g.specular[k];e.emi[k]=g.emission[k];}
    e.mask=g.light_mask&15;
    for(int i=0;i<4;++i){const auto& L=g.lights[i];
        auto& o=e.light[i];o.recip=L.spec_recip;o.dx=std::int16_t(L.dx);o.dy=std::int16_t(L.dy);o.dz=std::int16_t(L.dz);
        o.r=std::uint8_t(L.r);o.g=std::uint8_t(L.g);o.b=std::uint8_t(L.b);}
    // Identical lighting states share one id.
    const int n=int(rec->envs.size());
    for(int i=n-1;i>=0 && i>=n-8;--i)if(!std::memcmp(&rec->envs[i],&e,sizeof e)){env=i;return env;}
    env=n;rec->envs.push_back(e);
    return env;
}

// Toon variant of a lighting env (face-toy): same lighting, colour = table[red].
int toon_env(int id,std::uint8_t mode=1){ // mode 1 toon, 2 highlight
    static int from=-1,to=-1;static std::uint64_t owner=~0ull;static std::uint32_t version=0;static std::uint8_t last_mode=0;
    // g_toon_version changes whenever g_toon_table does (gx.cpp toon_latch), so
    // the per-vertex table memcmp is not needed (menu-regression).
    if(owner==rec->generation && from==id && version==g_toon_version && last_mode==mode && to<int(rec->envs.size()))return to;
    Env e=rec->envs[id];e.toon=mode;std::memcpy(e.table,g_toon_table,sizeof e.table);
    const int n=int(rec->envs.size());int found=-1;
    for(int i=n-1;i>=0 && i>=n-8;--i)if(!std::memcmp(&rec->envs[i],&e,sizeof e)){found=i;break;}
    if(found<0){found=n;rec->envs.push_back(e);}
    owner=rec->generation;from=id;to=found;version=g_toon_version;last_mode=mode;return found;
}
// NORMAL replacement. Texture coordinate mode 2 and the vector-matrix product
// keep their exact integer CPU evaluation; the colour moves to the shader.
inline void normal(std::uint32_t packed){
    // Signed 10-bit fields (same values as the CPU unpack loop).
    const int raw[3]={int(packed<<22)>>22,int(packed<<12)>>22,int(packed<<2)>>22};
    if(g.texmode==2)texcoord_fixed(raw[0],raw[1],raw[2],0,21,true);
    int n[3];
    const std::int32_t* v=g.vec.raw;
    // |raw| <= 512 and |v| < 2^20: the three-term sum fits 32 bits, so the
    // 64-bit DS accumulator gives the same value (lighting_vector).
    if(vec_check){
        auto small=[](std::int32_t x){return std::uint32_t(x+(1<<20))<(2u<<20);};
        vec_small=small(v[0])&&small(v[1])&&small(v[2])&&small(v[4])&&small(v[5])&&small(v[6])&&small(v[8])&&small(v[9])&&small(v[10]);
        vec_check=false;
    }
    if(vec_small){
        for(int j=0;j<3;++j){const int value=(raw[0]*v[j]+raw[1]*v[4+j]+raw[2]*v[8+j])>>12;
            const unsigned wrapped=unsigned(value)&2047u;n[j]=wrapped>=1024?int(wrapped)-2048:int(wrapped);}
    }else lighting_vector(packed,n,false);
    const int id=current_env();
    if(g.use_shininess){
        bool shiny=false;
        for(int i=0;i<4;++i)if(((g.light_mask>>i)&1) &&
            (g.specular[0]*g.lights[i].r|g.specular[1]*g.lights[i].g|g.specular[2]*g.lights[i].b))shiny=true;
        if(shiny){
            // The 128-entry shininess table is not a shader input: exact CPU colour.
            ++fallback_light;
            set_unlit(ntr::gx_gpu::light_color(rec->envs[id],n,g.shininess,true));
            return;
        }
    }
    attr.a=std::int16_t(n[0]);attr.b=std::int16_t(n[1]);attr.c=std::int16_t(n[2]);attr.lit=1;attr.env=std::int16_t(id);
}

// Keep every triangle within one 16-entry palette window: a vertex whose
// entry is 16+ ids older than the newest one is re-recorded with a copy of
// its entry (rare: strips spanning many matrix states).
std::uint32_t window_copy(std::uint32_t id,int newest){
    static int from=-1,to=-1;static std::uint64_t owner=~0ull;
    Vertex v=rec->vertices[id];
    if(owner!=rec->generation || from!=v.entry || newest-to>=16 || to>=int(rec->entries.size())){
        owner=rec->generation;from=v.entry;to=int(rec->entries.size());rec->entries.push_back(rec->entries[v.entry]);
    }
    v.entry=std::int16_t(to);v.slot=std::int16_t((to&15)*4);
    const auto copy=std::uint32_t(rec->vertices.size_++);rec->vertices.data_[copy]=v;return copy;
}
void emit(std::uint32_t a,std::uint32_t b,std::uint32_t c){
    {const int ea=rec->vertices[a].entry,eb=rec->vertices[b].entry,ec=rec->vertices[c].entry;
     const int newest=std::max(ea,std::max(eb,ec));
     if(newest-std::min(ea,std::min(eb,ec))>=16){
         if(newest-ea>=16)a=window_copy(a,newest);
         if(newest-eb>=16)b=window_copy(b,newest);
         if(newest-ec>=16)c=window_copy(c,newest);
     }}
    const auto& m=g;
    if(material_check){
        material_check=false;
        if(material<0 || [&]{const auto& cur=rec->materials[material];
            return cur.tex!=m.tex_rgba || cur.tw!=m.tw || cur.th!=m.th || cur.wrap!=m.wrap ||
                   cur.polygon_attr!=m.active_polygon_attr || cur.texture_version!=m.texture_version;}()){
            material=int(rec->materials.size());
            rec->materials.push_back({m.tex_rgba,m.tw,m.th,m.wrap,m.active_polygon_attr,m.texture_version});
        }
    }
    rec->triangles.push_back({{a,b,c},std::uint32_t(material)});
}
// Same primitive assembly (and winding) as push_vertex.
inline void assemble(std::uint32_t id){
    const std::uint32_t n=++strip.n;strip.ring[(n-1)&3]=id;
    switch(g.prim){
    case 0:if(n==3){emit(strip[0],strip[1],strip[2]);strip.clear();}break;
    case 1:if(n==4){emit(strip[0],strip[1],strip[2]);emit(strip[0],strip[2],strip[3]);strip.clear();}break;
    case 2:if(n>=3){if((n-3)&1)emit(strip[n-2],strip[n-3],strip[n-1]);else emit(strip[n-3],strip[n-2],strip[n-1]);}break;
    case 3:if(n>=4&&!(n%2)){emit(strip[n-4],strip[n-3],strip[n-1]);emit(strip[n-4],strip[n-1],strip[n-2]);}break;
    default:break;
    }
}

inline bool vertex(std::int16_t x,std::int16_t y,std::int16_t z){
    auto& store=rec->vertices;
    // Room for this vertex and two span-split duplicates, else exact demotion.
    if(store.size_+3>store.capacity_){gpu_demote_to_cpu();return false;}
    if(tex_dirty){tex_dirty=false;gpu_lazy_bind();material_check=true;}
    const int e=entry>=0?entry:current_entry();
    const auto id=std::uint32_t(store.size_++);
    Vertex& out=store.data_[id];
    out.x=x;out.y=y;out.z=z;out.slot=std::int16_t((e&15)*4);
    std::memcpy(&out.s,&attr,sizeof attr);
    out.entry=std::int16_t(e);
    if(g_toon){ // DS toon shading: vertex colour -> ToonTable[red]
        if(out.lit)out.env=std::int16_t(toon_env(out.env));
        else{const std::uint16_t c=toon_bgr555(std::uint16_t(out.a));out.a=std::int16_t(c&31);out.b=std::int16_t((c>>5)&31);out.c=std::int16_t((c>>10)&31);}
    }else if(g_highlight){ // DS highlight: grey (red) base; lit vertices get the additive table pass
        if(out.lit)out.env=std::int16_t(toon_env(out.env,2));
        else{out.b=out.a;out.c=out.a;}
    }
    if(g.texmode==3){const Attr saved=attr;texcoord_fixed(x,y,z,0,24,true);out.s=attr.s;out.t=attr.t;attr=saved;}
    assemble(id);
    return true;
}

// gx_begin_frame: decide the mode of the next frame. A primitive left open
// across the boundary is re-emitted into the new frame (DS state survives).
void begin_frame(bool direct,bool mesh,bool cpu_strip_open){
    if(tex_dirty){tex_dirty=false;bind_from_vram();}
    const bool was=active;
    const bool next=allowed && option && direct && !mesh && !(cpu_strip_open && !was) && ensure_storage();
    Frame* previous=last();
    static HostVector<Vertex> carry;static HostVector<ntr::gx_gpu::Entry> carry_entries;static HostVector<Env> carry_envs;
    carry.clear();carry_entries.clear();carry_envs.clear();
    if(was && next){
        for(std::uint32_t k=strip.first();k<strip.n;++k){const std::uint32_t id=strip[k];Vertex v=previous->vertices[id];
            carry_entries.push_back(previous->entries[v.entry]);v.entry=std::int16_t(carry_entries.size()-1);v.slot=std::int16_t((v.entry&15)*4);
            if(v.lit){carry_envs.push_back(previous->envs[v.env]);v.env=std::int16_t(carry_envs.size()-1);}
            carry.push_back(v);}
        if(attr.lit){carry_envs.push_back(previous->envs[attr.env]);attr.env=std::int16_t(carry_envs.size()-1);}
    }else if(was && attr.lit){
        // Lit colour state leaving GPU mode: keep the colour, not the normal.
        const int n[3]={attr.a,attr.b,attr.c};const auto c=ntr::gx_gpu::light_color(previous->envs[attr.env],n);
        set_unlit(c);g.color=bgr555_to_argb(c);
    }
    if(was && !next){g.u=attr.s/16.f;g.v=attr.t/16.f;}
    if(!was && next){
        // Entering GPU mode: adopt the CPU path's DS colour/texcoord state.
        const std::uint32_t c=g.color;
        set_unlit(((c>>19)&31)|(((c>>11)&31)<<5)|(((c>>3)&31)<<10));
        attr.s=std::int16_t(g.u*16.f);attr.t=std::int16_t(g.v*16.f);
    }
    const Strip open=strip;
    strip.clear();
    gpu_frame=next;active=next;
    rec->clear();entry=-1;source_slot=-1;env=-1;material=-1;material_check=true;invalidate_slots();
    fresh=true;
    if(active){
        ++frames_recorded;
        for(auto& e:carry_entries)rec->entries.push_back(e);
        for(auto& e:carry_envs)rec->envs.push_back(e);
        // Re-emit the open primitive's last vertices with its parity, no triangles.
        std::uint32_t k=open.first();
        for(auto& v:carry){const auto id=std::uint32_t(rec->vertices.size_++);rec->vertices.data_[id]=v;strip.ring[k&3]=id;++k;}
        strip.n=open.n;
    }
}
// Presentation: publish the last recorded frame (swap only when new).
const Frame* publish(){
    if(fresh){Frame* free_frame=flight;flight=pub;pub=rec;rec=free_frame;fresh=false;published_gpu=gpu_frame;}
    return published_gpu?pub:nullptr;
}
}
