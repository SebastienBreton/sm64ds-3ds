#pragma once
#include "native_obj_plan.h"
#include "ntr/vram_banks.h"
#include <cstring>

struct NativeObjFixture {
    ntr::VramBanks banks;
    std::array<ntr::HostVector<uint8_t>,9> physical;
    std::array<uint8_t,1024> oam{};
    std::array<uint8_t,512> palette{};
    ntr::HostVector<uint8_t> view;
    ds_gpu::ObjInput input;
    unsigned engine=0;
    NativeObjFixture(){
        uint8_t* ptr[9];for(unsigned b=0;b<9;++b){physical[b].resize(banks.bank_size(b));ptr[b]=physical[b].data();}
        banks.reset(ptr);view.resize(0x40000);
    }
    static void put16(uint8_t* p,unsigned value){p[0]=value;p[1]=value>>8;}
    void setup(unsigned id){
        engine=id/32;
        for(unsigned b=0;b<9;++b){
            for(unsigned i=0;i<physical[b].size();++i)
                physical[b][i]=uint8_t((i*17+(i>>3)*43+b*71+(id/8)*29)^(i>>13));
            banks.changed(b,0,physical[b].size());
            unsigned control=0;
            if(!engine && b<2){
                const unsigned slot=(id&8)?0:(b^((id>>2)&1));
                control=(id&16) && b==1?0:0x82|(slot<<3);
            }
            if(engine && b==3)control=(id&8)?0:0x84;
            banks.map(b,control);
        }
        banks.copy_view(engine?ntr::VramView::BObj:ntr::VramView::AObj,0,view.data(),engine?0x20000:0x40000);
        for(unsigned p=0;p<256;++p)put16(palette.data()+p*2,((p*7+id/16)&31)|(((p*13+5)&31)<<5)|(((p*19+3)&31)<<10));
        for(unsigned i=0;i<128;++i){
            const unsigned shape=(i+id/8)%3,size=(i/3)%4;
            const unsigned x=(i%9==0)?504:((i*13+(id&7))%270+502)&511;
            const unsigned y=(i%7==0)?248:(i*17+(id&7))%208;
            put16(oam.data()+i*8,y|((i&1)?0x2000:0)|(shape<<14));
            put16(oam.data()+i*8+2,x|((id&1)?0x1000:0)|((id&2)?0x2000:0)|(size<<14));
            put16(oam.data()+i*8+4,((i*29+13)&1023)|((i&3)<<10)|((i&15)<<12));
            put16(oam.data()+i*8+6,0);
        }
        const unsigned display=(1u<<16)|(1u<<12)|((id&1)?16:0)|(((id>>1)&3)<<20);
        input={engine?display&0xc0b1fff7:display,oam.data(),view.data(),palette.data(),engine?0x1ffffu:0x3ffffu};
    }
};
