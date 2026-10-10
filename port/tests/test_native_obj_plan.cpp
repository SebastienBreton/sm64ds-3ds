#include "NDS.h"
#define private public
#include "GPU2D_Soft.h"
#undef private
#include "native_obj_fixtures.h"
#include <memory>
#include <cstdio>
#include <fstream>
#include <cstring>
#include <filesystem>

int main(int argc,char**argv){
    auto ds=std::make_unique<melonDS::NDS>();ds->Reset();
    auto& renderer=static_cast<melonDS::GPU2D::SoftRenderer&>(ds->GPU.GetRenderer2D());
    auto fixture=std::make_unique<NativeObjFixture>();auto plan=std::make_unique<ds_gpu::ObjPlan>();
    ntr::HostVector<uint32_t> expected(256*192),actual(256*192),pixels;
    std::ofstream golden,packed;
    if(argc==2){std::filesystem::create_directories(argv[1]);golden.open(std::string(argv[1])+"/expected.bin",std::ios::binary);packed.open(std::string(argv[1])+"/expected-packed.bin",std::ios::binary);}
    uint64_t comparisons=0;
    for(unsigned id=0;id<128;++id){
        fixture->setup(id%64);
        if(id>=64)for(unsigned i=0;i<128;++i)
            if(fixture->oam[i*8+1]&0x20)fixture->oam[i*8+4]^=1;
        ds->ARM9Write16(0x04000304,0x820f);
        for(unsigned b=0;b<9;++b){
            std::memcpy(ds->GPU.VRAM[b],fixture->physical[b].data(),fixture->physical[b].size());
            std::memset(ds->GPU.VRAMDirty[b].Data,255,sizeof ds->GPU.VRAMDirty[b].Data);
            const unsigned c=fixture->banks.control(b);
            if(b<2)ds->GPU.MapVRAM_AB(b,c);else if(b<4)ds->GPU.MapVRAM_CD(b,c);
            else if(b==4)ds->GPU.MapVRAM_E(b,c);else if(b<7)ds->GPU.MapVRAM_FG(b,c);
            else if(b==7)ds->GPU.MapVRAM_H(b,c);else ds->GPU.MapVRAM_I(b,c);
        }
        std::memcpy(ds->GPU.OAM+fixture->engine*0x400,fixture->oam.data(),1024);
        std::memcpy(ds->GPU.Palette+0x200+fixture->engine*0x400,fixture->palette.data(),512);
        ds->ARM9Write32(0x04000000+fixture->engine*0x1000,fixture->input.display);
        auto& unit=fixture->engine?ds->GPU.GPU2D_B:ds->GPU.GPU2D_A;
        unit.SetEnabled(true);std::fill(expected.begin(),expected.end(),0);
        for(unsigned y=0;y<192;++y){
            renderer.DrawSprites(y,&unit);
            for(unsigned x=0;x<256;++x){
                const auto raw=renderer.OBJLine[fixture->engine][x];
                if(!(raw&0x40000))continue;
                if((raw&0xff000000)!=0x10000000 || !(raw&0x1000)){std::puts("FAIL unexpected DS OBJ attributes");return 1;}
                const unsigned index=raw&255;
                const auto* p=fixture->palette.data()+index*2;
                const unsigned color=p[0]|(unsigned(p[1])<<8);
                const unsigned r=(color&31)<<1,g=((color>>5)&31)<<1,b=((color>>10)&31)<<1;
                expected[y*256+x]=(((raw>>16)&3)+1)<<24|((r<<2|r>>4)<<16)|((g<<2|g>>4)<<8)|(b<<2|b>>4);
            }
        }
        if(!plan->prepare(fixture->input)){std::printf("FAIL eligible fixture%u fallback%u\n",id,unsigned(plan->fallback));return 1;}
        std::fill(actual.begin(),actual.end(),0);
        for(unsigned s=0;s<plan->count;++s){
            const auto& obj=plan->sprites[plan->order[s]];ds_gpu::decode_obj_texture(obj,pixels);
            for(unsigned y=0;y<obj.height;++y)for(unsigned x=0;x<obj.width;++x){
                const int dx=obj.x+x,dy=obj.y+y;
                if(dx<0 || dx>=256 || dy<0 || dy>=192)continue;
                const auto c=pixels[(obj.vflip?obj.height-1-y:y)*obj.width+(obj.hflip?obj.width-1-x:x)];
                if(c>>24)actual[dy*256+dx]=(c&0xffffff)|((obj.priority+1)<<24);
            }
        }
        for(unsigned i=0;i<256*192;++i){++comparisons;if(actual[i]!=expected[i]){
            std::printf("FAIL fixture%u xy%u/%u native%08x DS%08x\n",id,i%256,i/256,actual[i],expected[i]);return 1;
        }}
        if(golden)golden.write(reinterpret_cast<const char*>(expected.data()),expected.size()*4);
        if(packed){
            std::array<uint32_t,256> dictionary{};std::array<uint8_t,256*192> indices{};
            unsigned used=1;
            for(unsigned i=0;i<expected.size();++i){
                auto* found=std::find(dictionary.data(),dictionary.data()+used,expected[i]);
                if(found==dictionary.data()+used){if(used==256){std::puts("FAIL golden dictionary too large");return 1;}dictionary[used++]=expected[i];}
                indices[i]=uint8_t(found-dictionary.data());
            }
            packed.write(reinterpret_cast<const char*>(dictionary.data()),sizeof dictionary);
            packed.write(reinterpret_cast<const char*>(indices.data()),sizeof indices);
        }
    }
    fixture->setup(0);
    NativeObjFixture::put16(fixture->oam.data(),40);
    NativeObjFixture::put16(fixture->oam.data()+2,40);
    // A cross term distinguishes unsupported rotation from the admitted
    // diagonal affine class; ordinary sprites never consume this matrix.
    NativeObjFixture::put16(fixture->oam.data()+14,1);
    auto reject=[&](unsigned bits,bool display,ds_gpu::ObjFallback why){
        const auto saved=fixture->input.display;const auto byte=fixture->oam[1];
        if(display)fixture->input.display|=bits;else fixture->oam[1]|=bits;
        const bool okay=!plan->prepare(fixture->input) && plan->fallback==why;
        if(!okay)std::printf("guard expected%u got%u\n",unsigned(why),unsigned(plan->fallback));
        fixture->input.display=saved;fixture->oam[1]=byte;return okay;
    };
    if(!reject(0xe000,true,ds_gpu::ObjFallback::Windows)||!reject(1,false,ds_gpu::ObjFallback::Affine)||
       !reject(0x10,false,ds_gpu::ObjFallback::Mosaic)||!reject(4,false,ds_gpu::ObjFallback::Alpha)||
       !reject(12,false,ds_gpu::ObjFallback::Bitmap)||!reject(0xc0,false,ds_gpu::ObjFallback::Shape)||
       !reject(0x80000000u,true,ds_gpu::ObjFallback::ExtendedPalette)){
        std::puts("FAIL strict fallback guard");return 1;
    }
    std::printf("PASS128 independent DS whole-image OBJ RGB/priority/opaque-attributes fixtures, %llu comparisons; even/odd8bpp tiles, tile/palette/flip/position changes and physical bank remaps, strict unsupported-case guards\n",(unsigned long long)comparisons);
}
