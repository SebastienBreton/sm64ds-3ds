// Host integration probe: retail menu dispatcher + original file operations + SD HAL.
// Drawing, sound, and summary callbacks are observed/isolated, not menu parity proof.
#include "SaveData.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <array>
extern "C" {
alignas(4) unsigned char data_0209caa0[0x400];
unsigned char data_0209cae4[740];
static unsigned char *files;
static unsigned summaries;
int func_0203da3c() { return 0; }
void func_0205a588(void *p, unsigned value, unsigned size) { std::memset(p,value,size); }
void CpuCopy8(const void *src, void *dst, unsigned size) { std::memcpy(dst,src,size); }
void SetSoundMode(int) { std::abort(); }
void TurnBacklightOn() { std::abort(); }
void TurnBacklightOff() { std::abort(); }
void *func_ov007_020cc0cc(int i) { assert(files && i>=0 && i<3);return files+i*68; }
void func_ov007_020cc0e4(void *p) { assert(p); files=static_cast<unsigned char*>(p); }
void *_ZN6Memory13operator_new2Ej(unsigned size) { assert(size==204);return std::calloc(1,size); }
void func_ov007_020cc168(unsigned i) { assert(i<3);++summaries; }
int func_ov007_020cc118(int,unsigned) { std::abort(); }
int _ZN8SaveData16ReadMinigameDataEP16MinigameSaveData(void*) { std::abort(); }
void _ZN8SaveData13SaveMinigamesEP16MinigameSaveData(void*) { std::abort(); }
void _ZN5Sound22StopLoadedMusic_Layer1Ej(unsigned) { std::abort(); }
unsigned char func_ov007_020cc600(int);
int func_02013c84(unsigned char,void*,int,void*);
}
using Block=std::array<unsigned char,68>;
static Block read(unsigned i) {
    Block b{};
    assert(SaveData::ReadFileData(i,reinterpret_cast<FileSaveData*>(b.data()))==1);
    return b;
}
int main(int argc,char**argv) {
    assert(argc==2);setenv("SM64DS_SAVE_PATH",argv[1],1);
    assert(!std::filesystem::exists(argv[1]));
    std::memset(data_0209caa0+0x48,255,4);
    // Initial read of blank cart allocates the exact buffer passed to the setter.
    assert(func_ov007_020cc600(20)==0 && files && summaries==4);
    Block blank=read(0);
    assert(blank[0]==0x38 && blank[4]==0 && blank[8]==8 && blank[0x41]==3);
    assert(!std::filesystem::exists(argv[1]));
    assert(func_ov007_020cc600(22)==0); // initialize unused slots
    assert(std::filesystem::file_size(argv[1])==8192);
    // Six directed copies, preserving source and the third slot.
    constexpr unsigned pairs[6][2]={{0,1},{0,2},{1,0},{1,2},{2,0},{2,1}};
    for(unsigned command=11;command<=16;++command) {
        for(unsigned i=0;i<3;++i) {
            auto b=blank;b[20]=0x81+i;b[50]=30+i;b[0x41]=i;
            assert(SaveData::SaveFile(i,reinterpret_cast<FileSaveData*>(b.data()))==1);
            std::memcpy(files+i*68,b.data(),68);
        }
        const unsigned source=pairs[command-11][0],dest=pairs[command-11][1];
        auto sourceBefore=read(source),thirdBefore=read(3-source-dest);
        assert(func_ov007_020cc600(command)==0);
        assert(read(source)==sourceBefore && read(dest)==sourceBefore);
        assert(read(3-source-dest)==thirdBefore);
        assert(std::memcmp(data_0209caa0,sourceBefore.data(),68)==0);
        assert(data_0209caa0[0x328]==dest);
    }
    for(unsigned i=0;i<3;++i) {
        assert(func_ov007_020cc600(8+i)==0);
        assert(read(i)==blank && data_0209caa0[0x328]==i);
    }
    // Reload from disk into a new menu allocation, with no cached slot payloads.
    std::free(files);files=nullptr;
    assert(func_ov007_020cc600(20)==0);
    for(unsigned i=0;i<3;++i) assert(std::memcmp(files+i*68,blank.data(),68)==0);
    std::free(files);
    puts("PASS menu save transactions: allocate/read, initialize, six directed copies, three erases, reload from disk");
}
