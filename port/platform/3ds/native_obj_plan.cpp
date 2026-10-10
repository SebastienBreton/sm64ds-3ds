#include "native_obj_plan.h"
#include <cstring>
#include <algorithm>

namespace ds_gpu {
namespace {
constexpr unsigned sizes[3][4][2]={
    {{8,8},{16,16},{32,32},{64,64}},
    {{16,8},{32,8},{32,16},{64,32}},
    {{8,16},{8,32},{16,32},{32,64}}};
unsigned rd16(const uint8_t* p){return p[0]|(unsigned(p[1])<<8);}
uint32_t color6(unsigned c){
    const unsigned r=c&31,g=(c>>5)&31,b=(c>>10)&31;
    return 0xff000000u|((r<<3|r>>3)<<16)|((g<<3|g>>3)<<8)|(b<<3|b>>3);
}
}
ObjFallback obj_admission(uint32_t display,const uint8_t* oam){
    if(((display>>16)&3)>1)return ObjFallback::DisplayMode;
    if(!(display&(1u<<12)) || (display&0x80) || !((display>>16)&3))return ObjFallback::None;
    if(display&0xe000)return ObjFallback::Windows;
    if(!oam)return ObjFallback::MissingViews;
    for(unsigned i=0;i<128;++i){
        const unsigned a0=rd16(oam+i*8),a1=rd16(oam+i*8+2);
        const bool affine=a0&0x100;
        if(!affine && (a0&0x200))continue;
        const unsigned shape=a0>>14,size=a1>>14;
        if(shape==3)return ObjFallback::Shape;
        const unsigned w=sizes[shape][size][0],h=sizes[shape][size][1];
        const unsigned bw=affine && (a0&0x200)?w*2:w,bh=affine && (a0&0x200)?h*2:h;
        int x=a1&511,y=a0&255;
        if(x>=256)x-=512;
        if(y>=192 && y>=int(256-bh))y-=256;
        if(x>=256 || x+int(bw)<=0 || y>=192 || y+int(bh)<=0)continue;
        // Rotated/sheared affine OBJs are drawn by NativeObjects::draw with
        // per-corner texture coordinates and a transparent clamp border.
        if(a0&0x1000)return ObjFallback::Mosaic;
        const unsigned mode=(a0>>10)&3;
        if(mode==1)return ObjFallback::Alpha;
        if(mode==2)return ObjFallback::Windows;
        if(mode==3)return ObjFallback::Bitmap;
        if((a0&0x2000) && (display&0x80000000u))return ObjFallback::ExtendedPalette;
    }
    return ObjFallback::None;
}
bool ObjPlan::prepare(const ObjInput& in){
    count=0;fallback=obj_admission(in.display,in.oam);
    if(fallback!=ObjFallback::None)return false;
    auto reject=[&](ObjFallback why){fallback=why;count=0;return false;};
    if(((in.display>>16)&3)>1)return reject(ObjFallback::DisplayMode);
    if(!(in.display&(1u<<12)) || (in.display&0x80) || !((in.display>>16)&3))return true;
    if(in.display&0xe000)return reject(ObjFallback::Windows);
    if(!in.oam || !in.vram || !in.palette ||
       (in.vram_mask!=0x1ffff && in.vram_mask!=0x3ffff))return reject(ObjFallback::MissingViews);
    const bool one_d=in.display&16;
    const unsigned boundary=32u<<((in.display>>20)&3);
    std::array<bool,128> visible{};
    for(unsigned i=0;i<128;++i){
        const unsigned a0=rd16(in.oam+i*8),a1=rd16(in.oam+i*8+2),a2=rd16(in.oam+i*8+4);
        const bool affine=a0&0x100;
        if(!affine && (a0&0x200))continue;
        const unsigned shape=a0>>14,size=a1>>14;
        if(shape==3)return reject(ObjFallback::Shape);
        const unsigned w=sizes[shape][size][0],h=sizes[shape][size][1];
        const unsigned bw=affine && (a0&0x200)?w*2:w,bh=affine && (a0&0x200)?h*2:h;
        int x=a1&511,y=a0&255;
        if(x>=256)x-=512;
        if(y>=192 && y>=int(256-bh))y-=256;
        if(x>=256 || x+int(bw)<=0 || y>=192 || y+int(bh)<=0)continue;
        
        if(a0&0x1000)return reject(ObjFallback::Mosaic);
        const unsigned mode=(a0>>10)&3;
        if(mode==1)return reject(ObjFallback::Alpha);
        if(mode==2)return reject(ObjFallback::Windows);
        if(mode==3)return reject(ObjFallback::Bitmap);
        const bool bpp8=a0&0x2000;
        if(bpp8 && (in.display&0x80000000u))return reject(ObjFallback::ExtendedPalette);
        auto& out=sprites[i];out.index=i;out.priority=(a2>>10)&3;
        out.width=w;out.height=h;out.x=x;out.y=y;
        out.hflip=!affine && (a1&0x1000);out.vflip=!affine && (a1&0x2000);out.bpp8=bpp8;
        out.affine=affine;out.bound_width=bw;out.bound_height=bh;
        out.pa=out.pd=256;out.pb=out.pc=0;out.clip_x0=out.clip_y0=0;out.clip_x1=bw;out.clip_y1=bh;
        out.clip_y_second0=out.clip_y_second1=0;
        if(affine && (rd16(in.oam+((a1>>9)&31)*32+14) || rd16(in.oam+((a1>>9)&31)*32+22))){
            // Rotation/shear: the whole bounding box is drawn; texels outside
            // the source are transparent (clamp-to-border), as on the DS.
            const unsigned matrix=(a1>>9)&31;
            out.pa=int16_t(rd16(in.oam+matrix*32+6));out.pb=int16_t(rd16(in.oam+matrix*32+14));
            out.pc=int16_t(rd16(in.oam+matrix*32+22));out.pd=int16_t(rd16(in.oam+matrix*32+30));
            if(y>=0 && y+int(bh)>256){out.clip_y_second0=0;out.clip_y_second1=bh;}
        }else if(affine){
            const unsigned matrix=(a1>>9)&31;
            out.pa=int16_t(rd16(in.oam+matrix*32+6));
            out.pd=int16_t(rd16(in.oam+matrix*32+30));
            auto clip=[](unsigned source,unsigned bound,int position,int coefficient,unsigned screen){
                std::array<unsigned,2> range{bound,0};
                for(unsigned p=0;p<bound;++p){
                    if(position+int(p)<0 || position+int(p)>=int(screen))continue;
                    const int texture=((int(p)-int(bound/2))*coefficient>>8)+int(source/2);
                    if(texture<0 || texture>=int(source))continue;
                    range[0]=std::min(range[0],p);range[1]=p+1;
                }
                return range;
            };
            const auto sx=clip(w,bw,x,out.pa,256),sy=clip(h,bh,y,out.pd,192);
            out.clip_x0=sx[0];out.clip_x1=sx[1];out.clip_y0=sy[0];out.clip_y1=sy[1];
            if(y>=0 && y+int(bh)>256){
                const auto wrap=clip(h,bh,y-256,out.pd,192);
                out.clip_y_second0=wrap[0];out.clip_y_second1=wrap[1];
            }
            if(out.clip_x0>=out.clip_x1 || (out.clip_y0>=out.clip_y1 && out.clip_y_second0>=out.clip_y_second1))continue;
        }
        const unsigned tile=a2&1023,tile_bytes=bpp8?64:32,tiles=w*h/64;
        out.encoded.resize(tiles*tile_bytes);
        for(unsigned t=0;t<tiles;++t){
            const unsigned address=one_d?tile*boundary+t*tile_bytes:
                tile*32+(t/(w/8))*1024+(t%(w/8))*tile_bytes;
            for(unsigned b=0;b<tile_bytes;++b)out.encoded[t*tile_bytes+b]=in.vram[(address+b)&in.vram_mask];
        }
        out.palette_bytes=bpp8?512:32;
        std::memcpy(out.palette.data(),in.palette+(bpp8?0:(a2>>12)*32),out.palette_bytes);
        visible[i]=true;
    }
    for(int priority=3;priority>=0;--priority)for(int i=127;i>=0;--i)
        if(visible[i] && sprites[i].priority==unsigned(priority))order[count++]=uint8_t(i);
    return true;
}
void decode_obj_texture(const ObjSnapshot& s,ntr::HostVector<uint32_t>& pixels){
    pixels.resize(s.width*s.height);
    const unsigned bytes=s.bpp8?64:32;
    for(unsigned y=0;y<s.height;++y)for(unsigned x=0;x<s.width;++x){
        const unsigned t=(y/8)*(s.width/8)+x/8,fx=x&7,fy=y&7;
        const unsigned raw=s.encoded[t*bytes+fy*(s.bpp8?8:4)+(s.bpp8?fx:fx/2)];
        const unsigned index=s.bpp8?raw:(raw>>((fx&1)*4))&15;
        pixels[y*s.width+x]=index?color6(rd16(s.palette.data()+index*2)):0;
    }
}
}
