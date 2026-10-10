#include "native_obj_gpu.h"
#include <cstring>
#include <algorithm>

namespace ds_gpu {
namespace {
unsigned tiled_offset(unsigned x,unsigned y,unsigned width){
    const unsigned xx=x&7,yy=y&7;
    const unsigned morton=(xx&1)|((yy&1)<<1)|((xx&2)<<1)|((yy&2)<<2)|((xx&4)<<2)|((yy&4)<<3);
    return ((y/8)*(width/8)+x/8)*64+morton;
}
}
bool NativeObjects::init(const uint32_t* shader,unsigned bytes){
    dvlb_=DVLB_ParseFile(const_cast<uint32_t*>(shader),bytes);if(!dvlb_)return false;
    shaderProgramInit(&program_);shaderProgramSetVsh(&program_,&dvlb_->DVLE[0]);
    AttrInfo_Init(&attributes_);
    AttrInfo_AddLoader(&attributes_,0,GPU_FLOAT,4);
    AttrInfo_AddLoader(&attributes_,1,GPU_FLOAT,2);
    AttrInfo_AddLoader(&attributes_,2,GPU_FLOAT,2);
    vertices_=static_cast<Vertex*>(linearAlloc(128*12*sizeof(Vertex)));
    return vertices_!=nullptr;
}
void NativeObjects::destroy(){
    textures_.clear();texture_bytes_=0;
    if(vertices_)linearFree(vertices_);vertices_=nullptr;
    if(dvlb_){shaderProgramFree(&program_);DVLB_Free(dvlb_);}dvlb_=nullptr;
}
NativeObjects::Texture* NativeObjects::texture(const ObjSnapshot& s){
    for(auto& t:textures_)if(t->width==s.width && t->height==s.height &&
        t->encoded.size()==s.encoded.size() && t->palette.size()==s.palette_bytes &&
        !std::memcmp(t->encoded.data(),s.encoded.data(),s.encoded.size()) &&
        !std::memcmp(t->palette.data(),s.palette.data(),s.palette_bytes)){
        t->used=frame_;return t.get();
    }
    //Only textures not referenced in this frame may be evicted. The caller
    //waited for the previous GPU queue before entering this frame.
    const size_t bytes=s.width*s.height*4;
    for(auto it=textures_.begin();texture_bytes_+bytes>2*1024*1024 && it!=textures_.end();){
        if((*it)->used!=frame_){texture_bytes_-=(*it)->gpu.size;it=textures_.erase(it);}
        else ++it;
    }
    auto t=std::make_unique<Texture>();
    if(!C3D_TexInit(&t->gpu,s.width,s.height,GPU_RGBA8))return nullptr;
    C3D_TexSetFilter(&t->gpu,GPU_NEAREST,GPU_NEAREST);
    // Transparent border: rotated affine OBJs sample outside their source.
    // Ordinary and scaled sprites never reach it (pixel-centre coordinates).
    C3D_TexSetWrap(&t->gpu,GPU_CLAMP_TO_BORDER,GPU_CLAMP_TO_BORDER);t->gpu.border=0;
    ntr::HostVector<uint32_t> pixels;decode_obj_texture(s,pixels);
    auto* gpu=static_cast<uint32_t*>(t->gpu.data);
    for(unsigned y=0;y<s.height;++y)for(unsigned x=0;x<s.width;++x){
        const auto c=pixels[y*s.width+x];
        gpu[tiled_offset(x,s.height-1-y,s.width)]=(c<<8)|(c>>24);
    }
    GSPGPU_FlushDataCache(t->gpu.data,t->gpu.size);
    t->width=s.width;t->height=s.height;t->used=frame_;
    t->encoded=s.encoded;t->palette.assign(s.palette.begin(),s.palette.begin()+s.palette_bytes);
    texture_bytes_+=t->gpu.size;++decoded_;
    auto* result=t.get();textures_.push_back(std::move(t));return result;
}
bool NativeObjects::draw(Surface& output,const ObjPlan& plan){
    draw_stats_={};
    if(!dvlb_ || !vertices_ || plan.fallback!=ObjFallback::None)return false;
    ++frame_;std::array<Texture*,128> ready{};
    std::array<unsigned,128> first{},counts{};unsigned vertex_count=0;
    //Complete allocation/upload before issuing any draws, so a failed
    //preflight cannot leave a partly rendered result for its caller.
    for(unsigned i=0;i<plan.count;++i){
        const auto& s=plan.sprites[plan.order[i]];
        if(!(ready[i]=texture(s)))return false;
        first[i]=vertex_count;
        const unsigned parts=s.affine && s.clip_y_second0<s.clip_y_second1?2:1;
        for(unsigned part=0;part<parts;++part){
            const unsigned by0=s.affine?(part?s.clip_y_second0:s.clip_y0):0;
            const unsigned by1=s.affine?(part?s.clip_y_second1:s.clip_y1):s.height;
            if(by0>=by1)continue;
            const int shift=part?-256:0;
            const float x0=(s.x+int(s.affine?s.clip_x0:0))/128.f-1;
            const float x1=(s.x+int(s.affine?s.clip_x1:s.width))/128.f-1;
            const float y0=1-(s.y+shift+int(by0))/96.f,y1=1-(s.y+shift+int(by1))/96.f;
            float u0=s.hflip?1:0,u1=s.hflip?0:1,v0=s.vflip?1:0,v1=s.vflip?0:1;
            if(s.affine){
                // Pixel-centre compensation; positive 1/512 texel is below
                // the smallest fixed8 fraction (1/256).
                auto uv=[](unsigned edge,unsigned bound,unsigned source,int coefficient){
                    return ((float(edge)-bound*.5f-.5f)*coefficient/256.f+source*.5f+1.f/512.f)/source;
                };
                u0=uv(s.clip_x0,s.bound_width,s.width,s.pa);u1=uv(s.clip_x1,s.bound_width,s.width,s.pa);
                v0=uv(by0,s.bound_height,s.height,s.pd);v1=uv(by1,s.bound_height,s.height,s.pd);
            }
            Vertex q[4]={{{x0,y0,-.5f,1},{u0,v0},{0,0}},{{x1,y0,-.5f,1},{u1,v0},{0,0}},
                {{x1,y1,-.5f,1},{u1,v1},{0,0}},{{x0,y1,-.5f,1},{u0,v1},{0,0}}};
            if(s.affine && (s.pb || s.pc)){
                // DS: texel = ((P*(pixel-bound/2))>>8)+size/2 at integer pixel
                // positions. The GPU samples pixel centres, so evaluate the
                // same affine map at the corner positions shifted by -0.5.
                auto corner=[&](Vertex& v,unsigned cx,unsigned cy){
                    const float dx=float(cx)-s.bound_width*.5f-.5f,dy=float(cy)-s.bound_height*.5f-.5f;
                    v.uv[0]=((s.pa*dx+s.pb*dy)/256.f+s.width*.5f+1.f/512.f)/s.width;
                    v.uv[1]=((s.pc*dx+s.pd*dy)/256.f+s.height*.5f+1.f/512.f)/s.height;
                };
                corner(q[0],s.clip_x0,by0);corner(q[1],s.clip_x1,by0);corner(q[2],s.clip_x1,by1);corner(q[3],s.clip_x0,by1);
            }
            const unsigned indices[]={0,1,2,0,2,3};
            for(unsigned k=0;k<6;++k)vertices_[vertex_count++]=q[indices[k]];
        }
        counts[i]=vertex_count-first[i];
    }
    GSPGPU_FlushDataCache(vertices_,vertex_count*sizeof(Vertex));
    C3D_RenderTargetClear(output.target,C3D_CLEAR_COLOR,0,0);
    C3D_FrameDrawOn(output.target);C3D_SetViewport(0,0,256,192);
    C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
    C3D_BindProgram(&program_);C3D_SetAttrInfo(&attributes_);C3D_LightEnvBind(nullptr);
    auto* buffer=C3D_GetBufInfo();BufInfo_Init(buffer);BufInfo_Add(buffer,vertices_,sizeof(Vertex),3,0x210);
    C3D_CullFace(GPU_CULL_NONE);C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(true,GPU_GREATER,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_CONSTANT,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
    for(unsigned i=0;i<plan.count;++i){
        if(!counts[i])continue;
        C3D_TexBind(0,&ready[i]->gpu);
        C3D_TexEnvColor(C3D_GetTexEnv(0),0x00ffffffu|((16*(plan.sprites[plan.order[i]].priority+1))<<24));
        C3D_DrawArrays(GPU_TRIANGLES,first[i],counts[i]);
        ++draw_stats_.sprites;++draw_stats_.draws;
        if(plan.sprites[plan.order[i]].affine)++draw_stats_.affine;
        draw_stats_.vertices+=counts[i];
    }
    return true;
}
}
