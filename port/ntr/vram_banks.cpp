#include "ntr/vram_banks.h"
#include <cstring>

namespace ntr {
namespace {
unsigned unit(VramView view){
    switch(view){
        case VramView::ABgPalette:case VramView::BBgPalette:return 0x2000;
        case VramView::AObjPalette:case VramView::BObjPalette:return 0x2000;
        case VramView::Texture:case VramView::Arm7:return 0x20000;
        default:return 0x4000;
    }
}
unsigned size(VramView view){
    switch(view){
        case VramView::ABg:case VramView::Texture:return 0x80000;
        case VramView::AObj:case VramView::Arm7:return 0x40000;
        case VramView::BBg:case VramView::BObj:return 0x20000;
        case VramView::TexturePalette:return 0x20000;
        case VramView::ABgPalette:case VramView::BBgPalette:return 0x8000;
        case VramView::AObjPalette:case VramView::BObjPalette:return 0x2000;
        default:return 0;
    }
}
}
unsigned VramBanks::bank_size(unsigned bank){return bank<4?0x20000:bank==4?0x10000:bank==7?0x8000:bank<9?0x4000:0;}
void VramBanks::reset(uint8_t* const banks[9]){
    for(unsigned i=0;i<9;++i){banks_[i]=banks[i];controls_[i]=0;}
    rebuild();++generation_;
    for(auto& version:view_generation_)++version;
    for(auto& version:bank_generation_)++version;
}
uint8_t VramBanks::map(unsigned bank,uint8_t control){
    if(bank>=9)return 0;
    const uint8_t mask=bank<2?0x9b:bank==4?0x87:bank<7?0x9f:0x83;
    control&=mask;
    if(controls_[bank]!=control){controls_[bank]=control;rebuild();++generation_;}
    return control;
}
void VramBanks::rebuild(){
    uint16_t previous[unsigned(VramView::Count)][32];std::memcpy(previous,pages_,sizeof pages_);
    std::memset(pages_,0,sizeof pages_);lcdc_=0;
    for(unsigned bank=0;bank<9;++bank){
        const unsigned c=controls_[bank], mode=c&7,ofs=(c>>3)&3;
        if(!(c&0x80))continue;
        auto add=[&](VramView v,unsigned base,unsigned count){for(unsigned i=0;i<count;++i)pages_[unsigned(v)][base+i]|=1u<<bank;};
        if(mode==0){lcdc_|=1u<<bank;continue;}
        if(bank<4){
            // A/B have two mode bits; bit2 is masked by map().
            if(mode==1)add(VramView::ABg,ofs*8,8);
            else if(mode==2){if(bank<2)add(VramView::AObj,(ofs&1)*8,8);else add(VramView::Arm7,ofs&1,1);}
            else if(mode==3)add(VramView::Texture,ofs,1);
            else if(mode==4)add(bank==2?VramView::BBg:VramView::BObj,0,8);
        }else if(bank==4){
            if(mode==1)add(VramView::ABg,0,4);
            else if(mode==2)add(VramView::AObj,0,4);
            else if(mode==3)add(VramView::TexturePalette,0,4);
            else if(mode==4)add(VramView::ABgPalette,0,4);
        }else if(bank<7){
            const unsigned base=(ofs&1)+((ofs&2)*2);
            if(mode==1 || mode==2){auto v=mode==1?VramView::ABg:VramView::AObj;add(v,base,1);add(v,base+2,1);}
            else if(mode==3)add(VramView::TexturePalette,base,1);
            else if(mode==4)add(VramView::ABgPalette,(ofs&1)*2,2);
            else if(mode==5)add(VramView::AObjPalette,0,1);
        }else if(bank==7){
            if(mode==1){add(VramView::BBg,0,2);add(VramView::BBg,4,2);}
            else if(mode==2)add(VramView::BBgPalette,0,4);
        }else{
            if(mode==1){add(VramView::BBg,2,2);add(VramView::BBg,6,2);}
            else if(mode==2)add(VramView::BObj,0,8);
            else if(mode==3)add(VramView::BObjPalette,0,1);
        }
    }
    std::memset(bank_views_,0,sizeof bank_views_);
    for(unsigned view=0;view<unsigned(VramView::Count);++view){
        if(std::memcmp(previous[view],pages_[view],sizeof pages_[view]))++view_generation_[view];
        unsigned banks=0;for(auto page:pages_[view])banks|=page;
        for(unsigned bank=0;bank<9;++bank)if(banks&(1u<<bank))bank_views_[bank]|=1u<<view;
    }
}
uint8_t VramBanks::arm7_status()const{return uint8_t((controls_[2]&0x87)==0x82)|uint8_t(((controls_[3]&0x87)==0x82)<<1);}
uint16_t VramBanks::resolve(VramView view,unsigned& offset)const{
    const unsigned bytes=size(view);if(!bytes)return 0;
    offset&=bytes-1;return pages_[unsigned(view)][offset/unit(view)];
}
uint16_t VramBanks::resolve_cpu(uint32_t address,unsigned& offset)const{
    if((address>>24)!=6)return 0;
    offset=address;
    switch(address&0x00e00000){
        case 0:return resolve(VramView::ABg,offset);
        case 0x200000:return resolve(VramView::BBg,offset);
        case 0x400000:return resolve(VramView::AObj,offset);
        case 0x600000:return resolve(VramView::BObj,offset);
        default:break;
    }
    const unsigned loc=address&0x8fc000;
    unsigned bank;
    if(loc<0x880000)bank=(loc-0x800000)/0x20000;
    else if(loc<0x890000)bank=4;
    else if(loc==0x890000)bank=5;
    else if(loc==0x894000)bank=6;
    else if(loc==0x898000 || loc==0x89c000)bank=7;
    else if(loc==0x8a0000)bank=8;
    else return 0;
    offset=address&(bank_size(bank)-1);return lcdc_&(1u<<bank);
}
uint32_t VramBanks::read_mask(uint16_t mask,unsigned offset,unsigned width)const{
    uint32_t value=0;
    for(unsigned bank=0;bank<9;++bank)if((mask&(1u<<bank)) && banks_[bank])
        for(unsigned i=0;i<width;++i)value|=uint32_t(banks_[bank][(offset+i)&(bank_size(bank)-1)])<<(i*8);
    return value;
}
uint32_t VramBanks::read_cpu(uint32_t address,unsigned width)const{
    if(width!=1 && width!=2 && width!=4)return 0;
    address&=~(width-1);unsigned offset;const unsigned mask=resolve_cpu(address,offset);return read_mask(mask,offset,width);
}
uint32_t VramBanks::read(VramView view,unsigned offset,unsigned width)const{
    if(width!=1 && width!=2 && width!=4)return 0;
    offset&=~(width-1);const unsigned mask=resolve(view,offset);return read_mask(mask,offset,width);
}
void VramBanks::changed(unsigned bank,unsigned offset,unsigned bytes){
    if(bank>=9)return;
    ++generation_;
    ++bank_generation_[bank];
    for(unsigned view=0;view<unsigned(VramView::Count);++view)
        if(bank_views_[bank]&(1u<<view))++view_generation_[view];
    if(written)written(context,bank,offset,bytes);
}
void VramBanks::write_mask(uint16_t mask,unsigned offset,uint32_t value,unsigned width){
    for(unsigned bank=0;bank<9;++bank)if((mask&(1u<<bank)) && banks_[bank]){
        const unsigned base=offset&(bank_size(bank)-1);
        for(unsigned i=0;i<width;++i)banks_[bank][(base+i)&(bank_size(bank)-1)]=uint8_t(value>>(i*8));
        changed(bank,base,width);
    }
}
void VramBanks::write_cpu(uint32_t address,uint32_t value,unsigned width){
    if(width!=2 && width!=4)return; // ARM9 byte VRAM stores are ignored.
    address&=~(width-1);unsigned offset;const unsigned mask=resolve_cpu(address,offset);write_mask(mask,offset,value,width);
}
void VramBanks::write_arm7(unsigned offset,uint32_t value,unsigned width){
    if(width!=1 && width!=2 && width!=4)return;
    offset&=~(width-1);const unsigned mask=resolve(VramView::Arm7,offset);write_mask(mask,offset,value,width);
}
void VramBanks::copy_view(VramView view,unsigned offset,uint8_t* output,unsigned bytes)const{
    const unsigned granularity=unit(view);
    while(bytes){
        unsigned mapped=offset;const unsigned mask=resolve(view,mapped);
        unsigned count=granularity-(mapped&(granularity-1));if(count>bytes)count=bytes;
        std::memset(output,0,count);
        bool first=true;
        for(unsigned bank=0;bank<9;++bank)if((mask&(1u<<bank)) && banks_[bank]){
            const unsigned bankmask=bank_size(bank)-1;
            unsigned done=0;
            while(done<count){
                const unsigned at=(mapped+done)&bankmask;
                unsigned part=bank_size(bank)-at;if(part>count-done)part=count-done;
                if(first)std::memcpy(output+done,banks_[bank]+at,part);
                else for(unsigned i=0;i<part;++i)output[done+i]|=banks_[bank][at+i];
                done+=part;
            }
            first=false;
        }
        offset+=count;output+=count;bytes-=count;
    }
}
VramSpan VramBanks::unique_cpu_span(uint32_t address)const{
    unsigned offset;const unsigned mask=resolve_cpu(address,offset);
    if(!mask || (mask&(mask-1)))return {};
    const unsigned bank=__builtin_ctz(mask),loc=offset&(bank_size(bank)-1);
    if(!banks_[bank])return {};
    // Also stop at a map-page boundary so the next page cannot silently select
    // another bank or a newly overlapping alias.
    return {banks_[bank]+loc,0x4000-(loc&0x3fff)};
}
}
