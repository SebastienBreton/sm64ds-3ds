// Includes a frozen GX implementation so both policies exercise the same code.
// The Python driver changes only invalidation and counts cache rebuilds.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static bool matrix_selective = false;
static unsigned clip_rebuilds = 0, texture_rebuilds = 0;
#include GX_IMPLEMENTATION
namespace ntr {
uint8_t* g_region[8]{};
bool g_region_identity[8]{};
bool io_ready() { return false; }
uint64_t io_read(uint32_t, unsigned) { return 0; }
}
static unsigned rng;
static unsigned next() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void words(std::vector<unsigned>& out, const void* data, size_t size) {
    const auto* bytes=static_cast<const unsigned char*>(data);
    for(size_t i=0;i<size;i+=4) { unsigned v; std::memcpy(&v,bytes+i,4);out.push_back(v); }
}
static std::vector<unsigned> run(bool selective) {
    matrix_selective=selective; rng=0x643d5u;
    clip_rebuilds=texture_rebuilds=0;
    std::vector<unsigned> result;
    for(unsigned trial=0;trial<256;++trial) {
        ntr::gx_reset();
        for(unsigned step=0;step<128;++step) {
            unsigned p[16]{};
            unsigned mode=next()%4;
            ntr::exec(0x10,&mode,1);
            unsigned cmd=0x10+next()%13;
            if(cmd==0x10) p[0]=next()%4;
            else if(cmd==0x12) p[0]=next()%5;
            else if(cmd==0x13 || cmd==0x14) p[0]=next()%32;
            else if(cmd>=0x16 && cmd<=0x1a) {
                const unsigned columns=cmd==0x1a?3:4;
                const unsigned rows=cmd==0x16 || cmd==0x18?4:3;
                for(unsigned i=0;i<rows;++i)for(unsigned j=0;j<columns;++j)
                    p[i*columns+j]=(i==j?4096:0)+int(next()%257)-128;
            } else if(cmd==0x1b) { for(unsigned i=0;i<3;++i)p[i]=2048+next()%2049; }
            else if(cmd==0x1c) { for(unsigned i=0;i<3;++i)p[i]=int(next()%513)-256; }
            ntr::exec(cmd,p,16);
            ntr::g.raw_texcoord[0]=int(next()%4096)-2048;
            ntr::g.raw_texcoord[1]=int(next()%4096)-2048;
            // Warm and reuse both derived matrices after every possible command.
            for(unsigned repeat=0;repeat<3;++repeat) {
                const int x=int(next()%8193)-4096, y=int(next()%8193)-4096,z=int(next()%8193)-4096;
                const auto v=ntr::project(x,y,z);
                words(result,&v,sizeof v);
                for(unsigned shift:{12u,20u,24u}) {
                    float uv[2];ntr::generated_texcoord(x,y,z,repeat?0:4096,shift,repeat!=0,uv[0],uv[1]);
                    words(result,uv,sizeof uv);
                }
            }
            // All source matrices and stack entries must also stay identical.
            words(result,ntr::g.pos.m,sizeof ntr::g.pos);
            words(result,ntr::g.proj.m,sizeof ntr::g.proj);
            words(result,ntr::g.vec.m,sizeof ntr::g.vec);
            words(result,ntr::g.tex.m,sizeof ntr::g.tex);
            words(result,ntr::g.clip,sizeof ntr::g.clip);
            words(result,ntr::g.texture_fixed,sizeof ntr::g.texture_fixed);
            result.push_back(ntr::gx_state_hash());
        }
    }
    return result;
}
int main() {
    const auto baseline=run(false);const unsigned old_clip=clip_rebuilds,old_texture=texture_rebuilds;
    const auto candidate=run(true);
    if(baseline!=candidate) {
        for(size_t i=0;i<baseline.size();++i)if(baseline[i]!=candidate[i]) {
            std::printf("FAIL matrix output word %zu: %08x != %08x\n",i,baseline[i],candidate[i]);break;
        }
        return 1;
    }
    if(clip_rebuilds>=old_clip || texture_rebuilds>=old_texture)return 2;
    std::printf("PASS 32768 matrix transitions across all 4 modes, %zu exact output words; clip rebuilds %u -> %u, texture rebuilds %u -> %u\n",
        baseline.size(),old_clip,clip_rebuilds,old_texture,texture_rebuilds);
}
