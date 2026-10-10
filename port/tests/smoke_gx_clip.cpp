#include "ntr/gx.h"
#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstring>

static ntr::GxVertex vertex(float x,float y,float z,float w) {
    ntr::GxVertex v{};
    v.cx=x;v.cy=y;v.cz=z;v.cw=v.w=w;
    v.x=(x/w+1)*128;v.y=(1-y/w)*96;v.z=(z/w+1)*.5f;
    v.color=0xffffffff;return v;
}

int main() {
    if(!ntr::io_init() || !Heap::SetupRootHeap())return 2;
    static ntr::Framebuffer fb;
    ntr::GxTriangle t{};t.polygon_attr=0x001f00c0;
    t.v[0]=vertex(-.8f,-.8f,0,1);
    t.v[1]=vertex(.8f,-.8f,0,1);
    t.v[2]=vertex(0,2,-2,-1);
    ntr::gx_reset();ntr::gx_append_polygons(&t,1);ntr::gx_render(fb);
    // The visible trapezoid crosses the centre at y=150; y=80 is
    // beyond its near-plane intersection. Dropping the triangle loses both.
    if(fb.px[150][128]!=0xffffffff || fb.px[80][128]!=0){puts("FAIL eye-plane clipping");return 1;}
    std::memset(&fb,0,sizeof fb);
    for(auto& v:t.v){v.w=v.cw=1;v.cz=2;v.z=1.5f;}
    ntr::gx_begin_frame();ntr::gx_append_polygons(&t,1);ntr::gx_render(fb);
    for(const auto& row:fb.px)for(auto p:row)if(p){puts("FAIL far-plane rejection");return 1;}
    // A subpixel tilt smaller than one native pixel must not produce a
    // staircase in a DS UI rectangle. Both top vertices truncate to y=18.
    ntr::GxTriangle rect[2]{};
    auto screen=[](float x,float y){return vertex(x/128-1,1-y/96,0,1);};
    rect[0].polygon_attr=rect[1].polygon_attr=0x001f00c0;
    rect[0].v[0]=screen(4.4f,18.1f);rect[0].v[1]=screen(135.8f,18.8f);rect[0].v[2]=screen(135.8f,187.7f);
    rect[1].v[0]=rect[0].v[0];rect[1].v[1]=rect[0].v[2];rect[1].v[2]=screen(4.4f,187.1f);
    ntr::gx_begin_frame();ntr::gx_append_polygons(rect,2);ntr::gx_render(fb);
    for(int x=5;x<135;++x)if(fb.px[18][x]!=0xffffffff || fb.px[17][x]){puts("FAIL integer viewport edge");return 1;}
    // Actual DS opening-letter clip vertices, script020890a0 timer120.
    // Its right vertices quantize to x136(top)/135(bottom), yet DS samples
    // integer pixels: both visible sides stay at x4 and x135 throughout.
    const ntr::GxVertex corners[]={vertex(-5337.f/4096,-5297.f/4096,5016.f/4096,5528.f/4096),
        vertex(338.f/4096,-5328.f/4096,5016.f/4096,5528.f/4096),
        vertex(362.f/4096,4455.f/4096,5016.f/4096,5528.f/4096),
        vertex(-5313.f/4096,4486.f/4096,5016.f/4096,5528.f/4096)};
    rect[0].v[0]=corners[0];rect[0].v[1]=corners[1];rect[0].v[2]=corners[2];
    rect[1].v[0]=corners[0];rect[1].v[1]=corners[2];rect[1].v[2]=corners[3];
    std::memset(&fb,0,sizeof fb);ntr::gx_begin_frame();ntr::gx_append_polygons(rect,2);ntr::gx_render(fb);
    for(int y=18;y<187;++y)if(fb.px[y][3] || fb.px[y][4]!=0xffffffff || fb.px[y][135]!=0xffffffff || fb.px[y][136]) {
        printf("FAIL DS letter lateral edge at y=%d\n",y);return 1;
    }
    ntr::io_write(0x04000060,8,2);
    t.v[0]=vertex(-.8f,-.8f,0,1);t.v[1]=vertex(.8f,-.8f,0,1);t.v[2]=vertex(0,.8f,0,1);
    for(auto& v:t.v)v.color=0xff0000ff;
    ntr::gx_begin_frame();ntr::gx_append_polygons(&t,1);
    t.polygon_attr=0x010f00c0;
    for(auto& v:t.v){v.color=0xffff0000;v.cz=-.5f;v.z=.25f;}
    ntr::gx_append_polygons(&t,1);ntr::gx_append_polygons(&t,1);
    ntr::gx_render(fb);
    if(fb.px[120][128]!=0xff7d007d){printf("FAIL alpha/ID %08lx\n",(unsigned long)fb.px[120][128]);return 1;}
    // Translucent polygons leave depth alone unless bit11 requests a write.
    t.polygon_attr=0x021f00c0;
    for(auto& v:t.v){v.color=0xff00ff00;v.cz=-.25f;v.z=.375f;}
    ntr::gx_append_polygons(&t,1);ntr::gx_render(fb);
    if(fb.px[120][128]!=0xff00ff00){puts("FAIL translucent depth");return 1;}
    puts("PASS: near/far clipping, polygon alpha, repeated ID, translucent depth");
    return 0;
}
