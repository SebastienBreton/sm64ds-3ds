#include "platform/3ds/gx_geometry_resources.h"
#include <cstdio>
#include <cstdlib>
#include <array>
#include <new>
void* operator new(std::size_t){std::abort();}void* operator new[](std::size_t){std::abort();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL resources line=%d %s\n",__LINE__,#c);return 1;}}while(0)
int main(){using namespace ntr::gx_geometry;
    std::array<std::uint32_t,64> external{},immutable{};for(unsigned i=0;i<64;++i){external[i]=i*73;immutable[i]=i*91;}
    Resources resources(512);Packet packet;for(unsigned i=0;i<3;++i)packet.vertex({});
    packet.triangle(0,1,2,{external.data(),8,8,3,0x1f00c0,0});packet.triangle(0,1,2,{immutable.data(),8,8,3,0x1f00c0,1});
    CHECK(!resources.matches(packet));
    V3dsBatch batches[3]={{external.data(),8,8,3,0,3,false,0x1f00c0,0},
        {external.data(),8,8,15,3,3,false,0x0f00c0,0},{immutable.data(),8,8,3,6,3,false,0x1f00c0,1}};
    CHECK(resources.freeze(batches,3));CHECK(resources.bytes()==512);CHECK(resources.matches(packet));CHECK(resources.external_unchanged());
    CHECK(batches[0].tex==batches[1].tex && batches[0].tex!=external.data());
    CHECK(std::memcmp(batches[0].tex,external.data(),256)==0 && std::memcmp(batches[2].tex,immutable.data(),256)==0);
    CHECK(!resources.freeze(batches,3));CHECK(resources.matches(packet));
    const auto* stable=batches[0].tex;external[63]^=1;CHECK(!resources.matches(packet));CHECK(!resources.external_unchanged());CHECK(stable[63]!=external[63]);
    V3dsBatch refreshed[3]={{external.data(),8,8,3,0,3,false,0x1f00c0,0},
        {external.data(),8,8,15,3,3,false,0x0f00c0,0},{immutable.data(),8,8,3,6,3,false,0x1f00c0,1}};
    CHECK(resources.freeze(refreshed,3));CHECK(resources.matches(packet));CHECK(refreshed[0].tex==stable);
    CHECK(std::memcmp(stable,external.data(),256)==0);
    // Bounds fail before rewriting any caller batch. No truncation.
    V3dsBatch bad{external.data(),-1,8,3,0,3,false,0x1f00c0,0};CHECK(!resources.freeze(&bad,1));CHECK(bad.tex==external.data());
    Resources small(255);V3dsBatch over{external.data(),8,8,3,0,3,false,0x1f00c0,0};CHECK(!small.freeze(&over,1));CHECK(over.tex==external.data());
    // Identity/version alias changes invalidate external reuse. An immutable
    // decoded version keeps its owned snapshot even after source retirement.
    packet.clear();packet.triangle(0,0,0,{immutable.data(),8,8,3,0x1f00c0,2});CHECK(!resources.matches(packet));
    packet.clear();packet.triangle(0,0,0,{immutable.data(),8,8,3,0x1f00c0,1});immutable.fill(0);CHECK(resources.matches(packet));CHECK(batches[2].tex[1]==91);
    packet.clear();CHECK(resources.matches(packet));CHECK(resources.freeze(nullptr,0));CHECK(resources.bytes()==0);
    std::puts("PASS exact external byte invalidation, immutable snapshot lifetime, bounded resources, stable pointers and no global new");
}
