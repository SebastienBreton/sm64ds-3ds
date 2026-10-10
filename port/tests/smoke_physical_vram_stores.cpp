// Standalone test of the production seams and original __real fallback bodies.
#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include "vram_store_fixtures.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#if defined(__3DS__) && !defined(SM64DS_VRAM_STORE_HOST)
#include <3ds.h>
#include "cpu_mode.h"
#endif

extern "C" {
#if defined(SM64DS_VRAM_STORE_HOST)
#define MultiStore16 __wrap_MultiStore16
#define MultiCopyHalf __wrap_MultiCopyHalf
#define MultiStore32Bytes __wrap_MultiStore32Bytes
#endif
void MultiStore16(unsigned short,char*,int);
void MultiCopyHalf(const void*,void*,int);
void MultiStore32Bytes(unsigned,int*,int);
uint32_t __real__ZN3ntr13vram_cpu_readEmj(uint32_t,unsigned);
void __real__ZN3ntr14vram_cpu_writeEmmj(uint32_t,uint32_t,unsigned);
}

namespace ntr {
uint8_t* g_region[8]{};bool g_region_identity[8]{};
bool io_ready(){return true;}
uint64_t io_read(uint32_t,unsigned){std::puts("FATAL: unexpected IO read");std::abort();}
void io_write(uint32_t,uint64_t,unsigned){std::puts("FATAL: unexpected IO write");std::abort();}
#if defined(SM64DS_VRAM_STORE_HOST)
unsigned vram_cpu_read_real(unsigned,unsigned);
void vram_cpu_write_real(unsigned,unsigned,unsigned);
#endif
}

struct Trace {unsigned reads=0,writes=0,events=0,accesses=0;std::uint64_t read_hash=0,write_hash=0,event_hash=0,access_hash=0;} trace;
static std::uint64_t mix(std::uint64_t h,unsigned value){return h*1099511628211ull^value;}
static unsigned observed_read(unsigned address,unsigned width){
#if defined(SM64DS_VRAM_STORE_HOST)
 const auto value=ntr::vram_cpu_read_real(address,width);
#else
 const auto value=__real__ZN3ntr13vram_cpu_readEmj(address,width);
#endif
 ++trace.reads;trace.read_hash=mix(mix(mix(trace.read_hash,address&~(width-1)),width),value);
 ++trace.accesses;trace.access_hash=mix(mix(mix(mix(trace.access_hash,0),address&~(width-1)),width),value);
 return value;
}
static void observed_write(unsigned address,unsigned value,unsigned width){
 ++trace.writes;trace.write_hash=mix(mix(mix(trace.write_hash,address&~(width-1)),width),value);
 ++trace.accesses;trace.access_hash=mix(mix(mix(mix(trace.access_hash,1),address&~(width-1)),width),value);
#if defined(SM64DS_VRAM_STORE_HOST)
 ntr::vram_cpu_write_real(address,value,width);
#else
 __real__ZN3ntr14vram_cpu_writeEmmj(address,value,width);
#endif
}
#if defined(SM64DS_VRAM_STORE_HOST)
namespace ntr {
unsigned vram_cpu_read(unsigned address,unsigned width){return observed_read(address,width);}
void vram_cpu_write(unsigned address,unsigned value,unsigned width){observed_write(address,value,width);}
}
#else
extern "C" uint32_t __wrap__ZN3ntr13vram_cpu_readEmj(uint32_t address,unsigned width){return observed_read(address,width);}
extern "C" void __wrap__ZN3ntr14vram_cpu_writeEmmj(uint32_t address,uint32_t value,unsigned width){observed_write(address,value,width);}
#endif
static void bank_written(void* context,unsigned bank,unsigned offset,unsigned width){
 auto* bus=static_cast<ntr::VramBanks*>(context);unsigned value=0;
 for(unsigned i=0;i<width;++i)value|=unsigned(bus->physical(bank)[offset+i])<<(8*i);
 ++trace.events;trace.event_hash=mix(mix(mix(mix(trace.event_hash,bank),offset),width),value);
}
static unsigned char seed(unsigned bank,unsigned offset){return ((offset*53+(offset>>(bank%5+1))*19+bank*41)^0xa5)&255;}
static unsigned char flat_seed(unsigned region,unsigned offset){
 const unsigned value=offset*(region==0?53:region==1?17:23)+(offset>>(region==0?2:3))*19;
 return (value^(region==0?0xc3:region==1?0x6b:0x51))&255;
}

