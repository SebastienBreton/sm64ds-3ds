// Isolated correctness and native microbenchmark, with production unchanged.
#include "ntr/fixed_direction_matrix.h"
#include "fixed_direction_bounded_candidate.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#ifdef __3DS__
#include <3ds.h>
#include "cpu_mode.h"
#endif

static unsigned random_state=0x685419ad;
static unsigned next_random(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static std::uint64_t compared,checksum,matrix_checksum;
static std::uint32_t neighbour_bits(float value,int direction){
 std::uint32_t bits;std::memcpy(&bits,&value,4);
 if(!direction)return bits;
 if(!(bits&0x7fffffffu))return direction<0?0x80000001u:1u;
 return bits+((bits&0x80000000u)?(direction<0?1u:0xffffffffu):(direction<0?0xffffffffu:1u));
}
__attribute__((always_inline)) static inline void current(ntr::FixedDirectionMatrix& cache,const float* matrix,unsigned packed,int* out,bool negate){
 const auto* fixed=cache.coefficients(matrix);int raw[3];
 for(int k=0;k<3;++k){raw[k]=(packed>>(10*k))&1023;if(raw[k]>=512)raw[k]-=1024;}
 for(int j=0;j<3;++j){std::int64_t sum=0;for(int k=0;k<3;++k)sum+=raw[k]*fixed[k*3+j];
  const int value=int(sum>>12);const unsigned bits=unsigned(negate?-value:value)&2047u;out[j]=bits>=1024?int(bits)-2048:int(bits);}
}
static ntr::FixedDirectionMatrix reference;
static FixedDirectionBoundedCandidate<true> candidate;
#if defined(NTR_TEST_INTEGRATED_DIRECTION)
static ntr::BoundedFixedDirectionMatrix integrated;
#endif
static float prior[16]{};static bool prior_valid;
static std::uint64_t expected_hits,expected_misses,expected_bounded,expected_fallback;
static void check(const float* matrix,unsigned packed,bool negate){
 for(unsigned i=0;i<16;++i){std::uint32_t bits;std::memcpy(&bits,matrix+i,4);matrix_checksum=(matrix_checksum*1099511628211ull)^bits;}
 bool bounded=true;for(int k=0;k<3;++k)for(int j=0;j<3;++j){const float scaled=matrix[k*4+j]*4096.f;bounded&=scaled>=-2147483648.f&&scaled<2147483648.f;}
 if(prior_valid&&!std::memcmp(prior,matrix,sizeof prior))++expected_hits;
 else{++expected_misses;bounded?++expected_bounded:++expected_fallback;std::memcpy(prior,matrix,sizeof prior);prior_valid=true;}
 int a[3],b[3];current(reference,matrix,packed,a,negate);candidate.transform(matrix,packed,b,negate);
 if(std::memcmp(a,b,sizeof a)||candidate.is_bounded()!=bounded){std::puts("FAIL integer directional vector or eligibility");std::abort();}
 const auto* coefficients=reference.coefficients(matrix);
 for(int i=0;i<9;++i){const auto value=bounded?std::int64_t(candidate.narrow_coefficients()[i]):candidate.wide_coefficients()[i];if(value!=coefficients[i]){std::puts("FAIL coefficient");std::abort();}}
#if defined(NTR_TEST_INTEGRATED_DIRECTION)
 int actual[3];integrated.transform(matrix,packed,actual,negate);
 if(std::memcmp(a,actual,sizeof a)||integrated.is_bounded()!=bounded){std::puts("FAIL integrated production vector or eligibility");std::abort();}
 for(int i=0;i<9;++i){const auto value=bounded?std::int64_t(integrated.narrow_coefficients()[i]):integrated.wide_coefficients()[i];if(value!=coefficients[i]){std::puts("FAIL integrated production coefficient");std::abort();}}
#endif
 ++compared;checksum=(checksum*1099511628211ull)^unsigned(a[0]+2048)^((unsigned(a[1]+2048))<<12)^((std::uint64_t(unsigned(a[2]+2048)))<<24);
}
static void correctness(){
 const float edges[]={0.f,-0.f,.5f/4096,-.5f/4096,1.5f/4096,-1.5f/4096,
  524287.9375f,-524288.f,524288.f,-524288.0625f,1048576.f,-1048576.f};
 for(float e:edges)for(int direction=-1;direction<=1;++direction)for(unsigned index=0;index<16;++index){
  float matrix[16]{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1.f;const auto bits=neighbour_bits(e,direction);std::memcpy(matrix+index,&bits,4);
  for(unsigned packed:{0u,0x1ff7fdffu,0x20080200u,0x1ffffe00u,0x3fffffffu})for(bool negate:{false,true}){check(matrix,packed,negate);check(matrix,packed,negate);}
 }
 // All signed10 source extrema combinations, alternating cache hits/misses.
 for(unsigned mode=0;mode<3;++mode)for(int x:{-512,-511,-1,0,1,510,511})for(int y:{-512,-511,-1,0,1,510,511})for(int z:{-512,-511,-1,0,1,510,511}){
  float matrix[16]{};
  for(int k=0;k<3;++k)for(int j=0;j<3;++j)matrix[k*4+j]=(k+j)&1?-524288.f:524287.96875f;
  if(mode==1)matrix[10]=524288.f;
  if(mode==2){std::memset(matrix,0,sizeof matrix);matrix[0]=matrix[10]=1048576.f;matrix[5]=-1048576.f;}
  const unsigned packed=(x&1023)|((y&1023)<<10)|((z&1023)<<20);check(matrix,packed,false);check(matrix,packed,true);
 }
 float matrix[16]{};
 for(unsigned batch=0;batch<2048;++batch){
  for(auto& v:matrix)v=(int(next_random()%0x100001)-0x80000)/65536.f;
  if(batch%7==0)matrix[(batch%3)*4+(batch/3)%3]=(batch&1)?524288.f:-524288.0625f;
  for(unsigned i=0;i<32;++i)check(matrix,next_random(),i&1);
 }
 // Cache compares all16 source words: signed zero, unused payload NaN and
 // infinity change the cache identity without changing the nine coefficients.
 for(unsigned bits:{0u,0x80000000u,0x7fc01234u,0x7f800000u,0xff800000u}){
  std::memcpy(matrix+15,&bits,4);check(matrix,0x2007fdff,false);check(matrix,0x2007fdff,true);
 }
 const auto s=candidate.stats;
 if(s.hits!=expected_hits||s.misses!=expected_misses||s.bounded_misses!=expected_bounded||s.fallback_misses!=expected_fallback){std::puts("FAIL cache counters");std::abort();}
 std::printf("PASS vectors=%llu checksum=%llu matrix_bits_checksum=%llu hits=%llu misses=%llu bounded_misses=%llu fallback_misses=%llu\n",(unsigned long long)compared,(unsigned long long)checksum,(unsigned long long)matrix_checksum,(unsigned long long)s.hits,(unsigned long long)s.misses,(unsigned long long)s.bounded_misses,(unsigned long long)s.fallback_misses);
}

static float matrices[32][16];static unsigned normals[256];static volatile std::uint64_t bench_sink;
__attribute__((noinline)) static void transform_current(ntr::FixedDirectionMatrix& c,const float* m,unsigned n,int* out,bool negate){current(c,m,n,out,negate);}
__attribute__((noinline)) static void transform_candidate(FixedDirectionBoundedCandidate<false>& c,const float* m,unsigned n,int* out,bool negate){c.transform(m,n,out,negate);}
template<class Cache,class Function> static double measure(Function function,unsigned share,std::uint64_t& result){
 Cache cache;std::uint64_t hash=0;const auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<60000;++i){int out[3];function(cache,matrices[(i/share)%32],normals[i%256],out,i&1);hash=(hash*1099511628211ull)^unsigned(out[0]+2048)^((unsigned(out[1]+2048))<<12)^(std::uint64_t(unsigned(out[2]+2048))<<24);}
 const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/60000;result=hash;bench_sink=hash;return elapsed;
}
static void benchmark(){
 for(auto& n:normals)n=next_random();
 for(unsigned fallback=0;fallback<2;++fallback){
  for(auto& m:matrices){for(float& v:m)v=(int(next_random()%0x100001)-0x80000)/65536.f;if(fallback)m[0]=524288.f;}
  for(unsigned i=0;i<32;++i)if(!std::memcmp(matrices[i],matrices[(i+31)%32],sizeof matrices[i])){std::puts("FAIL benchmark matrix identities");std::abort();}
  for(unsigned share:{1u,16u,256u}){
   double a=0,b=0;std::uint64_t x=0,y=0;
   for(unsigned round=0;round<4;++round){
    double current_ms,candidate_ms;
    if(round&1){candidate_ms=measure<FixedDirectionBoundedCandidate<false>>(transform_candidate,share,y);current_ms=measure<ntr::FixedDirectionMatrix>(transform_current,share,x);}
    else{current_ms=measure<ntr::FixedDirectionMatrix>(transform_current,share,x);candidate_ms=measure<FixedDirectionBoundedCandidate<false>>(transform_candidate,share,y);}
    a+=current_ms;b+=candidate_ms;
    if(x!=y){std::puts("FAIL benchmark checksum");std::abort();}
    std::printf("Sample workload=%s share=%u round=%u order=%s current_ms/vector=%.9f candidate_ms/vector=%.9f checksum=%llu\n",fallback?"legacy-fallback":"bounded",share,round,round&1?"BA":"AB",current_ms,candidate_ms,(unsigned long long)x);
   }
   const unsigned misses=(60000+share-1)/share;
   std::printf("Bounded direction microbench workload=%s share=%u vectors_per_round=60000 rounds=4 predicted_cache_hits=%u predicted_cache_misses=%u ms/vector current_cached64=%.9f candidate=%.9f checksum=%llu\n",fallback?"legacy-fallback":"bounded",share,60000-misses,misses,a/4,b/4,(unsigned long long)x);
  }
 }
}
int main(){
#ifdef __3DS__
 gfxInitDefault();std::freopen("sdmc:/sm64ds/log.txt","w",stdout);std::setvbuf(stdout,nullptr,_IONBF,0);
 // Standalone executable: load run.py settings once, before model validation.
 if(auto* f=std::fopen("sdmc:/sm64ds/env.txt","r")){char line[512];while(std::fgets(line,sizeof line,f)){line[std::strcspn(line,"\r\n")]=0;if(auto* e=std::strchr(line,'=')){*e=0;setenv(line,e+1,1);}}std::fclose(f);}
 sm64ds_configure_cpu_mode();
#endif
 correctness();
#if !defined(NTR_DIRECTION_CORRECTNESS_ONLY)
 benchmark();
#endif
#ifdef __3DS__
 gfxExit();
#endif
 std::puts("PASS isolated bounded matrix; production unchanged; no menu speed claim");
 std::puts("PASS");return 0;
}
