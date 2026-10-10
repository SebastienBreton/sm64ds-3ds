// Native SD-backed cart storage. Format follows SaveData's original routines.
#include "SaveData.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <unistd.h>

// Local extern: SDK/game session query has no non-generated shared header.
extern "C" int func_0203da3c(void);
namespace {
constexpr unsigned Capacity=8192, Half=Capacity/2;
// Boot0201a054 passes this eight-byte tag at0208ee50 to02042f68.
constexpr unsigned char Magic[8]={'d','s',' ','m','a','r','i','o'};
const char* savePath() {
    const char* path=std::getenv("SM64DS_SAVE_PATH");
    return path && *path ? path : "sdmc:/sm64ds/save.sav";
}
bool load(unsigned char (&image)[Capacity]) {
    FILE* file=std::fopen(savePath(),"rb");
    if (!file) {
        if (errno!=ENOENT) return false;
        std::memset(image,0xff,Capacity);
        return true;
    }
    const size_t n=std::fread(image,1,Capacity,file);
    const int extra=std::fgetc(file);
    const bool good=n==Capacity && extra==EOF && !std::ferror(file);
    const int closed=std::fclose(file);
    return good && closed==0;
}
bool store(const unsigned char (&image)[Capacity]) {
    const std::string path=savePath(), temporary=path+".tmp";
    FILE* file=std::fopen(temporary.c_str(),"wb");
    if (!file) return false;
    bool good=std::fwrite(image,1,Capacity,file)==Capacity;
    if (std::fflush(file)!=0) good=false;
    if (good && fsync(fileno(file))!=0) good=false;
    if (std::fclose(file)!=0) good=false;
    if (good && std::rename(temporary.c_str(),path.c_str())==0) return true;
    std::remove(temporary.c_str());
    return false;
}
bool validRange(unsigned size,unsigned slot) {
    // Reject overflow before shifting; the original takes slot<<7.
    return slot<Half/128 && size<=Half-slot*128-10;
}
unsigned short checksum(const unsigned char* bytes,unsigned count) {
    unsigned short value=0;
    for (unsigned char c:Magic) value+=c;
    for (unsigned i=0;i<count;++i)
        value=static_cast<unsigned short>(((value<<1)|(value>>15))^bytes[i]);
    return value;
}
bool hasMagic(const unsigned char* block) { return std::memcmp(block+2,Magic,8)==0; }
bool validBlock(const unsigned char* block,unsigned size) {
    const unsigned short stored=block[0]|(block[1]<<8);
    return hasMagic(block) && checksum(block+10,size)==stored;
}
}

int SaveData::SaveDataToCart(char* data,u32 size,u32 slot) {
    if (func_0203da3c()==2 || !validRange(size,slot) || (!data && size)) return 1;
    unsigned char image[Capacity];
    if (!load(image)) return 1;
    const unsigned short sum=checksum(reinterpret_cast<unsigned char*>(data),size);
    for (unsigned base:{slot*128,Half+slot*128}) {
        image[base]=sum&255; image[base+1]=sum>>8;
        std::memcpy(image+base+2,Magic,8);
        if (size) std::memcpy(image+base+10,data,size);
    }
    return store(image)?0:1;
}

int SaveData::ReadDataFromCart(char* data,u32 size,u32 slot) {
    if (func_0203da3c()==2 || !validRange(size,slot) || (!data && size)) return 1;
    unsigned char image[Capacity];
    if (!load(image)) return 1;
    const unsigned char* primary=image+slot*128;
    const unsigned char* mirror=primary+Half;
    for (const unsigned char* block:{primary,mirror}) {
        if (validBlock(block,size)) {
            if (size) std::memcpy(data,block+10,size);
            return 0;
        }
    }
    // Original cart API:2 for absent tags;1 for corrupt tagged data/I/O failure.
    return !hasMagic(primary) && !hasMagic(mirror)?2:1;
}
