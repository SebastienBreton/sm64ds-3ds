// GPU geometry mode, citro3d side (gpu-geometry-menu). Included by
// viewer_3ds.cpp before shader_bind(); see gx_gpu_viewer.h and gx_gpu.v.pica.
#include "gx_gpu_viewer.h"
#include "gx_gpu_shader.h"
#include <algorithm>
namespace gpugeo {
using ntr::gx_gpu::Vertex;
constexpr unsigned palette_slots=ntr::gx_gpu::palette_slots;
struct Run {
    int first=0,count=0;               // index range
    int env=-1;                        // lighting env of lit vertices, -1 none
    int lo=0;                          // palette window [lo, lo+16), slot = entry % 16
    std::int16_t viewport[4]={0,0,256,192};
};
static DVLB_s* dvlb=nullptr;
static shaderProgram_s program;
static C3D_AttrInfo attributes;
static int u_vport=-1,u_texdims=-1,u_base=-1,u_ldir=-1,u_ldif=-1,u_lspe=-1,u_pal=-1,b_light[4]={-1,-1,-1,-1};
static bool ready=false,failed=false;
// Frame drawn by gpu_frame: valid while the lease token matches.
static uint64_t token=0;
static const ntr::gx_gpu::Frame* frame=nullptr;
static const std::uint16_t* indices=nullptr;
static ntr::HostVector<Run> runs;
static ntr::HostVector<std::uint32_t> batch_runs;   // batch i -> runs [batch_runs[i], batch_runs[i+1])
static ntr::HostVector<float> depths;
// Uniform state already in the shader registers since the last bind.
static bool bound=false;
static int loaded_env=-1;
static bool highlight_pass=false;   // upload_env: highlight envs give the additive table colour
static std::int32_t loaded_entry[palette_slots];
static int loaded_tw=-1,loaded_th=-1;
static std::int16_t loaded_viewport[4]={-1,-1,-1,-1};
struct Stats {std::uint64_t frames=0,vertices=0,indices=0,runs=0,batches=0,palette_uploads=0,env_uploads=0,draws=0;double build_ms=0;};
static Stats stats;
static void* storage_allocate(std::size_t bytes){return linearAlloc(bytes);}
// Registration only (pointer + capacity); the game thread allocates the three
// 16384-vertex recording buffers (1.1 MiB linear) when GPU mode first starts.
static const bool storage_registered=(ntr::gx_gpu_storage(storage_allocate,16384),true);

static bool init(){
    if(ready)return true;
    if(failed)return false;
    failed=true;
    dvlb=DVLB_ParseFile(reinterpret_cast<u32*>(const_cast<unsigned char*>(gx_gpu_shader)),sizeof(gx_gpu_shader));
    if(!dvlb)return false;
    shaderProgramInit(&program);shaderProgramSetVsh(&program,&dvlb->DVLE[0]);
    auto* v=program.vertexShader;
    const int fix=shaderInstanceGetUniformLocation(v,"fix"),st=shaderInstanceGetUniformLocation(v,"stereo");
    const int snap=shaderInstanceGetUniformLocation(v,"ds_snap");
    u_texdims=shaderInstanceGetUniformLocation(v,"texdims");u_vport=shaderInstanceGetUniformLocation(v,"vport");u_base=shaderInstanceGetUniformLocation(v,"base");
    u_ldir=shaderInstanceGetUniformLocation(v,"ldir");u_ldif=shaderInstanceGetUniformLocation(v,"ldif");
    u_lspe=shaderInstanceGetUniformLocation(v,"lspe");u_pal=shaderInstanceGetUniformLocation(v,"pal");
    b_light[1]=shaderInstanceGetUniformLocation(v,"light1");b_light[2]=shaderInstanceGetUniformLocation(v,"light2");
    b_light[3]=shaderInstanceGetUniformLocation(v,"light3");
    // fix/stereo/ds_snap must alias the viewer shader's registers (set_fix).
    if(fix!=g_u_fix || st!=g_u_stereo || snap!=g_u_snap || u_pal<0 || u_pal+int(palette_slots*4)>C3D_FVUNIF_COUNT){
        std::printf("GX GPU shader layout mismatch fix=%d/%d stereo=%d/%d snap=%d/%d pal=%d\n",fix,g_u_fix,st,g_u_stereo,snap,g_u_snap,u_pal);
        return false;
    }
    AttrInfo_Init(&attributes);
    AttrInfo_AddLoader(&attributes,0,GPU_SHORT,4);
    AttrInfo_AddLoader(&attributes,1,GPU_SHORT,4);
    AttrInfo_AddLoader(&attributes,2,GPU_SHORT,4);
    failed=false;ready=true;
    std::printf("GX GPU geometry shader ready pal=%d slots=%u\n",u_pal,palette_slots);
    return true;
}

// Worker: index list and draw runs over the recorded vertices, which the PICA
// reads in place (no per-vertex work here). Indices live in the lease memory,
// so the acquire that drained the previous GPU frame also protects them.
static bool build(const ntr::gx_gpu::Frame& f,const V3dsDirectLease& lease,ntr::HostVector<V3dsBatch>& batches,
                  ntr::gx_geometry::Prepared& prepared,float& convergence,bool stereo_target){
    frame_profile::Scope profile_scope(40); // worker: GPU geometry runs
    const double start=v3ds_time_ms();
    prepared={};
    if(!init() || !lease.vertices)return false;
    if(std::getenv("PLAYER_GPU_SHADOWS"))return false; // shadow-plan passes rebind the CPU layout
    // Prepared counts indices; accepts() bounds it by the lease vertex capacity.
    if(f.triangles.size()*3>lease.vertex_capacity || f.vertices.size()>65535)return false;
    auto* out=reinterpret_cast<std::uint16_t*>(lease.vertices);
    runs.clear();batch_runs.clear();
    std::size_t nb=0,n=0;
    std::uint32_t current_material=UINT32_MAX;
    Run run;
    auto close_run=[&](){if(run.count)runs.push_back(run);run=Run{};run.first=int(n);run.lo=-1;};
    run.lo=-1;
    auto same_material=[&](const ntr::gx_geometry::Material& m,const V3dsBatch& b){
        return b.tex==m.tex && b.tw==m.tw && b.th==m.th && b.wrap==m.wrap && b.polygon_attr==m.polygon_attr && b.texture_version==m.texture_version;
    };
    const Vertex* vertices=f.vertices.data();
    for(const auto& t:f.triangles){
        if(t.material!=current_material){
            const auto& m=f.materials[t.material];
            if(!nb || !same_material(m,batches[nb-1])){
                close_run();
                if(nb)batches[nb-1].count=int(n)-batches[nb-1].first;
                if(batches.size()<=nb)batches.resize(nb+1);
                batches[nb]={m.tex,m.tw,m.th,m.wrap,int(n),0,false,m.polygon_attr,m.texture_version};
                batch_runs.push_back(std::uint32_t(runs.size()));
                ++nb;
            }
            current_material=t.material;
        }
        const Vertex& a=vertices[t.id[0]];const Vertex& b=vertices[t.id[1]];const Vertex& c=vertices[t.id[2]];
        const int emin=std::min<int>(a.entry,std::min<int>(b.entry,c.entry)),emax=std::max<int>(a.entry,std::max<int>(b.entry,c.entry));
        int env=a.lit?a.env:b.lit?b.env:c.lit?c.env:-1;
        if((a.lit && a.env!=env) || (b.lit && b.env!=env) || (c.lit && c.env!=env))return false; // one DS lighting state per polygon
        const std::int16_t* vp=f.entries[emax].viewport;
        if(run.lo<0 || emin<run.lo || emax>=run.lo+int(palette_slots) || (env>=0 && run.env>=0 && run.env!=env) ||
           std::memcmp(run.viewport,vp,8)){
            if(run.count || run.lo>=0){close_run();}
            run.lo=emin;std::memcpy(run.viewport,vp,8);
        }
        if(env>=0)run.env=env;
        out[n]=std::uint16_t(t.id[0]);out[n+1]=std::uint16_t(t.id[1]);out[n+2]=std::uint16_t(t.id[2]);n+=3;
        run.count+=3;
    }
    close_run();
    if(nb)batches[nb-1].count=int(n)-batches[nb-1].first;
    batch_runs.push_back(std::uint32_t(runs.size()));
    GSPGPU_FlushDataCache(out,n*sizeof(std::uint16_t));
    GSPGPU_FlushDataCache(vertices,f.vertices.size()*sizeof(Vertex));
    // Stereo convergence: median W of on-screen polygon corners, as the CPU
    // Builder computed it. Only needed when an eye separation is applied.
    float slider=osGet3DSliderState();
    if(const char* forced=std::getenv("SM64DS_FORCE_3D"))slider=float(std::atof(forced));
    if(slider>0.f && stereo_target){ // menu-stereo: lower-LCD 3D is never stereo
        depths.clear();
        for(const auto& t:f.triangles)for(unsigned k=0;k<3;++k){
            const auto& p=vertices[t.id[k]];const auto& en=f.entries[p.entry];const auto* c=en.clip;const auto* vp=en.viewport;
            auto col=[&](int j){return (float(p.x)*c[j]+float(p.y)*c[4+j]+float(p.z)*c[8+j])*(1.f/(4096.f*4096.f))+c[12+j]*(1.f/4096.f);};
            const float w=col(3);if(!(w>1e-5f))continue;
            const float sx=(col(0)/w+1.f)*.5f*vp[2]+vp[0],sy=(1.f-(col(1)/w+1.f)*.5f)*vp[3]+vp[1];
            if(sx>=0 && sx<256 && sy>=0 && sy<192)depths.push_back(w);
        }
        if(!depths.empty()){auto* mid=depths.data()+depths.size()/2;std::nth_element(depths.data(),mid,depths.data()+depths.size());convergence=*mid;}
        else convergence=1.f;
    }
    frame=&f;indices=out;token=lease.frame_token;
    prepared.vertices=lease.vertices;prepared.vertex_count=n;prepared.batches=batches.data();prepared.batch_count=nb;
    prepared.frame_token=lease.frame_token;prepared.geometry_generation=f.generation;
    prepared.coordinates=ntr::gx_geometry::CoordinateConvention::NativePixelCentered;prepared.valid=true;
    ++stats.frames;stats.vertices+=f.vertices.size();stats.indices+=n;stats.runs+=runs.size();stats.batches+=nb;stats.build_ms+=v3ds_time_ms()-start;
    return true;
}

static bool active_for(uint64_t direct_token){return ready && direct_token && direct_token==token && frame;}
static void bind(){
    C3D_BindProgram(&program);C3D_SetAttrInfo(&attributes);
    auto* b=C3D_GetBufInfo();BufInfo_Init(b);BufInfo_Add(b,frame->vertices.data(),sizeof(Vertex),3,0x210);
    bound=true;loaded_env=-2;loaded_tw=loaded_th=-1;for(auto& x:loaded_viewport)x=-1;
    for(auto& e:loaded_entry)e=-1;
}
static void upload_env(int id){
    C3D_FVec* base=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,u_base,1);
    C3D_FVec* ldir=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,u_ldir,4);
    C3D_FVec* ldif=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,u_ldif,4);
    C3D_FVec* lspe=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,u_lspe,4);
    for(unsigned i=0;i<4;++i){ldir[i]=FVec4_New(0,0,0,0);ldif[i]=ldir[i];lspe[i]=ldir[i];}
    unsigned count=0;int sum[3]={0,0,0};
    if(id>=0){
        const auto& e=frame->envs[id];
        for(unsigned k=0;k<3;++k)sum[k]=e.emi[k]*32;
        for(unsigned i=0;i<4;++i){
            if(!((e.mask>>i)&1))continue;
            const auto& L=e.light[i];const int lc[3]={L.r,L.g,L.b};
            for(unsigned k=0;k<3;++k)sum[k]+=e.amb[k]*lc[k];
            ldir[count]=FVec4_New(L.dx,L.dy,L.dz,float(L.recip));
            // dif*light/(32*512): d is in 1/512 units, colours in 5-bit units.
            ldif[count]=FVec4_New(e.dif[0]*lc[0]*(1.f/16384.f),e.dif[1]*lc[1]*(1.f/16384.f),e.dif[2]*lc[2]*(1.f/16384.f),0);
            lspe[count]=FVec4_New(e.spe[0]*lc[0]*(1.f/16384.f),e.spe[1]*lc[1]*(1.f/16384.f),e.spe[2]*lc[2]*(1.f/16384.f),0);
            ++count;
        }
    }
    *base=FVec4_New(sum[0]*(1.f/32.f),sum[1]*(1.f/32.f),sum[2]*(1.f/32.f),0);
    if(id>=0 && frame->envs[id].toon){
        // DS toon shading (face-toy): colour = ToonTable[lit red]. The shader's
        // per-channel lighting is linear up to the final floor/clamp, so feeding
        // every channel the red terms gives red everywhere; the table itself is
        // applied as a per-channel fit max(0, a*red+b) (exact for the grey
        // identity ramp the title heads use; other tables are approximated).
        // Highlight envs (toon==2): the base pass is the grey red colour; the
        // additive pass (highlight_pass) is the fitted table colour.
        const auto& e=frame->envs[id];float a[3]={1.f,1.f,1.f},b[3]={0.f,0.f,0.f};
        if(e.toon==1 || highlight_pass)for(unsigned k=0;k<3;++k){
            double v[32];bool identity=true;
            for(unsigned i=0;i<32;++i){v[i]=(e.table[i]>>(5*k))&31;identity&=v[i]==i;}
            if(identity)continue;
            // Hinge fit: least squares on [s,31] for every start s, error of
            // max(0, line) over the whole table; keep the best.
            double best=1e30;
            for(unsigned s=0;s<31;++s){
                double si=0,st=0,sii=0,sit=0;const double n=32-s;
                for(unsigned i=s;i<32;++i){si+=i;st+=v[i];sii+=double(i)*i;sit+=i*v[i];}
                const double den=n*sii-si*si;if(den==0)continue;
                const double slope=(n*sit-si*st)/den,bias=(st-slope*si)/n;
                double err=0;for(unsigned i=0;i<32;++i){double f=slope*i+bias;if(f<0)f=0;err+=(f-v[i])*(f-v[i]);}
                if(err<best){best=err;a[k]=float(slope);b[k]=float(bias+.5-.5*slope);} // floor(a*x+b) ~ round(fit(floor x))
            }
        }
        auto red=[&](C3D_FVec& v,bool add_bias){const float r=v.x;v=FVec4_New(a[0]*r+(add_bias?b[0]:0.f),a[1]*r+(add_bias?b[1]:0.f),a[2]*r+(add_bias?b[2]:0.f),v.w);};
        red(*base,true);
        for(unsigned i=0;i<count;++i){red(ldif[i],false);red(lspe[i],false);}
    }
    for(unsigned i=1;i<4;++i)C3D_BoolUnifSet(GPU_VERTEX_SHADER,b_light[i],count>i);
    ++stats.env_uploads;
}
// True when a run of batch i uses a highlight env (DS polygon mode 2 with
// DISP3DCNT bit 1): the viewer then draws draw_batch(b,i,true) additively.
static bool highlight_batch(int i){
    if(!frame)return false;
    for(std::uint32_t r=batch_runs.at(i);r<batch_runs.at(i+1);++r){const int e=runs[r].env;if(e>=0 && frame->envs[e].toon==2)return true;}
    return false;
}
static void draw_batch(const V3dsBatch& b,int i,bool highlight=false){
    if(!bound)bind();
    if(highlight!=highlight_pass){highlight_pass=highlight;loaded_env=-2;}
    if(loaded_tw!=b.tw || loaded_th!=b.th){
        C3D_FVUnifSet(GPU_VERTEX_SHADER,u_texdims,b.tw?1.f/(16.f*b.tw):0.f,b.th?1.f/(16.f*b.th):0.f,0.f,0.f);
        loaded_tw=b.tw;loaded_th=b.th;
    }
    for(std::uint32_t r=batch_runs.at(i);r<batch_runs.at(i+1);++r){
        const auto& run=runs[r];
        if(std::memcmp(loaded_viewport,run.viewport,8)){
            C3D_FVUnifSet(GPU_VERTEX_SHADER,u_vport,run.viewport[2]*.5f,run.viewport[0],run.viewport[3]*.5f,run.viewport[1]);
            std::memcpy(loaded_viewport,run.viewport,8);
        }
        if(highlight && (run.env<0 || frame->envs[run.env].toon!=2))continue; // additive pass: highlight runs only
        if(run.env>=0 && run.env!=loaded_env){upload_env(run.env);loaded_env=run.env;}
        else if(loaded_env==-2){upload_env(-1);loaded_env=-1;}
        for(unsigned s=0;s<palette_slots;++s){
            const int id=run.lo+int((s-unsigned(run.lo))&(palette_slots-1));
            if(id>=int(frame->entries.size()) || loaded_entry[s]==id)continue;
            const auto* c=frame->entries[id].clip;
            C3D_FVec* p=C3D_FVUnifWritePtr(GPU_VERTEX_SHADER,u_pal+int(s*4),4);
            // Column j of the row-vector clip matrix: clip_j = dph(vertex, column_j).
            // DS: clip = (x*M0j + y*M1j + z*M2j + 4096*M3j) >> 12, in fixed12, and
            // x, y, z are 4.12 vertex units: x..z scale 1/4096^2, translation 1/4096.
            constexpr float s2=1.f/(4096.f*4096.f),s1=1.f/4096.f;
            for(unsigned j=0;j<4;++j)p[j]=FVec4_New(c[j]*s2,c[4+j]*s2,c[8+j]*s2,c[12+j]*s1);
            loaded_entry[s]=id;++stats.palette_uploads;
        }
        C3D_DrawElements(GPU_TRIANGLES,run.count,C3D_UNSIGNED_SHORT,indices+run.first);++stats.draws;
    }
}
}
namespace gx_gpu_viewer {
bool build(const ntr::gx_gpu::Frame& f,const V3dsDirectLease& lease,ntr::HostVector<V3dsBatch>& batches,
           ntr::gx_geometry::Prepared& prepared,float& convergence,bool stereo_target){
    return gpugeo::build(f,lease,batches,prepared,convergence,stereo_target);
}
void print_stats(){
    const auto& s=gpugeo::stats;if(!s.frames)return;
    const double f=double(s.frames);
    std::printf("GX GPU geometry frames=%llu vertices=%.1f indices=%.1f batches=%.1f runs=%.1f draws=%.1f paletteUploads=%.1f envUploads=%.1f buildMs=%.3f\n",
        (unsigned long long)s.frames,s.vertices/f,s.indices/f,s.batches/f,s.runs/f,s.draws/f,s.palette_uploads/f,s.env_uploads/f,s.build_ms/f);
}
}
