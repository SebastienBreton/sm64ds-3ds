// visual-enhancements: see enhance_upscale.h.
#include "enhance_upscale.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace enhance {

namespace {
inline unsigned A(uint32_t c){return c>>24;}
inline unsigned R(uint32_t c){return (c>>16)&255;}
inline unsigned G(uint32_t c){return (c>>8)&255;}
inline unsigned B(uint32_t c){return c&255;}
inline uint32_t pack(unsigned a,unsigned r,unsigned g,unsigned b){return a<<24|r<<16|g<<8|b;}
// DS layers: every fully transparent pixel compares equal whatever its RGB.
inline uint32_t canon(uint32_t c){return A(c)?c:0u;}
}

const char* method_name(Method m){
    switch(m){
    case Method::Nearest:return "nearest";
    case Method::Sharp:return "sharp";
    case Method::Bilinear:return "bilinear";
    case Method::Epx:return "epx";
    case Method::Mmpx:return "mmpx";
    case Method::SuperXbr:return "superxbr";
    case Method::MmpxSuperXbr:return "mmpx+superxbr";
    case Method::Xbr:return "xbr";
    default:return "?";
    }
}
bool method_from_name(const char* name,Method& out){
    for(unsigned i=0;i<unsigned(Method::Count);++i)
        if(!std::strcmp(name,method_name(Method(i)))){out=Method(i);return true;}
    return false;
}

// ---- EPX / Scale2x --------------------------------------------------------
Image epx2x(const Image& s){
    Image o(s.width*2,s.height*2);
    for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x){
        const uint32_t P=canon(s.at(x,y)),Au=canon(s.at(x,y-1)),Bv=canon(s.at(x+1,y)),C=canon(s.at(x-1,y)),D=canon(s.at(x,y+1));
        uint32_t e0=P,e1=P,e2=P,e3=P;
        if(C==Au&&C!=D&&Au!=Bv)e0=Au;
        if(Au==Bv&&Au!=C&&Bv!=D)e1=Bv;
        if(D==C&&D!=Bv&&C!=Au)e2=C;
        if(Bv==D&&Bv!=Au&&D!=C)e3=D;
        uint32_t* r0=&o.px[size_t(2*y)*o.width+2*x];uint32_t* r1=r0+o.width;
        r0[0]=e0;r0[1]=e1;r1[0]=e2;r1[1]=e3;
    }
    return o;
}

