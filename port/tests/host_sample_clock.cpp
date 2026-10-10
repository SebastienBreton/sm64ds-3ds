#include "audio/sample_clock.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>

int main(){
    unsigned cases=0;
    for(unsigned rate=8000;rate<=48000;++rate){
        const Sm64dsSampleClock clock(rate);
        std::uint64_t actual=0,expected=0;
        for(unsigned frame=0;frame<32;++frame){
            // Exercise continuous carries as well as a frequency change's
            // leftover accumulator (which can exceed the new denominator).
            if(frame==16)actual=expected=96000;
            actual+=33513982;expected+=33513982;
            const unsigned cycles=expected/(2u*rate);
            expected%=2u*rate;
            assert(clock.reduce(actual)==cycles && actual==expected);
            ++cases;
        }
        for(auto value:{std::uint64_t(0),std::uint64_t(2u*rate-1),
                        std::uint64_t(2u*rate),std::uint64_t(2u*rate+1),
                        std::numeric_limits<std::uint64_t>::max()}){
            actual=value;
            assert(clock.reduce(actual)==static_cast<unsigned>(value/(2u*rate)));
            assert(actual==value%(2u*rate));++cases;
        }
    }
    std::printf("PASS %u exact audio clock reductions across all 8000..48000 Hz rates\n",cases);
}
