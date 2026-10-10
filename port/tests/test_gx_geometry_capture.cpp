#include "ntr/gx_direct.h"
#include "platform/3ds/gx_geometry_arena.h"
#include "gx_geometry_capture_test_api.h"
#include "ntr/mmio.h"
#define GX_GEOMETRY_ARENA_REFERENCE_ONLY
#include "test_gx_geometry_arena.cpp"
#include <fstream>
#include <string>
namespace ntr {
uint8_t* g_region[8]{};bool g_region_identity[8]{};
bool io_ready(){return true;}
uint64_t io_read(uint32_t address,unsigned width){uint64_t value=0;std::memcpy(&value,host_ptr(address),width);return value;}
}
static std::uint64_t state_checks=0,words_checked=0,frames_checked=0,geometry_checked=0;
static unsigned cpu_frames=0,cpu_nonempty=0;
static std::vector<unsigned char> baseline_state,candidate_state;
static void state(){
    ntr::gx_test_state_baseline(baseline_state);ntr::gx_test_state_candidate(candidate_state);
    if(baseline_state!=candidate_state){
        std::size_t at=0;while(at<std::min(baseline_state.size(),candidate_state.size()) && baseline_state[at]==candidate_state[at])++at;
        std::fprintf(stderr,"state mismatch check=%llu word=%llu byte=%zu sizes=%zu,%zu\n",(unsigned long long)state_checks,(unsigned long long)words_checked,at,baseline_state.size(),candidate_state.size());
        std::exit(1);
    }
    ++state_checks;
}
static void reset(){ntr::gx_reset_baseline();ntr::gx_reset_candidate();state();}
static void frame(){ntr::gx_begin_frame_baseline();ntr::gx_begin_frame_candidate();state();}
static void port(unsigned cmd,uint32_t value){ntr::gx_write_port_baseline(0x04000400+cmd*4,value);ntr::gx_write_port_candidate(0x04000400+cmd*4,value);++words_checked;state();}
static void fifo(uint32_t word){ntr::gx_write_fifo_baseline(word);ntr::gx_write_fifo_candidate(word);++words_checked;state();}
static unsigned packed(int x,int y,int z){return(unsigned(x)&1023)|((unsigned(y)&1023)<<10)|((unsigned(z)&1023)<<20);}
static void cpu_fallback(){
    ntr::Framebuffer a{},b{};for(auto& row:a.px)for(auto& pixel:row)pixel=0x11223344;b=a;
    ntr::gx_render_baseline(a);ntr::gx_render_candidate(b);REQUIRE(std::memcmp(&a,&b,sizeof(a))==0);
    bool changed=false;for(const auto& row:a.px)for(auto pixel:row)changed|=pixel!=0x11223344;
    cpu_nonempty+=changed;++cpu_frames;hash(&a,sizeof(a));state();
}
static void geometry(Builder& builder,bool fallback=false){
    if(!ntr::gx_direct_enabled()){
        std::size_t na=0,nb=0;const auto* a=ntr::gx_polygons_baseline(na);const auto* b=ntr::gx_polygons_candidate(nb);REQUIRE(na==nb);
        for(std::size_t i=0;i<na;++i){REQUIRE(std::memcmp(a[i].v,b[i].v,sizeof(a[i].v))==0);
            REQUIRE(a[i].tw==b[i].tw && a[i].th==b[i].th && a[i].wrap==b[i].wrap && a[i].polygon_attr==b[i].polygon_attr && a[i].texture_version==b[i].texture_version);
            REQUIRE(bool(a[i].tex)==bool(b[i].tex));if(a[i].tex)REQUIRE(std::memcmp(a[i].tex,b[i].tex,std::size_t(a[i].tw)*a[i].th*4)==0);}
        ++frames_checked;geometry_checked+=na;state();return;
    }

    const auto& packet=ntr::gx_test_packet();std::vector<ntr::GxTriangle> expanded;packet.reconstruct(expanded);
    std::size_t count=0;const auto* reference_triangles=ntr::gx_polygons_baseline(count);
    REQUIRE(count==expanded.size());
    for(std::size_t i=0;i<count;++i){const auto& a=reference_triangles[i];const auto& b=expanded[i];
        REQUIRE(std::memcmp(a.v,b.v,sizeof(a.v))==0);
        REQUIRE(a.tw==b.tw && a.th==b.th && a.wrap==b.wrap && a.polygon_attr==b.polygon_attr && a.texture_version==b.texture_version);
        REQUIRE(bool(a.tex)==bool(b.tex));if(a.tex)REQUIRE(std::memcmp(a.tex,b.tex,std::size_t(a.tw)*a.th*4)==0);
    }
    if(fallback){std::size_t fallback_count=0;const auto* t=ntr::gx_polygons_candidate(fallback_count);REQUIRE(fallback_count==count);
        for(std::size_t i=0;i<count;++i)REQUIRE(std::memcmp(t[i].v,reference_triangles[i].v,sizeof(t[i].v))==0);
    }
    builder.reserve(packet.vertices().size());
    // Geometry/material bytes already equal above. Conversion reference keeps
    // candidate texture addresses so allocation identities are not compared.
    compare(const_cast<Packet&>(packet),builder,++frames_checked);
    geometry_checked+=count;state();
}
static void setup(unsigned variant){
    reset();frame();port(0x10,0);port(0x15,0);
    const int scale=variant%3==0?64:variant%3==1?4096:8192;
    const int projection[]={scale,0,0,0,0,scale,0,0,0,0,scale,0,0,0,0,4096};
    for(int value:projection)port(0x16,uint32_t(value));
    for(unsigned slot=0;slot<32;++slot){float m[16]{};m[0]=m[5]=m[10]=m[15]=1;
        m[12]=float(int((slot+variant)%7)-3)/8.f;m[13]=float(int(slot%5)-2)/16.f;
        ntr::gx_set_matrix_slot_baseline(slot,m);ntr::gx_set_matrix_slot_candidate(slot,m);
    }
    state();port(0x10,2);port(0x15,0);port(0x30,0x14637fff);port(0x31,0x04216b5a);
    port(0x32,packed(0,-512,-256));port(0x33,0x6e9f);port(0x29,0x001f00c1|(variant%64)<<24);
    port(0x60,variant%2?0xbfef0808:0xbfff0000);
    static uint32_t external[256]{};
    for(unsigned i=0;i<256;++i)external[i]=0xff000000|((i*7919)&0xffffff);
    ntr::gx_bind_texture_baseline(external,16,16);ntr::gx_bind_texture_candidate(external,16,16);state();
}
static uint32_t read_word(std::ifstream& input){uint32_t word=0;input.read(reinterpret_cast<char*>(&word),4);REQUIRE(bool(input));return word;}
int main(int argc,char** argv){
    REQUIRE(argc==3);ntr::gx_direct_configure(std::atoi(argv[2])!=0);for(unsigned i=2;i<8;++i){ntr::g_region[i]=static_cast<uint8_t*>(std::calloc(1,0x1000000));REQUIRE(ntr::g_region[i]);}
    Builder builder;std::ifstream fixture(argv[1],std::ios::binary);REQUIRE(bool(fixture));const auto lists=read_word(fixture);
    for(unsigned i=0;i<lists;++i){const auto words=read_word(fixture);setup(i);
        for(unsigned k=0;k<words;++k)fifo(read_word(fixture));geometry(builder,i%31==0);
    }
    REQUIRE(fixture.peek()==std::char_traits<char>::eof());
    // Primitive types, winding, latched attributes and changing UV dimensions.
    static uint32_t external[256]{};
    for(unsigned prim=0;prim<4;++prim){setup(prim);port(0x40,prim);
        for(unsigned v=0;v<17;++v){port(0x22,packed(int(v*3),int(v*7),0));port(0x21,packed(-512+int(v)*31,0,257));
            port(0x24,packed(int(v)*13-100,int(v)*17-120,int(v)*11-70));
            if(v%3==0){geometry(builder,true);frame();}
            if(v%5==0){ntr::gx_bind_texture_baseline(external,8,16);ntr::gx_bind_texture_candidate(external,8,16);state();}
        }
        port(0x41,0);geometry(builder,true);
    }
    // Packed and direct decoder partial parameters survive frame boundaries.
    setup(55);fifo(0x23151110);frame();fifo(2);frame();fifo(0xfffe0001);frame();fifo(0x000003ff);geometry(builder,true);
    port(0x40,2);port(0x23,0x00100010);frame();port(0x23,0xfffffff0);port(0x23,0x00200030);frame();port(0x23,0x00000020);
    port(0x23,0x00400050);port(0x23,0x00000030);geometry(builder,true);
    // External replay: no permanent expansion, still reconstructable on demand.
    std::size_t count=0;auto* t=ntr::gx_polygons_baseline(count);std::vector<ntr::GxTriangle> saved;if(count)saved.assign(t,t+count);
    REQUIRE(!saved.empty());frame();ntr::gx_append_polygons_baseline(saved.data(),saved.size());ntr::gx_append_polygons_candidate(saved.data(),saved.size());state();geometry(builder,true);
    // Actual encoded texture bytes and immutable identities: cache hit, texel
    // mutation, palette mutation, frame boundary and mode changes. No sampling.
    for(unsigned fmt=1;fmt<=7;++fmt){setup(fmt);auto* tex=static_cast<uint8_t*>(ntr::host_ptr(0x06800000));auto* pal=static_cast<uint8_t*>(ntr::host_ptr(0x06880000));
        for(unsigned i=0;i<128;++i){tex[i]=uint8_t(i*19+fmt);pal[i]=uint8_t(i*7+fmt);}
        std::memset(tex+0x20000,0,64);
        const uint32_t parameter=(fmt<<26)|0x00030000;
        for(unsigned step=0;step<8;++step){if(step==2)tex[0]^=3;if(step==4)pal[0]^=31;
            if(step==5)frame();port(0x2a,parameter|((step%4)<<30));port(0x2b,0);port(0x40,1);
            port(0x22,0x00010020);port(0x21,packed(511,-511,1));
            const int corners[4][2]={{-16,-16},{16,-16},{16,16},{-16,16}};
            for(const auto& v:corners)port(0x24,packed(v[0],v[1],0));port(0x41,0);geometry(builder,true);
            if(step==7)cpu_fallback();
        }
    }
    for(unsigned i=2;i<8;++i){std::free(ntr::g_region[i]);ntr::g_region[i]=nullptr;}
    REQUIRE(cpu_nonempty>0);
    std::printf("PASS indexed GX retail_lists=%u input_words=%llu state_checks=%llu frames=%llu triangles=%llu output_vertices=%llu conversions=%llu reuses=%llu clipped=%llu checksum=%llu cpu_frames=%u cpu_nonempty=%u; state/resource bytes, winding, frame carry, partial decoders and fallback exact\n",
        lists,(unsigned long long)words_checked,(unsigned long long)state_checks,(unsigned long long)frames_checked,(unsigned long long)geometry_checked,(unsigned long long)vertices_checked,(unsigned long long)conversions,(unsigned long long)reuses,(unsigned long long)clipped,(unsigned long long)checksum,cpu_frames,cpu_nonempty);
}
