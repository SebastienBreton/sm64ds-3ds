// Compare the real native SDK call chain to independent retail ARM snapshots.
#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "bank_upload_golden.h"

extern "C" {
void _ZN2GX13SetBankForTexEt(unsigned short);
void _ZN2GX17SetBankForTexPlttEt(unsigned short);
void InitialiseVramGlobals();
void _ZN2GX12BeginLoadTexEv(); void _ZN2GX10EndLoadTexEv();
void _ZN2GX16BeginLoadTexPlttEv(); void _ZN2GX14EndLoadTexPlttEv();
void _ZN2GX7LoadTexEPKvjj(const void*,unsigned,unsigned);
void _ZN2GX11LoadTexPlttEPKvjj(const void*,unsigned,unsigned);
extern unsigned short data_020a6088[];
extern unsigned data_020a60ac,data_020a60b0,data_020a60b4,data_020a60b8,data_020a60bc,data_020a60c0;
extern unsigned data_020a4bc8,data_020a4bcc,data_020a4bd0,data_020a4bd4,data_020a4bd8;
extern unsigned data_020a4bdc,data_020a4be0,data_020a4be4,data_020a4be8;
extern int data_02099fd0;
}

static unsigned* const windows[]={&data_020a60ac,&data_020a60b0,&data_020a60b4,&data_020a60b8,&data_020a60bc,&data_020a60c0};
static unsigned* const budgets[]={&data_020a4bc8,&data_020a4bcc,&data_020a4bd0,&data_020a4bd4,&data_020a4bd8,&data_020a4bdc,&data_020a4be0,&data_020a4be4,&data_020a4be8};
static unsigned char* expected;
#define CHECK(c) do{if(!(c)){std::printf("FATAL: bank upload case=%u stage=%u line=%d %s\n",ci,si,__LINE__,#c);return 1;}}while(0)
int main()
{
    unsigned ci=0,si=0,uploads=0;unsigned long long compared=0;
    setvbuf(stdout,nullptr,_IONBF,0);
    setenv("PLAYER_PHYSICAL_VRAM","1",1);
    CHECK(ntr::io_init());auto* bus=ntr::io_vram_banks();CHECK(bus);
    // A runtime pointer avoids a static pointer-minus-DS-address relocation,
    // whose negative addend is not representable in the 3DSX loader format.
    expected=static_cast<unsigned char*>(std::malloc(0xa4000));CHECK(expected);
    unsigned char* banks[9];for(unsigned b=0;b<9;++b)banks[b]=bus->physical(b);
    for(const auto& test:upload_cases){
        std::memset(ntr::host_ptr(ntr::IO_BASE),0,ntr::IO_SIZE);
        for(unsigned b=0;b<9;++b)std::memset(banks[b],0,ntr::VramBanks::bank_size(b));
        bus->reset(banks);std::memset(expected,0,0xa4000);
        std::memset(data_020a6088,0,32);data_020a6088[0]=0x1ff;
        for(auto* p:windows)*p=0;for(auto* p:budgets)*p=0;
        data_02099fd0=test.dma;ntr::io_write(0x04000060,0x3f00,2);
        for(si=0;si<test.count;++si){
            const auto& step=upload_steps[test.first+si];
            unsigned payload[16]{};
            CHECK(step.write_count<=16);
            for(unsigned w=0;w<step.write_count;++w){
                const auto& transaction=upload_writes[step.first_write+w];
                payload[w]=transaction.value;
                CHECK(transaction.width==4 && transaction.address>=0x06800000 && transaction.address+4<=0x068a4000);
                std::memcpy(expected+transaction.address-0x06800000,&transaction.value,4);
            }
            switch(step.op){
                case 0:_ZN2GX13SetBankForTexEt(test.tex);break;
                case 1:_ZN2GX17SetBankForTexPlttEt(test.pal);break;
                case 2:InitialiseVramGlobals();break;
                case 3:_ZN2GX12BeginLoadTexEv();break;
                case 4:_ZN2GX10EndLoadTexEv();break;
                case 5:_ZN2GX16BeginLoadTexPlttEv();break;
                case 6:_ZN2GX14EndLoadTexPlttEv();break;
                case 7:_ZN2GX7LoadTexEPKvjj(payload,step.offset,step.bytes);++uploads;break;
                case 8:_ZN2GX11LoadTexPlttEPKvjj(payload,step.offset,step.bytes);++uploads;break;
                default:CHECK(false);
            }
            for(unsigned i=0;i<16;++i)CHECK(data_020a6088[i]==step.state[i]);
            for(unsigned i=0;i<6;++i)CHECK(*windows[i]==step.windows[i]);
            for(unsigned i=0;i<9;++i)CHECK(*budgets[i]==step.budget[i]);
            CHECK(ntr::io_read(0x04000060,2)==step.io60);
            for(unsigned i=0;i<10;++i)CHECK(ntr::io_read(0x04000240+i,1)==step.controls[i]);
        }
        if(test.dma==-1){
            unsigned offset=0;
            for(unsigned b=0;b<9;++b){
                const auto bytes=ntr::VramBanks::bank_size(b);
                CHECK(std::memcmp(banks[b],expected+offset,bytes)==0);
                offset+=bytes;compared+=bytes;
            }
        }
        ++ci;if(ci%80==0)std::printf("Bank upload progress cases=%u uploads=%u\n",ci,uploads);
    }
    // Legacy mode must preserve the old flat-window contract without remaps.
    unsigned old_windows[6];unsigned short old_state[16];unsigned char old_io[ntr::IO_SIZE];
    for(unsigned i=0;i<6;++i)old_windows[i]=*windows[i];
    std::memcpy(old_state,data_020a6088,sizeof old_state);
    std::memcpy(old_io,ntr::host_ptr(ntr::IO_BASE),sizeof old_io);
    ntr::vram_attach_cpu_bus(nullptr);
    _ZN2GX12BeginLoadTexEv();_ZN2GX10EndLoadTexEv();
    _ZN2GX16BeginLoadTexPlttEv();_ZN2GX14EndLoadTexPlttEv();
    for(unsigned i=0;i<6;++i)CHECK(*windows[i]==old_windows[i]);
    CHECK(std::memcmp(old_state,data_020a6088,sizeof old_state)==0);
    CHECK(std::memcmp(old_io,ntr::host_ptr(ntr::IO_BASE),sizeof old_io)==0);
    ntr::vram_attach_cpu_bus(bus);
    std::free(expected);
    std::printf("PASS bank upload ARM snapshots: cases=%u uploads=%u complete physical bytes=%llu; DMA payload excluded\n",ci,uploads,compared);
    return 0;
}
