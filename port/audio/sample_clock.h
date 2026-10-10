#pragma once
#include <cstdint>

// Exact reduction of the ARM7-cycle accumulator at a fixed output rate.
// The caller validates rate != 0. Power-of-two rates avoid an ARM software
// uint64 division for every output sample; other rates retain division/modulo.
struct Sm64dsSampleClock {
    // Accumulator increment per output sample (ARM7 cycles) and the divisor
    // giving 16.76 MHz sound-timer ticks.
    std::uint64_t numerator = 33513982;
    std::uint64_t denominator;
    unsigned shift = 0;
    bool powerOfTwo;

    explicit Sm64dsSampleClock(unsigned rate) : denominator(2ull * rate),
        powerOfTwo(denominator && !(denominator & (denominator - 1))) {
        if(powerOfTwo)for(auto n=denominator;n>1;n>>=1)++shift;
    }
    Sm64dsSampleClock(std::uint64_t cyclesPerSample, std::uint64_t divisor) : numerator(cyclesPerSample),
        denominator(divisor), powerOfTwo(denominator && !(denominator & (denominator - 1))) {
        if(powerOfTwo)for(auto n=denominator;n>1;n>>=1)++shift;
    }

    unsigned reduce(std::uint64_t& accumulator) const {
        if(powerOfTwo){
            const auto cycles=accumulator>>shift;
            accumulator &= denominator-1;
            return static_cast<unsigned>(cycles);
        }
        const auto cycles=accumulator/denominator;
        accumulator %= denominator;
        return static_cast<unsigned>(cycles);
    }
};