// ---- MMPX (McGuire & Gagiu 2021, MIT reference implementation) ------------
namespace {
// Reference luma: transparent pixels count as very bright, so dark outlines
// win against empty space.
inline int luma(uint32_t c){return int(R(c)+G(c)+B(c)+1)*int(256-A(c));}
inline bool all_eq2(uint32_t b,uint32_t a0,uint32_t a1){return b==a0&&b==a1;}
inline bool all_eq3(uint32_t b,uint32_t a0,uint32_t a1,uint32_t a2){return b==a0&&b==a1&&b==a2;}
inline bool all_eq4(uint32_t b,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3){return b==a0&&b==a1&&b==a2&&b==a3;}
inline bool any_eq3(uint32_t b,uint32_t a0,uint32_t a1,uint32_t a2){return b==a0||b==a1||b==a2;}
inline bool none_eq2(uint32_t b,uint32_t a0,uint32_t a1){return b!=a0&&b!=a1;}
inline bool none_eq4(uint32_t b,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3){return b!=a0&&b!=a1&&b!=a2&&b!=a3;}
}
Image mmpx2x(const Image& s){
    Image o(s.width*2,s.height*2);
    auto src=[&](int x,int y){return canon(s.at(x,y));};
    for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x){
        const uint32_t a=src(x-1,y-1),b=src(x,y-1),c=src(x+1,y-1);
        const uint32_t d=src(x-1,y),  e=src(x,y),  f=src(x+1,y);
        const uint32_t g=src(x-1,y+1),h=src(x,y+1),i=src(x+1,y+1);
        const uint32_t p=src(x,y-2),q=src(x-2,y),r=src(x+2,y),ss=src(x,y+2);
        const int bl=luma(b),dl=luma(d),el=luma(e),fl=luma(f),hl=luma(h);
        uint32_t J=e,K=e,L=e,M=e;
        if(!(a==e&&b==e&&c==e&&d==e&&f==e&&g==e&&h==e&&i==e)){
            // 1:1 slope rules
            if((d==b&&d!=h&&d!=f)&&(el>=dl||e==a)&&any_eq3(e,a,c,g)&&((el<dl)||a!=d||e!=p||e!=q))J=d;
            if((b==f&&b!=d&&b!=h)&&(el>=bl||e==c)&&any_eq3(e,a,c,i)&&((el<bl)||c!=b||e!=p||e!=r))K=b;
            if((h==d&&h!=f&&h!=b)&&(el>=hl||e==g)&&any_eq3(e,a,g,i)&&((el<hl)||g!=h||e!=ss||e!=q))L=h;
            if((f==h&&f!=b&&f!=d)&&(el>=fl||e==i)&&any_eq3(e,c,g,i)&&((el<fl)||i!=h||e!=r||e!=ss))M=f;
            // Intersection rules
            if((e!=f&&all_eq4(e,c,i,d,q)&&all_eq2(f,b,h))&&(f!=src(x+3,y))){K=f;M=f;}
            if((e!=d&&all_eq4(e,a,g,f,r)&&all_eq2(d,b,h))&&(d!=src(x-3,y))){J=d;L=d;}
            if((e!=h&&all_eq4(e,g,i,b,p)&&all_eq2(h,d,f))&&(h!=src(x,y+3))){L=h;M=h;}
            if((e!=b&&all_eq4(e,a,c,h,ss)&&all_eq2(b,d,f))&&(b!=src(x,y-3))){J=b;K=b;}
            if(bl<el&&all_eq4(e,g,h,i,ss)&&none_eq4(e,a,d,c,f)){J=b;K=b;}
            if(hl<el&&all_eq4(e,a,b,c,p)&&none_eq4(e,d,g,i,f)){L=h;M=h;}
            if(fl<el&&all_eq4(e,a,d,g,q)&&none_eq4(e,b,c,i,h)){K=f;M=f;}
            if(dl<el&&all_eq4(e,c,f,i,r)&&none_eq4(e,b,a,g,h)){J=d;L=d;}
            // 2:1 slope rules
            if(h!=b){
                if(h!=a&&h!=e&&h!=c){
                    if(all_eq3(h,g,f,r)&&none_eq2(h,d,src(x+2,y-1)))L=M;
                    if(all_eq3(h,i,d,q)&&none_eq2(h,f,src(x-2,y-1)))M=L;
                }
                if(b!=i&&b!=g&&b!=e){
                    if(all_eq3(b,a,f,r)&&none_eq2(b,d,src(x+2,y+1)))J=K;
                    if(all_eq3(b,c,d,q)&&none_eq2(b,f,src(x-2,y+1)))K=J;
                }
            }
            if(f!=d){
                if(d!=i&&d!=e&&d!=c){
                    if(all_eq3(d,a,h,ss)&&none_eq2(d,b,src(x+1,y+2)))J=L;
                    if(all_eq3(d,g,b,p)&&none_eq2(d,h,src(x+1,y-2)))L=J;
                }
                if(f!=e&&f!=a&&f!=g){
                    if(all_eq3(f,c,h,ss)&&none_eq2(f,b,src(x-1,y+2)))K=M;
                    if(all_eq3(f,i,b,p)&&none_eq2(f,h,src(x-1,y-2)))M=K;
                }
            }
        }
        uint32_t* r0=&o.px[size_t(2*y)*o.width+2*x];uint32_t* r1=r0+o.width;
        r0[0]=J;r0[1]=K;r1[0]=L;r1[1]=M;
    }
    return o;
}

