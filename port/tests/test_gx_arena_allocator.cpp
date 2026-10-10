// Native-game global new may route to a DS scene heap. This gate forbids it
// while exercising packet/cache allocation and on-demand reconstruction.
#include "platform/3ds/gx_geometry_arena.h"
#include <cstdio>
#include <cstdlib>
#include <new>
void* operator new(std::size_t){std::fputs("FAIL arena used global new\n",stderr);std::abort();}
void* operator new[](std::size_t){std::fputs("FAIL arena used global new[]\n",stderr);std::abort();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
int main(){
    using namespace ntr::gx_geometry;
    Packet packet;packet.reserve(16,8);Builder builder;builder.reserve(16);
    for(unsigned i=0;i<4;++i){ntr::GxVertex v{};v.w=v.cw=1;v.x=64+i*32;v.y=64+i*16;v.color=0xff556677;packet.vertex(v);}
    packet.triangle(0,1,2,{});packet.triangle(0,2,3,{});
    V3dsVert vertices[32]{};V3dsBatch batches[8]{};float depths[32]{};
    if(!builder.build(packet,{vertices,32,batches,8,depths,32,1}) || builder.vertex_count()!=6)return 1;
    ntr::HostVector<ntr::GxTriangle> fallback;packet.reconstruct(fallback);
    if(fallback.size()!=2)return 1;
    std::puts("PASS packet, IDs, conversion cache and reconstruction avoid global new/game heaps");
}
