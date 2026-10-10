#include "native_blit.h"
#include "native_readback.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __3DS__
#include <3ds.h>
#include "cpu_mode.h"
#endif

static std::array<uint16_t,256*192> indices;
static void legacy(uint32_t* tiled,const uint32_t* image){
    for(unsigned i=0;i<indices.size();++i){
        const auto color=image[i];tiled[indices[i]]=(color<<8)|(color>>24);
    }
}
int main(){
#ifdef __3DS__
    gfxInitDefault();
    std::freopen("sdmc:/sm64ds/log.txt","w",stdout);
    std::setvbuf(stdout,nullptr,_IONBF,0);
    sm64ds_configure_cpu_mode();
#endif
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
        const unsigned py=255-y;
        unsigned morton=0;
        for(unsigned bit=0;bit<3;++bit)
            morton|=((x>>bit)&1)<<(2*bit)|((py>>bit)&1)<<(2*bit+1);
        indices[y*256+x]=((py>>3)*32+(x>>3))*64+morton;
    }
    static uint32_t input[256*192];
#ifdef __3DS__
    auto* before=static_cast<uint32_t*>(linearAlloc(256*256*4));
    auto* after=static_cast<uint32_t*>(linearAlloc(256*256*4));
    if(!before || !after){std::puts("FAIL");return 1;}
#else
    static uint32_t before[256*256],after[256*256];
#endif
    uint32_t random=0x734129;
    for(unsigned run=0;run<32;++run){
        for(auto& pixel:input){random^=random<<13;random^=random>>17;random^=random<<5;pixel=random;}
        std::memset(before,0xa5,256*256*4);std::memset(after,0xa5,256*256*4);
        legacy(before,input);upload_native_image(after,input);
        if(std::memcmp(before,after,256*256*4)){std::puts("FAIL: texture differs");return 1;}
    }
    if(decode_native_pixel(0)!=0){std::puts("FAIL: native clear is not transparent");return 1;}
    auto measure=[&](auto convert){
        const auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<3000;++frame){input[frame%indices.size()]^=frame;convert(after,input);}
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/3000;
    };
    const auto old_ms=measure(legacy),new_ms=measure(upload_native_image);
    // Ensure the compiler cannot discard the destination writes.
    uint32_t checksum=0;for(unsigned i=0;i<256*256;++i)checksum^=after[i];
    std::printf("PASS: 32 complete random textures and padding match, transparent clear verified\n");
    std::printf("Conversion ms/image: scanline=%.6f tiled=%.6f checksum=%08lx\n",old_ms,new_ms,static_cast<unsigned long>(checksum));
#ifdef __3DS__
    linearFree(before);linearFree(after);gfxExit();
#endif
    std::puts("PASS");
}
