#include <cstdio>
#include <cstring>
struct LevelResetRange { unsigned char *data; const unsigned char *initial; unsigned size,overlay; };
extern "C" {
extern const LevelResetRange sm64ds_level_reset_ranges[],sm64ds_level_reset_ranges_end[];
unsigned sm64ds_reset_level_data(unsigned);
}
int main() {
    unsigned failures=0,bytes=0,ranges=0;
    for(auto *r=sm64ds_level_reset_ranges;r!=sm64ds_level_reset_ranges_end;++r) {
        failures+=std::memcmp(r->data,r->initial,r->size)!=0;
        for(unsigned i=0;i<r->size;++i)r->data[i]=r->initial[i]^0xa5;
        bytes+=r->size;++ranges;
    }
    failures+=sm64ds_reset_level_data(7)!=0;
    failures+=sm64ds_reset_level_data(60)!=0;
    for(unsigned ov=8;ov<60;++ov) {
        unsigned expected=0;
        for(auto *r=sm64ds_level_reset_ranges;r!=sm64ds_level_reset_ranges_end;++r)
            if(r->overlay==ov)expected+=r->size;
        failures+=expected==0;
        failures+=sm64ds_reset_level_data(ov)!=expected;
        failures+=sm64ds_reset_level_data(ov)!=expected;
        for(auto *r=sm64ds_level_reset_ranges;r!=sm64ds_level_reset_ranges_end;++r)
            for(unsigned i=0;i<r->size;++i)
                if(r->data[i]!=(r->initial[i]^(r->overlay>ov?0xa5:0))) {++failures;break;}
    }
    std::printf("smoke_level_reset: %s levels=52 ranges=%u bytes=%u failures=%u\n",failures?"FAIL":"PASS",ranges,bytes,failures);
    return failures?1:0;
}