static void* fixed(unsigned address,unsigned bytes){
#if defined(__3DS__) && !defined(SM64DS_VRAM_STORE_HOST)
 auto* heap=std::aligned_alloc(0x1000,bytes);
 if(!heap)return nullptr;
 u32 mapped=0;Handle self=0;
 const auto permissions=static_cast<MemPerm>(MEMPERM_READ|MEMPERM_WRITE);
 const Result first=svcControlMemory(&mapped,address,reinterpret_cast<unsigned>(heap),bytes,MEMOP_MAP,permissions);
 Result second=0;
 if(R_FAILED(first)){
  if(R_FAILED(svcDuplicateHandle(&self,CUR_PROCESS_HANDLE)))return nullptr;
  second=svcControlProcessMemory(self,address,reinterpret_cast<unsigned>(heap),bytes,MEMOP_MAP,permissions);
  svcCloseHandle(self);
  if(R_FAILED(second)){
   std::printf("FATAL: fixed probe mirror %08x unavailable %08lx/%08lx\n",address,(unsigned long)first,(unsigned long)second);
   return nullptr;
  }
 }
 return reinterpret_cast<void*>(address);
#else
 (void)address;return std::malloc(bytes);
#endif
}

static int verify(){
 ntr::VramBanks bus;
 unsigned char* banks[9];unsigned char* expected[12];unsigned char* initial[12];
 unsigned sizes[12];
 for(unsigned b=0;b<9;++b){sizes[b]=ntr::VramBanks::bank_size(b);banks[b]=static_cast<unsigned char*>(std::malloc(sizes[b]));expected[b]=static_cast<unsigned char*>(std::malloc(sizes[b]));}
 const unsigned addresses[]={0x03000000,0x06000000,0x07000000};
 const unsigned flat_sizes[]={0x10000,0x2000,0x2000};
 unsigned char* mirrors[3];
 for(unsigned r=0;r<3;++r){
  sizes[9+r]=flat_sizes[r];mirrors[r]=static_cast<unsigned char*>(fixed(addresses[r],flat_sizes[r]));
  expected[9+r]=static_cast<unsigned char*>(std::malloc(flat_sizes[r]));
  if(!mirrors[r]){std::puts("FATAL: mirror allocation");return 1;}
  ntr::g_region[addresses[r]>>24]=mirrors[r];
#if defined(SM64DS_VRAM_STORE_HOST)
  ntr::g_region_identity[addresses[r]>>24]=false;
#else
  ntr::g_region_identity[addresses[r]>>24]=true;
#endif
 }
 for(unsigned b=0;b<9;++b)if(!banks[b]||!expected[b]){std::puts("FATAL: bank allocation");return 1;}
 for(unsigned r=0;r<3;++r)if(!expected[9+r]){std::puts("FATAL: expected allocation");return 1;}
 for(unsigned r=0;r<12;++r){initial[r]=static_cast<unsigned char*>(std::malloc(sizes[r]));if(!initial[r]){std::puts("FATAL: initial allocation");return 1;}for(unsigned i=0;i<sizes[r];++i)initial[r][i]=r<9?seed(r,i):flat_seed(r-9,i);}
 unsigned count=0,kinds[3]{};std::uint64_t total_reads=0,total_writes=0,total_events=0;
 for(const auto& c:store_cases){
  for(unsigned r=0;r<12;++r){std::memcpy(expected[r],initial[r],sizes[r]);std::memcpy(r<9?banks[r]:mirrors[r-9],initial[r],sizes[r]);}
  bus.reset(banks);bus.context=&bus;bus.written=bank_written;
  if(c.profile==0)bus.map(0,0x81);
  if(c.profile==1){bus.map(0,0x81);bus.map(1,0x81);}
  if(c.profile==2)bus.map(2,0x84);
  ntr::vram_attach_cpu_bus(c.profile==4?nullptr:&bus);
  for(unsigned p=c.patch_start;p<c.patch_start+c.patch_count;++p){
   const auto& patch=store_patches[p];
   if(patch.region>=12||patch.offset+patch.bytes>sizes[patch.region]){std::puts("FATAL: invalid golden patch");return 1;}
   std::memcpy(expected[patch.region]+patch.offset,store_blob+patch.blob_start,patch.bytes);
  }
  trace={};
  void* source=reinterpret_cast<void*>(std::uintptr_t(c.source));
  void* destination=reinterpret_cast<void*>(std::uintptr_t(c.destination));
#if defined(SM64DS_VRAM_STORE_HOST)
  // Host-only ABI adaptation. ARM11 below uses the exact DS addresses and
  // original u32 pointer casts, with GNU --wrap and real mapped mirrors.
  const bool physical=c.profile!=4&&(ntr::vram_cpu_address(c.destination)||(c.kind==1&&ntr::vram_cpu_address(c.source)));
  if(!physical){source=ntr::host_ptr(c.source);destination=ntr::host_ptr(c.destination);}
#endif
  if(c.kind==0)MultiStore16(c.value,static_cast<char*>(destination),c.bytes);
  else if(c.kind==1)MultiCopyHalf(source,destination,c.bytes);
  else if(c.kind==2)MultiStore32Bytes(c.value,static_cast<int*>(destination),c.bytes);
  else{std::puts("FATAL: unknown fixture kind");return 1;}
  if(trace.reads!=c.reads||trace.read_hash!=c.read_hash||trace.writes!=c.writes||trace.write_hash!=c.write_hash||trace.events!=c.events||trace.event_hash!=c.event_hash||trace.accesses!=c.accesses||trace.access_hash!=c.access_hash){
   std::printf("FATAL: fixture%u kind%u profile%u src%08x dst%08x bytes%d ordered trace mismatch reads%u/%u writes%u/%u events%u/%u\n",count,c.kind,c.profile,c.source,c.destination,c.bytes,trace.reads,c.reads,trace.writes,c.writes,trace.events,c.events);return 1;
  }
  for(unsigned r=0;r<12;++r){
   const auto* actual=r<9?banks[r]:mirrors[r-9];
   if(std::memcmp(actual,expected[r],sizes[r])){
    unsigned i=0;while(i<sizes[r]&&actual[i]==expected[r][i])++i;
    std::printf("FATAL: fixture%u kind%u profile%u src%08x dst%08x bytes%d region%u offset%x actual%u expected%u\n",count,c.kind,c.profile,c.source,c.destination,c.bytes,r,i,actual[i],expected[r][i]);return 1;
   }
  }
  total_reads+=trace.reads;total_writes+=trace.writes;total_events+=trace.events;++kinds[c.kind];++count;
  if(count%500==0)std::printf("Progress fixtures=%u/%u\n",count,unsigned(sizeof store_cases/sizeof store_cases[0]));
 }
 ntr::vram_attach_cpu_bus(nullptr);
 for(unsigned b=0;b<9;++b)std::free(banks[b]);
 for(auto* p:expected)std::free(p);
 for(auto* p:initial)std::free(p);
#if defined(SM64DS_VRAM_STORE_HOST)
 for(auto* p:mirrors)std::free(p);
#endif
 std::printf("PASS fixtures=%u fill16=%u copy16=%u fill32=%u full_banks=9 full_mirrors=3 ordered_reads=%llu ordered_writes=%llu ordered_bank_events=%llu\n",count,kinds[0],kinds[1],kinds[2],(unsigned long long)total_reads,(unsigned long long)total_writes,(unsigned long long)total_events);
 return 0;
}

int main(){
#if defined(__3DS__) && !defined(SM64DS_VRAM_STORE_HOST)
 gfxInitDefault();std::freopen("sdmc:/sm64ds/log.txt","w",stdout);std::setvbuf(stdout,nullptr,_IONBF,0);
 if(auto* f=std::fopen("sdmc:/sm64ds/env.txt","r")){char line[512];while(std::fgets(line,sizeof line,f)){line[std::strcspn(line,"\r\n")]=0;if(auto* equals=std::strchr(line,'=')){*equals=0;setenv(line,equals+1,1);}}std::fclose(f);}
 sm64ds_configure_cpu_mode();
#endif
 const int result=verify();
#if defined(__3DS__) && !defined(SM64DS_VRAM_STORE_HOST)
 gfxExit();
#endif
 std::puts(result?"FAIL":"PASS");return result;
}
