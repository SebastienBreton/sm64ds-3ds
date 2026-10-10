// Exercise the original dual-buffer OAM reset with native contiguous aliases.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "ntr/mmio.h"
namespace OAM { void Reset(); }
extern "C" {
extern unsigned char data_0209e660;
extern unsigned data_0209e664,data_0209e668,data_0209e66c,data_0209e670;
extern unsigned data_0209e674[],data_0209e67c[],data_0209e694[],data_0209ea74[];
}
int main() {
    if (!ntr::io_init()) return 2;
    int failures=0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAIL: %s\n",#x); } } while(0)
    CHECK(data_0209e660==0);
    CHECK(reinterpret_cast<uintptr_t>(data_0209e67c)-reinterpret_cast<uintptr_t>(data_0209e674)==8);
    CHECK(reinterpret_cast<uintptr_t>(data_0209e694)-reinterpret_cast<uintptr_t>(data_0209e674)==32);
    CHECK(reinterpret_cast<uintptr_t>(data_0209ea74)-reinterpret_cast<uintptr_t>(data_0209e674)==1024);
    std::memset(data_0209e674,0x5a,1024);
    std::memset(data_0209ea74,0xa5,1024);
    data_0209e664=11;data_0209e668=22;data_0209e66c=33;data_0209e670=44;
    OAM::Reset();
    for (int i=0;i<128;++i) {
        CHECK(data_0209e674[2*i]==0xc0);
        CHECK(data_0209e674[2*i+1]==0x5a5a0000);
    }
    CHECK(std::memcmp(data_0209e674,data_0209ea74,1024)==0);
    CHECK((data_0209e664|data_0209e668|data_0209e66c|data_0209e670)==0);
    // The separate single-screen mode clears main attrs but preserves sub.
    data_0209e660=1;
    std::memset(data_0209e674,0x3c,1024);
    std::memset(data_0209ea74,0xa5,1024);
    OAM::Reset();
    for(int i=0;i<128;++i) {
        CHECK(data_0209e674[2*i]==0xc0);
        CHECK(data_0209e674[2*i+1]==0x3c3c0000);
        CHECK(data_0209ea74[2*i]==0xa5a5a5a5);
        CHECK(data_0209ea74[2*i+1]==0xa5a5a5a5);
    }
    std::printf("smoke_oam_reset: %s failures=%d dual-buffer128 single-buffer128 aliases=8/32/1024\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
