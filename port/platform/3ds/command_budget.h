#pragma once
#include <cstdint>
namespace command_budget {
constexpr uint32_t reserve_words=64;
constexpr uint64_t encoded_words(uint32_t parameters){
 const uint64_t n=parameters?parameters:1,full=n/256,tail=n%256;
 return full*258+(tail?tail+1+((tail&1)==0):0);
}
constexpr bool fits(uint32_t remaining,uint32_t offset,uint64_t needed){
 return uint64_t(offset)+needed+reserve_words<=remaining;
}
}