// ---- Super-xBR (Hyllian 2016, MIT) ----------------------------------------
// Fixed-weight edge-directed 2x in three passes, with anti-ringing clamps.
// Works on premultiplied RGBA so transparent texels never bleed colour.
namespace {
struct PM {float c[4];};    // premultiplied r,g,b,a in 0..255
struct Plane {
    int w,h;std::vector<PM> p;
    Plane(int W,int H):w(W),h(H),p(size_t(W)*size_t(H)){}
    const PM& at(int x,int y) const{x=x<0?0:(x>=w?w-1:x);y=y<0?0:(y>=h?h-1:y);return p[size_t(y)*w+x];}
    PM& set(int x,int y){return p[size_t(y)*w+x];}
};
inline float Y(const PM& v){return .2126f*v.c[0]+.7152f*v.c[1]+.0722f*v.c[2];}
inline float df(float a,float b){return std::fabs(a-b);}
float diagonal_edge(const float m[4][4],const float* wp){
    const float dw1=wp[0]*(df(m[0][2],m[1][1])+df(m[1][1],m[2][0])+df(m[1][3],m[2][2])+df(m[2][2],m[3][1]))+
        wp[1]*(df(m[0][3],m[1][2])+df(m[2][1],m[3][0]))+wp[2]*(df(m[0][3],m[2][1])+df(m[1][2],m[3][0]))+
        wp[3]*df(m[1][2],m[2][1])+wp[4]*(df(m[0][2],m[2][0])+df(m[1][3],m[3][1]))+wp[5]*(df(m[0][1],m[1][0])+df(m[2][3],m[3][2]));
    const float dw2=wp[0]*(df(m[0][1],m[1][2])+df(m[1][2],m[2][3])+df(m[1][0],m[2][1])+df(m[2][1],m[3][2]))+
        wp[1]*(df(m[0][0],m[1][1])+df(m[2][2],m[3][3]))+wp[2]*(df(m[0][0],m[2][2])+df(m[1][1],m[3][3]))+
        wp[3]*df(m[1][1],m[2][2])+wp[4]*(df(m[1][0],m[3][2])+df(m[0][1],m[2][3]))+wp[5]*(df(m[0][2],m[1][3])+df(m[2][0],m[3][1]));
    return dw1-dw2;
}
// s[i][j]: 4x4 neighbourhood (i = sx+1, j = sy+1 in the reference layout).
PM interpolate(const PM s[4][4],const float* wp,float wa,float wb){
    float y[4][4];for(int i=0;i<4;++i)for(int j=0;j<4;++j)y[i][j]=Y(s[i][j]);
    const float edge=diagonal_edge(y,wp);
    PM out;
    for(int k=0;k<4;++k){
        const float lo=std::min(std::min(s[1][1].c[k],s[2][1].c[k]),std::min(s[1][2].c[k],s[2][2].c[k]));
        const float hi=std::max(std::max(s[1][1].c[k],s[2][1].c[k]),std::max(s[1][2].c[k],s[2][2].c[k]));
        float v=edge<=0?wa*(s[0][3].c[k]+s[3][0].c[k])+wb*(s[1][2].c[k]+s[2][1].c[k])
                       :wa*(s[0][0].c[k]+s[3][3].c[k])+wb*(s[1][1].c[k]+s[2][2].c[k]);
        out.c[k]=std::min(std::max(v,lo),hi);
    }
    // premultiplied invariant: colour <= alpha
    for(int k=0;k<3;++k)out.c[k]=std::min(out.c[k],out.c[3]);
    return out;
}
}
Image superxbr2x(const Image& src){
    const float wgt1=.129633f,wgt2=.175068f,w1=-wgt1,w2=wgt1+.5f,w3=-wgt2,w4=wgt2+.5f;
    const int W=src.width,H=src.height,OW=W*2,OH=H*2;
    Plane in(W,H);
    for(int i=0;i<W*H;++i){const uint32_t c=src.px[i];const float a=float(A(c));
        in.p[i]={{R(c)*a/255.f,G(c)*a/255.f,B(c)*a/255.f,a}};}
    Plane out(OW,OH);
    PM s[4][4];
    float wp[6]={2.f,1.f,-1.f,4.f,-1.f,1.f};
    // pass 1: originals at even positions, diagonal centre at (odd,odd)
    for(int y=0;y<OH;y+=2)for(int x=0;x<OW;x+=2){
        const int cx=x/2,cy=y/2;
        for(int sx=-1;sx<=2;++sx)for(int sy=-1;sy<=2;++sy)s[sx+1][sy+1]=in.at(cx+sx,cy+sy);
        const PM o=in.at(cx,cy);
        out.set(x,y)=o;out.set(x+1,y)=o;out.set(x,y+1)=o;
        out.set(x+1,y+1)=interpolate(s,wp,w1,w2);
    }
    // pass 2: the two remaining (odd,even)/(even,odd) samples, rotated grid
    const float wp2[6]={2.f,0,0,0,0,0};
    for(int y=0;y<OH;y+=2)for(int x=0;x<OW;x+=2){
        for(int sx=-1;sx<=2;++sx)for(int sy=-1;sy<=2;++sy)s[sx+1][sy+1]=out.at(sx+sy+x,sx-sy+y);
        out.set(x+1,y)=interpolate(s,wp2,w3,w4);
        for(int sx=-1;sx<=2;++sx)for(int sy=-1;sy<=2;++sy)s[sx+1][sy+1]=out.at(sx+sy-1+x,sx-sy+1+y);
        out.set(x,y+1)=interpolate(s,wp2,w3,w4);
    }
    // pass 3: sharpening pass over every output pixel (reverse order, in place)
    for(int y=OH-1;y>=0;--y)for(int x=OW-1;x>=0;--x){
        for(int sx=-2;sx<=1;++sx)for(int sy=-2;sy<=1;++sy)s[sx+2][sy+2]=out.at(sx+x,sy+y);
        out.set(x,y)=interpolate(s,wp,w1,w2);
    }
    Image o(OW,OH);
    for(int i=0;i<OW*OH;++i){
        const PM& v=out.p[i];const float a=v.c[3];
        if(a<.5f){o.px[i]=0;continue;}
        auto ch=[&](float c){int q=int(std::ceil(c*255.f/a-.001f));return unsigned(q<0?0:q>255?255:q);};
        const int ai=int(std::lround(a));
        o.px[i]=pack(unsigned(ai>255?255:ai),ch(v.c[0]),ch(v.c[1]),ch(v.c[2]));
    }
    return o;
}


