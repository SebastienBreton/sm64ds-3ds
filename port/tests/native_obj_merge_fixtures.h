#pragma once
#include "native_obj_fixtures.h"
#include "ntr/ppu.h"

struct NativeObjBackground {
    std::array<uint8_t,0x20000> vram{};
    std::array<uint8_t,512> palette{};
    unsigned cnt[4]{},enabled=0,scroll[4]{};
    static uint32_t color(unsigned c){
        const unsigned r=(c&31)<<1,g=((c>>5)&31)<<1,b=((c>>10)&31)<<1;
        return 0xff000000u|((r<<2|r>>4)<<16)|((g<<2|g>>4)<<8)|(b<<2|b>>4);
    }
    void setup(unsigned id,ntr::PpuPreparedScene& scene){
        vram.fill(0);std::memset(&scene,0,sizeof scene);
        enabled=(id%11==0)?0:(15u^((id%5==0)?(1u<<(id%4)):0));
        for(unsigned p=0;p<256;++p)NativeObjFixture::put16(palette.data()+p*2,
            ((p*11+3)&31)|(((p*7+9)&31)<<5)|(((p*17+13)&31)<<10));
        for(unsigned layer=0;layer<4;++layer){
            cnt[layer]=((layer+id)%4)|(layer<<2)|((28+layer)<<8);
            scroll[layer]=(layer*5+(id&7))&255;
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;x+=2){
                auto index=[&](unsigned xx){return (xx+y+layer+id)%5==0?0:1+(xx*3+y*5+layer)%15;};
                vram[layer*0x4000+32+y*4+x/2]=index(x)|(index(x+1)<<4);
            }
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){
                const unsigned tile=(x*3+y+layer+id)%4==0?0:1;
                NativeObjFixture::put16(vram.data()+0xe000+layer*0x800+(y*32+x)*2,
                    tile|(((x+id)&1)<<10)|(((y+id)&1)<<11)|((layer+1)<<12));
            }
        }
        const unsigned modes[]={0,2,3},targets[]={0,16,63,5,32};
        scene.blend=(modes[(id/4)%3]<<6)|targets[(id/12)%5];
        scene.brightness=(id*13)%32;scene.master=(((id/17)%4)<<14)|((id*7)%32);
        const auto backdrop=color(unsigned(palette[0])|(unsigned(palette[1])<<8));
        for(unsigned i=0;i<256*192;++i){
            (&scene.front.px[0][0])[i]=backdrop;scene.front_layer[i]=5;scene.front_priority[i]=4;
            scene.window_mask[i]=63;
        }
        // Construct test inputs from the actual tile bytes. The independent
        // DS oracle verifies these BG-only RGB/layer/priority candidates first.
        for(int priority=3;priority>=0;--priority)for(int layer=3;layer>=0;--layer){
            if(!(enabled&(1u<<layer)) || (cnt[layer]&3)!=unsigned(priority))continue;
            for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
                const unsigned sx=(x+scroll[layer])&255;
                const unsigned offset=0xe000+layer*0x800+((y/8)*32+sx/8)*2;
                const unsigned tile=vram[offset]|(unsigned(vram[offset+1])<<8);
                const unsigned px=(tile&0x400)?7-(sx&7):sx&7,py=(tile&0x800)?7-(y&7):y&7;
                const auto raw=vram[layer*0x4000+(tile&1023)*32+py*4+px/2];
                const unsigned index=(raw>>((px&1)*4))&15;if(!index)continue;
                const unsigned pal=(tile>>12)*16+index,c=palette[pal*2]|(unsigned(palette[pal*2+1])<<8);
                const unsigned i=y*256+x;(&scene.front.px[0][0])[i]=color(c);
                scene.front_layer[i]=layer;scene.front_priority[i]=priority;
            }
        }
    }
};
