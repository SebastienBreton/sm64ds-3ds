#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include "vram_transfer_fixtures.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

extern "C" {
void CpuCopy8(const void*,void*,unsigned);
void func_0205a588(void*,unsigned,unsigned);
void Copy32Bytes(const void*,void*);
void Copy36Bytes(int*,int*);
void Copy48BytesFixed(int*,int*);
void MultiCopy_Int(int*,int*,int);
void MultiCopy32Bytes(int*,int*,int);
void DecompressLZ16(void*,void*);
void MultiStore_Int(int,int*,int);
}

static unsigned char expected[2][0x20000];
alignas(4) static unsigned char source[sizeof transfer_source];

int main() {
    if (!ntr::io_init() || !ntr::io_vram_banks()) {
        std::puts("FATAL: physical bank bus was not explicitly enabled");
        return 1;
    }
    auto* bus=ntr::io_vram_banks();
    unsigned count=0;
    for (const auto& c:transfer_cases) {
        std::memcpy(source,transfer_source,sizeof source);
        for (unsigned bank=0;bank<2;++bank) {
            for(unsigned i=0;i<0x20000;++i) {
                const auto value=uint8_t(bank ? i*11+(i>>5) : i*7+(i>>3));
                bus->physical(bank)[i]=value;expected[bank][i]=value;
            }
            bus->changed(bank,0,0x20000);
        }
        ntr::io_write(0x04000240,0x81|((c.mask&2)?0x8100:0),2);
        for(unsigned i=c.start;i<c.start+c.count;++i) {
            const auto& delta=transfer_changes[i];expected[delta.bank][delta.offset]=delta.value;
        }
        void* src=c.from_vram ? reinterpret_cast<void*>(0x06000000+c.src) : source+c.src;
        void* dst=reinterpret_cast<void*>(0x06000000+c.dst);
        switch(c.kind) {
            case 0:CpuCopy8(src,dst,c.size);break;
            case 1:func_0205a588(dst,c.value,c.size);break;
            case 2:Copy32Bytes(src,dst);break;
            case 3:Copy36Bytes(static_cast<int*>(src),static_cast<int*>(dst));break;
            case 4:Copy48BytesFixed(static_cast<int*>(src),static_cast<int*>(dst));break;
            case 5:MultiCopy_Int(static_cast<int*>(src),static_cast<int*>(dst),c.size);break;
            case 6:MultiCopy32Bytes(static_cast<int*>(src),static_cast<int*>(dst),c.size);break;
            case 7:DecompressLZ16(src,dst);break;
            case 8:MultiStore_Int(c.value,static_cast<int*>(dst),c.size);break;
            default:std::puts("FATAL: unknown fixture");return 1;
        }
        for(unsigned bank=0;bank<2;++bank) {
            for(unsigned i=0;i<0x20000;++i)if(bus->physical(bank)[i]!=expected[bank][i]) {
                std::printf("FATAL: fixture=%u kind=%u bank=%u offset=%x actual=%u expected=%u\n",
                            count,c.kind,bank,i,bus->physical(bank)[i],expected[bank][i]);
                return 1;
            }
        }
        ++count;
    }
    std::printf("PASS: %u original ARM transfer fixtures; both entire physical banks exact\n",count);
    return 0;
}
