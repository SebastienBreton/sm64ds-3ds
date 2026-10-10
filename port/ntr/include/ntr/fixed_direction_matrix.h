#pragma once
#include <cstdint>
#include <cstring>

namespace ntr {
// Retain the exact existing float->double->fixed12 rounding. Most NORMAL
// commands share a directional matrix, so perform these conversions once per
// matrix rather than once per normal. Compare all source bits, never a hash.
class FixedDirectionMatrix {
    float source[16]{};
    std::int64_t fixed[9]{};
    bool valid=false;
public:
    const std::int64_t* coefficients(const float* matrix) {
        if(!valid || std::memcmp(source,matrix,sizeof source)!=0){
            std::memcpy(source,matrix,sizeof source);
            for(int k=0;k<3;++k)for(int j=0;j<3;++j){
                const double scaled=double(matrix[k*4+j])*4096.0;
                const std::int64_t whole=static_cast<std::int64_t>(scaled);
                const double fraction=scaled-whole;
                fixed[k*3+j]=whole+(fraction>=0.5)-(fraction<=-0.5);
            }
            valid=true;
        }
        return fixed;
    }
};

// Geometry's bounded cache keeps the same full-bit source identity and legacy
// rounding, with signed32 coefficients/products when all nine values fit.
// FixedDirectionMatrix above remains the cached64 reference/API for callers.
class BoundedFixedDirectionMatrix {
    float source[16]{};
    std::int32_t narrow[9]{};
    std::int64_t wide[9]{};
    bool valid=false, bounded=false;
public:
    bool update(const float* matrix) {
        if(valid && std::memcmp(source,matrix,sizeof source)==0){
            return bounded;
        }
        std::memcpy(source,matrix,sizeof source);
        bounded=true;
        for(int k=0;k<3;++k)for(int j=0;j<3;++j){
            const float scaled=matrix[k*4+j]*4096.f;
            // Ineligible values retain the original double/int64 fallback.
            // This does not define new semantics for legacy undefined inputs.
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
        }
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
}
