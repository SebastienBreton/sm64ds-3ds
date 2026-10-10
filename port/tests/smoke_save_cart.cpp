#include "SaveData.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/time.h>
extern "C" int func_0203da3c() { return 0; }
int main() {
    char path[128];
    timeval now;
    if (gettimeofday(&now,nullptr)!=0) return 12;
    std::snprintf(path,sizeof path,"sdmc:/sm64ds/save-probe-%llu.sav",(unsigned long long)now.tv_sec*1000000+now.tv_usec);
    if (access(path,F_OK)==0) { std::puts("FAIL: test path already exists"); return 1; }
    setenv("SM64DS_SAVE_PATH",path,1);
    unsigned char image[8192];
    char input[740],output[740];
    for (unsigned i=0;i<sizeof input;++i) input[i]=(i*37+17)&255;
    if (SaveData::ReadDataFromCart(output,68,0)!=2) { std::puts("FAIL: blank read"); return 2; }
    for (unsigned pass=0;pass<2;++pass) {
        input[0]=pass+31;
        errno=0;
        int result=SaveData::SaveDataToCart(input,sizeof input,3);
        std::printf("save pass %u: result=%d errno=%d path=%s\n",pass,result,errno,path);
        if (result) return 3;
        if (SaveData::ReadDataFromCart(output,sizeof output,3)!=0 ||
            std::memcmp(input,output,sizeof input)) { std::puts("FAIL: roundtrip"); return 4; }
    }
    FILE* file=std::fopen(path,"rb");
    if (!file) return 5;
    const bool full=std::fread(image,1,sizeof image,file)==sizeof image;
    const bool closed=std::fclose(file)==0;
    if (!full || !closed || std::memcmp(image+384,image+4480,750)) return 6;
    if (std::memcmp(image+386,"ds mario",8)) return 7;
    image[394]^=1;
    file=std::fopen(path,"wb");
    if (!file) return 8;
    const bool written=std::fwrite(image,1,sizeof image,file)==sizeof image;
    const bool closewrite=std::fclose(file)==0;
    if (!written || !closewrite) return 9;
    if (SaveData::ReadDataFromCart(output,sizeof output,3)!=0 ||
        std::memcmp(input,output,sizeof input)) return 10;
    if (std::remove(path)!=0) return 11;
    std::puts("SD save: create, replace, reopen, mirror bytes and recovery verified");
    return 0;
}