// ---- 4xBR (Hyllian; port of the MIT xbr-js 2.0.1 kernel4Xv2) ---------------
namespace {
struct Yuv{float y,u,v;};
inline Yuv yuv_raw(uint32_t p){const float r=float(R(p)),g=float(G(p)),b=float(B(p));
    return {r*.299f+g*.587f+b*.114f,r*-.168736f+g*-.331264f+b*.5f,r*.5f+g*-.418688f+b*-.081312f};}
// DS art has few distinct colours: memoise the conversion (ARM11 VFP is slow).
struct YuvCache{uint32_t key[2048];Yuv val[2048];bool used[2048];};
YuvCache* g_yuv_cache=nullptr;
inline Yuv yuv(uint32_t p){
    if(!g_yuv_cache)return yuv_raw(p);
    unsigned slot=((p*2654435761u)>>21)&2047u;
    for(unsigned probe=0;probe<8;++probe,slot=(slot+1)&2047u){
        if(!g_yuv_cache->used[slot]){g_yuv_cache->used[slot]=true;g_yuv_cache->key[slot]=p;return g_yuv_cache->val[slot]=yuv_raw(p);}
        if(g_yuv_cache->key[slot]==p)return g_yuv_cache->val[slot];
    }
    return yuv_raw(p);
}
inline int ydiff(uint32_t a,uint32_t b){
    const unsigned aa=A(a),ab=A(b);
    if(!aa&&!ab)return 0;
    if(aa<255||ab<255)return 1000000;
    const Yuv x=yuv(a),y=yuv(b);
    return int(std::fabs(x.y-y.y)*48.f+std::fabs(x.u-y.u)*7.f+std::fabs(x.v-y.v)*6.f);
}
inline bool yeq(uint32_t a,uint32_t b){
    const unsigned aa=A(a),ab=A(b);
    if(!aa&&!ab)return true;
    if(aa<255||ab<255)return false;
    const Yuv x=yuv(a),y=yuv(b);
    return std::fabs(x.y-y.y)<=48.f&&std::fabs(x.u-y.u)<=7.f&&std::fabs(x.v-y.v)<=6.f;
}
// (q2*B + q1*A)/(q1+q2); a transparent side contributes alpha only.
inline uint32_t interp(uint32_t a,uint32_t b,unsigned q1,unsigned q2){
    const unsigned aa=A(a),ab=A(b),s=q1+q2;unsigned r,g,bl;
    if(!aa){r=R(b);g=G(b);bl=B(b);}
    else if(!ab){r=R(a);g=G(a);bl=B(a);}
    else{r=(q2*R(b)+q1*R(a))/s;g=(q2*G(b)+q1*G(a))/s;bl=(q2*B(b)+q1*B(a))/s;}
    return pack((q2*ab+q1*aa)/s,r,g,bl);
}
inline uint32_t b64(uint32_t d,uint32_t s){return interp(d,s,3,1);}
inline uint32_t b128(uint32_t d,uint32_t s){return interp(d,s,1,1);}
inline uint32_t b192(uint32_t d,uint32_t s){return interp(d,s,1,3);}
void kernel4x(uint32_t pe,uint32_t pi,uint32_t ph,uint32_t pf,uint32_t pg,uint32_t pc,uint32_t pd,uint32_t pb,
              uint32_t f4,uint32_t i4,uint32_t h5,uint32_t i5,
              uint32_t& n15,uint32_t& n14,uint32_t& n11,uint32_t& n3,uint32_t& n7,uint32_t& n10,uint32_t& n13,uint32_t& n12){
    if(!(pe!=ph&&pe!=pf))return;
    const int e=(ydiff(pe,pc)+ydiff(pe,pg)+ydiff(pi,h5)+ydiff(pi,f4))+(ydiff(ph,pf)<<2);
    const int i=(ydiff(ph,pd)+ydiff(ph,i5)+ydiff(pf,i4)+ydiff(pf,pb))+(ydiff(pe,pi)<<2);
    const uint32_t px=ydiff(pe,pf)<=ydiff(pe,ph)?pf:ph;
    if((e<i)&&((!yeq(pf,pb)&&!yeq(ph,pd))||(yeq(pe,pi)&&(!yeq(pf,i4)&&!yeq(ph,i5)))||yeq(pe,pg)||yeq(pe,pc))){
        const int ke=ydiff(pf,pg),ki=ydiff(ph,pc);
        const bool ex2=(pe!=pc&&pb!=pc),ex3=(pe!=pg&&pd!=pg);
        const bool left=((ke<<1)<=ki)&&ex3,up=(ke>=(ki<<1))&&ex2;
        if(left||up){
            if(left){n11=b192(n11,px);n13=b192(n13,px);n12=b64(n12,px);n10=b64(n10,px);n15=px;n14=px;}
            if(up){n14=b192(n14,px);n3=b64(n3,px);n7=b192(n7,px);n10=b64(n10,px);n15=px;n11=px;}
        }else{n14=b128(n14,px);n11=b128(n11,px);n15=px;}
    }else if(e<=i)n15=b128(n15,px);
}
}
Image xbr4x(const Image& s){
    Image o(s.width*4,s.height*4);
    std::vector<YuvCache> cache(1);std::memset(cache[0].used,0,sizeof cache[0].used);
    g_yuv_cache=&cache[0];
    struct Reset{~Reset(){g_yuv_cache=nullptr;}} reset;
    auto P=[&](int x,int y){return canon(s.at(x,y));};
    for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x){
        const uint32_t a1=P(x-1,y-2),b1=P(x,y-2),c1=P(x+1,y-2);
        const uint32_t a0=P(x-2,y-1),pa=P(x-1,y-1),pb=P(x,y-1),pc=P(x+1,y-1),c4=P(x+2,y-1);
        const uint32_t d0=P(x-2,y),pd=P(x-1,y),pe=P(x,y),pf=P(x+1,y),f4=P(x+2,y);
        const uint32_t g0=P(x-2,y+1),pg=P(x-1,y+1),ph=P(x,y+1),pi=P(x+1,y+1),i4=P(x+2,y+1);
        const uint32_t g5=P(x-1,y+2),h5=P(x,y+2),i5=P(x+1,y+2);
        uint32_t e[16];for(auto& v:e)v=pe;
        kernel4x(pe,pi,ph,pf,pg,pc,pd,pb,f4,i4,h5,i5,e[15],e[14],e[11],e[3],e[7],e[10],e[13],e[12]);
        kernel4x(pe,pc,pf,pb,pi,pa,ph,pd,b1,c1,f4,c4,e[3],e[7],e[2],e[0],e[1],e[6],e[11],e[15]);
        kernel4x(pe,pa,pb,pd,pc,pg,pf,ph,d0,a0,b1,a1,e[0],e[1],e[4],e[12],e[8],e[5],e[2],e[3]);
        kernel4x(pe,pg,pd,ph,pa,pi,pb,pf,h5,g5,d0,g0,e[12],e[8],e[13],e[15],e[14],e[9],e[4],e[0]);
        for(int r=0;r<4;++r)for(int c=0;c<4;++c)o.px[size_t(4*y+r)*o.width+4*x+c]=e[r*4+c];
    }
    return o;
}

