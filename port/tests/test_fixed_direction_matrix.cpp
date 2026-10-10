#include "ntr/fixed_direction_matrix.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>
#ifdef __3DS__
#include <3ds.h>
#include "cpu_mode.h"
#endif
static float matrices[32][16];
static unsigned normals[32];
static std::int64_t checksum;
static void legacy(const float* matrix,unsigned packed,int* out,bool negate){
    int raw[3];for(int k=0;k<3;++k){raw[k]=(packed>>(10*k))&1023;if(raw[k]>=512)raw[k]-=1024;}
    for(int j=0;j<3;++j){
        std::int64_t sum=0;
        for(int k=0;k<3;++k){
            const double scaled=double(matrix[k*4+j])*4096.0;
            const std::int64_t whole=static_cast<std::int64_t>(scaled);
            const double fraction=scaled-whole;
            const auto fixed=whole+(fraction>=0.5)-(fraction<=-0.5);
            sum+=raw[k]*fixed;
        }
        int value=static_cast<int>(sum>>12);value=(negate?-value:value)&2047;
        out[j]=value>=1024?value-2048:value;
    }
}
static ntr::FixedDirectionMatrix cache;
static void cached(const float* matrix,unsigned packed,int* out,bool negate){
    const auto* fixed=cache.coefficients(matrix);
    int raw[3];for(int k=0;k<3;++k){raw[k]=(packed>>(10*k))&1023;if(raw[k]>=512)raw[k]-=1024;}
    for(int j=0;j<3;++j){
        std::int64_t sum=0;for(int k=0;k<3;++k)sum+=raw[k]*fixed[k*3+j];
        int value=static_cast<int>(sum>>12);value=(negate?-value:value)&2047;
        out[j]=value>=1024?value-2048:value;
    }
}
int main(){
#ifdef __3DS__
    gfxInitDefault();std::freopen("sdmc:/sm64ds/log.txt","w",stdout);
    std::setvbuf(stdout,nullptr,_IONBF,0);sm64ds_configure_cpu_mode();
#endif
    unsigned random=0x941da;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    unsigned compared=0;
    // Adjacent representable floats below/at/above half-unit rounding edges,
    // signed zero and the largest fixed12 coefficients of a DS matrix.
    const float edges[]={0.f,-0.f,0.5f/4096,-0.5f/4096,1.5f/4096,-1.5f/4096,
                         524287.9375f,-524288.f};
    for(float edge:edges)for(int direction=-1;direction<=1;++direction){
        const auto value=direction?std::nextafter(edge,direction<0?-INFINITY:INFINITY):edge;
        for(int coefficient=0;coefficient<16;++coefficient){
            float matrix[16]{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1.f;
            matrix[coefficient]=value;
            for(unsigned packed:{0u,0x1ff7fdffu,0x20080200u,0x1ffffe00u})for(bool negate:{false,true}){
                int a[3],b[3];legacy(matrix,packed,a,negate);cached(matrix,packed,b,negate);
                if(std::memcmp(a,b,sizeof a)){std::puts("FAIL: conversion rounding edge differs");return 1;}
                ++compared;
            }
        }
    }
    for(unsigned batch=0;batch<128;++batch){
        for(auto& matrix:matrices)for(auto& v:matrix)v=(int(next()%0x100001)-0x80000)/65536.f;
        for(auto& normal:normals)normal=next();
        for(unsigned i=0;i<1024;++i){
            auto* matrix=matrices[(i/16)%32];int a[3],b[3];bool negate=i&1;
            legacy(matrix,normals[i%32],a,negate);cached(matrix,normals[i%32],b,negate);
            if(std::memcmp(a,b,sizeof a)){std::puts("FAIL: directional vector differs");return 1;}
            ++compared;
        }
    }
    auto measure=[&](auto transform){
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<100000;++i){int out[3];transform(matrices[(i/16)%32],normals[i%32],out,i&1);checksum+=out[0]+out[1]+out[2];}
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100000;
    };
    const auto before=measure(legacy),after=measure(cached);
    std::printf("PASS: %u exact directional vectors including repeated and changed matrices\n",compared);
    std::printf("Lighting ms/vector: legacy=%.9f cached=%.9f checksum=%lld\n",before,after,static_cast<long long>(checksum));
#ifdef __3DS__
    gfxExit();
#endif
    std::puts("PASS");return 0;
}
