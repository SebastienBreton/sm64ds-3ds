// Exact DS fixed12 clip product. Inputs/output are distinct 16-word spans.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
namespace ntr::clip_sparse {
inline int32_t result(uint64_t sum){const uint32_t bits=uint32_t(sum>>12);int32_t out;std::memcpy(&out,&bits,4);return out;}
// Each int32 product fits int64; addition wraps in uint64 exactly as the DS accumulator.
inline uint64_t product(int32_t a,int32_t b){return static_cast<uint64_t>(int64_t(a)*b);}
// Certificate removes only exact integer-zero terms; no float view is read.
inline bool certified(const int32_t* p){return (p[1]|p[2]|p[3]|p[4]|p[6]|p[7]|p[8]|p[9]|p[12]|p[13]|p[15])==0;}
inline void dense(const int32_t* a,const int32_t* p,int32_t* out){for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){uint64_t sum=0;for(unsigned k=0;k<4;++k)sum+=product(a[i*4+k],p[k*4+j]);out[i*4+j]=result(sum);}}
inline void sparse(const int32_t* a,const int32_t* p,int32_t* out){for(unsigned i=0;i<4;++i){const auto* row=a+i*4;out[i*4]=result(product(row[0],p[0]));out[i*4+1]=result(product(row[1],p[5]));out[i*4+2]=result(product(row[2],p[10])+product(row[3],p[14]));out[i*4+3]=result(product(row[2],p[11]));}}
// Consumer defaults install this option before the first frame/reset.
// Other ntr consumers retain the dense path unless they opt in explicitly.
inline bool enabled=false;
inline void configure(){static const bool value=[](){const auto* s=std::getenv("PLAYER_CLIP_SPARSE_PRODUCT");return s&&std::atoi(s)!=0;}();enabled=value;}
inline void multiply(const int32_t* a,const int32_t* p,int32_t* out){if(enabled&&certified(p))sparse(a,p,out);else dense(a,p,out);}
}