// ---- resamplers -------------------------------------------------------------
Image resize_area(const Image& s,int ow,int oh){
    Image o(ow,oh);
    const double fx=double(s.width)/ow,fy=double(s.height)/oh;
    for(int y=0;y<oh;++y){
        const double y0=y*fy,y1=(y+1)*fy;
        for(int x=0;x<ow;++x){
            const double x0=x*fx,x1=(x+1)*fx;
            double acc[4]={0,0,0,0},wsum=0;
            for(int sy=int(y0);sy<int(std::ceil(y1))&&sy<s.height;++sy){
                const double wy=std::min(y1,double(sy+1))-std::max(y0,double(sy));
                if(wy<=0)continue;
                for(int sx=int(x0);sx<int(std::ceil(x1))&&sx<s.width;++sx){
                    const double wx=std::min(x1,double(sx+1))-std::max(x0,double(sx));
                    if(wx<=0)continue;
                    const double w=wx*wy;const uint32_t c=s.px[size_t(sy)*s.width+sx];const double a=A(c);
                    acc[0]+=w*R(c)*a;acc[1]+=w*G(c)*a;acc[2]+=w*B(c)*a;acc[3]+=w*a;wsum+=w;
                }
            }
            if(wsum<=0||acc[3]<=0){o.px[size_t(y)*ow+x]=0;continue;}
            const double a=acc[3]/wsum;
            auto ch=[&](double v){long q=std::lround(v/acc[3]);return unsigned(q<0?0:q>255?255:q);};
            const long ai=std::lround(a);
            o.px[size_t(y)*ow+x]=pack(unsigned(ai>255?255:ai),ch(acc[0]),ch(acc[1]),ch(acc[2]));
        }
    }
    return o;
}
Image resize_nearest(const Image& s,int ow,int oh){
    Image o(ow,oh);
    for(int y=0;y<oh;++y)for(int x=0;x<ow;++x)
        o.px[size_t(y)*ow+x]=s.at(int((x+.5)*s.width/ow),int((y+.5)*s.height/oh));
    return o;
}
Image resize_bilinear(const Image& s,int ow,int oh){
    Image o(ow,oh);
    for(int y=0;y<oh;++y)for(int x=0;x<ow;++x){
        const double sx=(x+.5)*s.width/ow-.5,sy=(y+.5)*s.height/oh-.5;
        const int x0=int(std::floor(sx)),y0=int(std::floor(sy));const double tx=sx-x0,ty=sy-y0;
        double acc[4]={0,0,0,0};
        const uint32_t c[4]={s.at(x0,y0),s.at(x0+1,y0),s.at(x0,y0+1),s.at(x0+1,y0+1)};
        const double w[4]={(1-tx)*(1-ty),tx*(1-ty),(1-tx)*ty,tx*ty};
        for(int k=0;k<4;++k){const double a=A(c[k]);acc[0]+=w[k]*R(c[k])*a;acc[1]+=w[k]*G(c[k])*a;acc[2]+=w[k]*B(c[k])*a;acc[3]+=w[k]*a;}
        if(acc[3]<=0){o.px[size_t(y)*ow+x]=0;continue;}
        auto ch=[&](double v){long q=std::lround(v/acc[3]);return unsigned(q<0?0:q>255?255:q);};
        o.px[size_t(y)*ow+x]=pack(unsigned(std::lround(acc[3])),ch(acc[0]),ch(acc[1]),ch(acc[2]));
    }
    return o;
}

