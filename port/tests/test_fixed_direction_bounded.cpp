// Isolated experiment only; production FixedDirectionMatrix is unchanged.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static std::uint64_t checked,fast,fallback,undefined_skipped;
static std::int64_t original(float value)
{
    const double scaled=double(value)*4096.0;
    const auto whole=static_cast<std::int64_t>(scaled);
    const double fraction=scaled-whole;
    return whole+(fraction>=0.5)-(fraction<=-0.5);
}
static std::int64_t candidate(float value)
{
    const float scaled=value*4096.f;
    if(scaled>=-2147483648.f && scaled<2147483648.f){
        ++fast;
        const auto whole=static_cast<std::int32_t>(scaled);
        const float fraction=scaled-float(whole);
        return std::int64_t(whole)+(fraction>=.5f)-(fraction<=-.5f);
    }
    ++fallback;return original(value);
}
static void check(std::uint32_t bits)
{
    float value;std::memcpy(&value,&bits,4);
    const double scaled=double(value)*4096.0;
    // Existing conversion is undefined outside the signed64 domain. Those
    // values may only remain on its legacy fallback; do not invent an oracle.
    if(!std::isfinite(scaled) || scaled< -9223372036854775808.0 || scaled>=9223372036854775808.0){
        const float s=value*4096.f;
        if(s>=-2147483648.f && s<2147483648.f)std::abort();
        ++undefined_skipped;return;
    }
    const auto a=original(value),b=candidate(value);
    if(a!=b){std::printf("FAIL coefficient bits=%08x old=%lld candidate=%lld\n",bits,(long long)a,(long long)b);std::abort();}
    ++checked;
}
int main()
{
    // Every sign/exponent, dense low mantissas and neighborhoods at the top,
    // plus a coprime sweep through mantissas. Include exponent transitions.
    for(unsigned sign=0;sign<2;++sign)for(unsigned exponent=0;exponent<256;++exponent){
        const auto base=(sign<<31)|(exponent<<23);
        for(unsigned i=0;i<128;++i){check(base|i);check(base|(0x7fffff-i));}
        for(unsigned mantissa=0;mantissa<0x800000;mantissa+=4093)check(base|mantissa);
    }
    std::uint32_t random=0x5e654328;
    for(unsigned i=0;i<1048576;++i){random=random*1664525u+1013904223u;check(random);}
    std::printf("PASS isolated fixed direction: checked=%llu fast=%llu fallback=%llu undefined-original-skipped=%llu; production unchanged\n",
        (unsigned long long)checked,(unsigned long long)fast,(unsigned long long)fallback,(unsigned long long)undefined_skipped);
    return 0;
}
