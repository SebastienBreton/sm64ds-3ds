// Isolated candidate: do not include from the production geometry engine.
#pragma once
#include <cstdint>
#include <cstring>

template<bool Count=false>
class FixedDirectionBoundedCandidate {
    float source[16]{};
    std::int32_t narrow[9]{};
    std::int64_t wide[9]{};
    bool valid=false, bounded=false;
public:
    struct Statistics { std::uint64_t hits=0,misses=0,bounded_misses=0,fallback_misses=0; } stats;
    bool update(const float* matrix) {
        if(valid && std::memcmp(source,matrix,sizeof source)==0){
            if constexpr(Count)++stats.hits;
            return bounded;
        }
        if constexpr(Count)++stats.misses;
        std::memcpy(source,matrix,sizeof source);
        bounded=true;
        for(int k=0;k<3;++k)for(int j=0;j<3;++j){
            const float scaled=matrix[k*4+j]*4096.f;
            // NaN/infinity and either out-of-int32 bound stay on the legacy
            // conversion path. Its previously undefined inputs are not given
            // new semantics by this candidate.
            if(!(scaled>=-2147483648.f && scaled<2147483648.f)){
                bounded=false;
                continue;
            }
            const auto whole=static_cast<std::int32_t>(scaled);
            const float fraction=scaled-float(whole);
            narrow[k*3+j]=whole+(fraction>=.5f)-(fraction<=-.5f);
        }
        if(!bounded){
            for(int k=0;k<3;++k)for(int j=0;j<3;++j){
                const double scaled=double(matrix[k*4+j])*4096.0;
                const auto whole=static_cast<std::int64_t>(scaled);
                const double fraction=scaled-whole;
                wide[k*3+j]=whole+(fraction>=.5)-(fraction<=-.5);
            }
            if constexpr(Count)++stats.fallback_misses;
        }else if constexpr(Count)++stats.bounded_misses;
        valid=true;
        return bounded;
    }
    const std::int32_t* narrow_coefficients() const {return narrow;}
    const std::int64_t* wide_coefficients() const {return wide;}
    bool is_bounded() const {return bounded;}
    void transform(const float* matrix,std::uint32_t packed,int* out,bool negate){
        const bool use_narrow=update(matrix);
        int raw[3];
        for(int k=0;k<3;++k){raw[k]=(packed>>(10*k))&1023;if(raw[k]>=512)raw[k]-=1024;}
        for(int j=0;j<3;++j){
            std::int64_t sum=0;
            if(use_narrow){
                // Both operands are signed32; promotion before multiplication
                // makes the ARM signed32*32 -> signed64 contract explicit.
                for(int k=0;k<3;++k)sum+=std::int64_t(raw[k])*narrow[k*3+j];
            }else{
                for(int k=0;k<3;++k)sum+=raw[k]*wide[k*3+j];
            }
            const int value=static_cast<int>(sum>>12);
            const unsigned wrapped=unsigned(negate?-value:value)&2047u;
            out[j]=wrapped>=1024?int(wrapped)-2048:int(wrapped);
        }
    }
};
