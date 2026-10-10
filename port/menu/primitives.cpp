// Integer primitives needed by the original retail menu. Semantics follow
// the supplied ROM, including the signed division helper's zero divisor rule.
#include <cstdint>
#include <limits>
extern "C" void Matrix3x3_LoadIdentity(std::int32_t* out) {
 for(unsigned i=0;i<9;++i)out[i]=(i%4==0)?4096:0;
}
extern "C" void func_02052ec8(std::int32_t* out) {
 for(unsigned i=0;i<16;++i)out[i]=(i%5==0)?4096:0;
}
extern "C" void MultiStore32Bytes(std::uint32_t value,std::uint32_t* out,int bytes) {
 for(int offset=0;offset<bytes;offset+=4)*out++=value;
}
extern "C" std::int64_t func_01ffaa34(std::int64_t value,std::int64_t divisor) {
 if(divisor==0)return value;
 // Retail first takes a signed-32 fast path when both inputs fit.
 if(value==std::numeric_limits<std::int32_t>::min() && divisor==-1)return value;
 if(value==std::numeric_limits<std::int64_t>::min() && divisor==-1)return value;
 return value/divisor;
}