Image upscale(const Image& src,Method m,int ow,int oh){
    // number of 2x passes so that the intermediate is >= the output size
    auto passes_for=[&](int max_passes){
        int n=0;int w=src.width,h=src.height;
        while(n<max_passes&&(w<ow||h<oh)){w*=2;h*=2;++n;}
        return n;
    };
    switch(m){
    case Method::Nearest:return resize_nearest(src,ow,oh);
    case Method::Bilinear:return resize_bilinear(src,ow,oh);
    case Method::Sharp:{    // = the port's presentation: nearest x2, then filtered down
        Image t=resize_nearest(src,src.width*2,src.height*2);
        return resize_bilinear(t,ow,oh);
    }
    case Method::Epx:{Image t=src;for(int n=passes_for(3);n>0;--n)t=epx2x(t);return resize_area(t,ow,oh);}
    case Method::Mmpx:{Image t=src;for(int n=std::max(1,passes_for(3));n>0;--n)t=mmpx2x(t);return resize_area(t,ow,oh);}
    case Method::SuperXbr:{Image t=src;for(int n=std::max(1,passes_for(2));n>0;--n)t=superxbr2x(t);return resize_area(t,ow,oh);}
    case Method::MmpxSuperXbr:{Image t=superxbr2x(mmpx2x(src));return resize_area(t,ow,oh);}
    case Method::Xbr:return resize_area(xbr4x(src),ow,oh);
    default:return resize_nearest(src,ow,oh);
    }
}

void alpha_floor(Image& img,uint8_t threshold){
    for(auto& c:img.px)if(A(c)<threshold)c=0;
}
uint64_t content_hash(const Image& img){
    uint64_t h=1469598103934665603ull;
    auto mix=[&](uint32_t v){for(int k=0;k<4;++k){h^=(v>>(8*k))&255u;h*=1099511628211ull;}};
    mix(uint32_t(img.width));mix(uint32_t(img.height));
    for(uint32_t c:img.px)mix(canon(c));
    return h;
}

}  // namespace enhance
