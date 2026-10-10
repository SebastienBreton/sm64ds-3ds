#include "NDS.h"
#define private public
#include "GPU2D_Soft.h"
#undef private
#include "native_obj_merge_fixtures.h"
#include <memory>
#include <fstream>
#include <filesystem>
#include <cstdio>

static uint32_t display(unsigned raw){
    const unsigned r=raw&63,g=(raw>>8)&63,b=(raw>>16)&63;
    return 0xff000000u|((r<<2|r>>4)<<16)|((g<<2|g>>4)<<8)|(b<<2|b>>4);
}
int main(int argc,char**argv){
    if(argc!=2)return 2;std::filesystem::create_directories(argv[1]);
    std::ofstream packed(std::string(argv[1])+"/merge-packed.bin",std::ios::binary);
    auto ds=std::make_unique<melonDS::NDS>();ds->Reset();
    auto obj=std::make_unique<NativeObjFixture>();auto bg=std::make_unique<NativeObjBackground>();
    auto scene=std::make_unique<ntr::PpuPreparedScene>();
    auto& renderer=static_cast<melonDS::GPU2D::SoftRenderer&>(ds->GPU.GetRenderer2D());
    std::array<ntr::HostVector<uint32_t>,2> frames;for(auto& f:frames)f.resize(256*192);
    renderer.SetFramebuffer(frames[0].data(),frames[1].data());
    uint64_t pixels=0;
    for(unsigned id=0;id<128;++id){
        obj->setup(id%64);if(id>=64)for(unsigned i=0;i<128;++i)if(obj->oam[i*8+1]&0x20)obj->oam[i*8+4]^=1;
        bg->setup(id,*scene);ds->ARM9Write16(0x04000304,0x820f);
        for(unsigned b=0;b<9;++b){
            const auto* bytes=b==2?bg->vram.data():obj->physical[b].data();
            std::memcpy(ds->GPU.VRAM[b],bytes,obj->physical[b].size());
            std::memset(ds->GPU.VRAMDirty[b].Data,255,sizeof ds->GPU.VRAMDirty[b].Data);
            const unsigned c=b==2?(obj->engine?0x84:0x81):obj->banks.control(b);
            if(b<2)ds->GPU.MapVRAM_AB(b,c);else if(b<4)ds->GPU.MapVRAM_CD(b,c);
            else if(b==4)ds->GPU.MapVRAM_E(b,c);else if(b<7)ds->GPU.MapVRAM_FG(b,c);
            else if(b==7)ds->GPU.MapVRAM_H(b,c);else ds->GPU.MapVRAM_I(b,c);
        }
        const unsigned base=0x04000000+obj->engine*0x1000;
        std::memcpy(ds->GPU.OAM+obj->engine*0x400,obj->oam.data(),1024);
        std::memcpy(ds->GPU.Palette+obj->engine*0x400,bg->palette.data(),512);
        std::memcpy(ds->GPU.Palette+obj->engine*0x400+512,obj->palette.data(),512);
        for(unsigned l=0;l<4;++l){ds->ARM9Write16(base+8+l*2,bg->cnt[l]);ds->ARM9Write16(base+16+l*4,bg->scroll[l]);ds->ARM9Write16(base+18+l*4,0);}
        const unsigned disp=obj->input.display|(bg->enabled<<8);
        ds->ARM9Write32(base,disp&~(1u<<12));ds->ARM9Write16(base+0x50,0);ds->ARM9Write16(base+0x6c,0);
        auto& unit=obj->engine?ds->GPU.GPU2D_B:ds->GPU.GPU2D_A;unit.SetEnabled(true);
        renderer.SetFramebuffer(frames[0].data(),frames[1].data());
        for(unsigned y=0;y<192;++y){
            ds->GPU.VCount=y;unit.CheckWindows(y);renderer.DrawSprites(y,&unit);renderer.DrawScanline(y,&unit);
            for(unsigned x=0;x<256;++x){
                const unsigned i=y*256+x,raw=renderer.BGOBJLine[x],flag=raw>>24;
                unsigned layer=5;for(unsigned l=0;l<4;++l)if(flag==(1u<<l))layer=l;
                const unsigned priority=layer==5?4:bg->cnt[layer]&3;
                if(display(raw)!=(&scene->front.px[0][0])[i] || layer!=scene->front_layer[i] || priority!=scene->front_priority[i]){
                    std::printf("FAIL BG-input id%u xy%u/%u RGB%08x/%08x layer%u/%u priority%u/%u\n",id,x,y,display(raw),(&scene->front.px[0][0])[i],layer,scene->front_layer[i],priority,scene->front_priority[i]);return 1;
                }++pixels;
            }
        }
        ds->ARM9Write32(base,disp);ds->ARM9Write16(base+0x50,scene->blend);ds->ARM9Write16(base+0x54,scene->brightness);ds->ARM9Write16(base+0x6c,scene->master);
        std::array<uint32_t,256> dictionary{};std::array<uint8_t,256*192> indices{};unsigned used=0;
        for(unsigned y=0;y<192;++y){
            ds->GPU.VCount=y;unit.CheckWindows(y);renderer.DrawSprites(y,&unit);renderer.DrawScanline(y,&unit);
            for(unsigned x=0;x<256;++x){
                const unsigned c=frames[obj->engine][y*256+x];
                const unsigned pos=y*256+x,raw=renderer.OBJLine[obj->engine][x];
                const bool visible=(raw&0x40000) && ((raw>>16)&3)<=scene->front_priority[pos];
                uint32_t chosen=(&scene->front.px[0][0])[pos];unsigned layer=scene->front_layer[pos];
                if(visible){const auto* p=obj->palette.data()+(raw&255)*2;chosen=NativeObjBackground::color(p[0]|(unsigned(p[1])<<8));layer=4;}
                const unsigned mode=(scene->blend>>6)&3,master=scene->master>>14;
                auto channel=[&](unsigned byte){
                    unsigned q=byte>>2;
                    if((scene->blend&(1u<<layer)) && (mode==2 || mode==3)){
                        const unsigned factor=std::min(unsigned(scene->brightness),16u);
                        q=mode==2?q+((63-q)*factor+8)/16:q-(q*factor+7)/16;
                    }
                    if(master==1 || master==2){
                        const unsigned factor=std::min(unsigned(scene->master&31),16u);
                        q=master==1?q+((63-q)*factor)/16:q-(q*factor+15)/16;
                    }return (q<<2)|(q>>4);
                };
                const unsigned prediction=0xff000000u|(channel((chosen>>16)&255)<<16)|(channel((chosen>>8)&255)<<8)|channel(chosen&255);
                if(prediction!=c){std::printf("FAIL merge-formula id%u xy%u/%u predicted%08x DS%08x\n",id,x,y,prediction,c);return 1;}
                auto* f=std::find(dictionary.data(),dictionary.data()+used,c);
                if(f==dictionary.data()+used){if(used==256){std::puts("FAIL dictionary overflow");return 1;}dictionary[used++]=c;}
                indices[y*256+x]=uint8_t(f-dictionary.data());
            }
        }
        packed.write(reinterpret_cast<const char*>(dictionary.data()),sizeof dictionary);
        packed.write(reinterpret_cast<const char*>(indices.data()),sizeof indices);
    }
    std::printf("PASS128 independent DS BG candidates RGB/layer/priority %llu pixels and full OBJ+BG priority/effects/master formula; golden generated\n",(unsigned long long)pixels);
}
