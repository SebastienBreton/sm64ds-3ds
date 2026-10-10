#include "ntr/display_capture.h"
#include <cstdlib>

namespace ntr {
namespace {
constexpr uint32_t kWritable = 0xef3f1f1f;
uint16_t load(const uint8_t* bank, unsigned word) {
    const unsigned at = (word & 0xffff) * 2;
    return static_cast<uint16_t>(bank[at] | (bank[at+1] << 8));
}
void store(uint8_t* bank, unsigned word, uint16_t color) {
    const unsigned at = (word & 0xffff) * 2;
    bank[at] = static_cast<uint8_t>(color);
    bank[at+1] = static_cast<uint8_t>(color >> 8);
}
uint16_t quantize(uint32_t color) {
    return static_cast<uint16_t>(((color >> 19) & 31) | ((color >> 6) & 0x3e0) |
                                 ((color << 7) & 0x7c00) | ((color >> 24) ? 0x8000 : 0));
}
uint16_t blend(uint16_t a, uint16_t b, unsigned eva, unsigned evb) {
    const unsigned alpha_a = a >> 15, alpha_b = b >> 15;
    unsigned color = ((eva && alpha_a) || (evb && alpha_b)) ? 0x8000 : 0;
    for (unsigned shift = 0; shift < 15; shift += 5) {
        unsigned value = ((((a >> shift) & 31) * alpha_a * eva +
                           ((b >> shift) & 31) * alpha_b * evb + 8) >> 4);
        if (value > 31) value = 31;
        color |= value << shift;
    }
    return static_cast<uint16_t>(color);
}
uint32_t display_color(uint16_t color) {
    uint32_t out=0xff000000u;
    for(unsigned channel=0;channel<3;++channel){
        const unsigned six=((color>>(channel*5))&31)<<1;
        out|=((six<<2)|(six>>4))<<((2-channel)*8);
    }
    return out;
}
uint32_t master_color(uint32_t color,uint16_t master) {
    const unsigned mode=master>>14;
    if(mode!=1 && mode!=2)return 0xff000000u|(color&0xffffffu);
    const unsigned factor=(master&31)>16?16:master&31;
    uint32_t out=0xff000000u;
    for(unsigned shift=0;shift<24;shift+=8){
        unsigned six=(color>>(shift+2))&63;
        if(mode==1)six+=((63-six)*factor)>>4;
        else six-=(six*factor+15)>>4;
        out|=((six<<2)|(six>>4))<<shift;
    }
    return out;
}
}

uint8_t display_capture_lcdc_mask(const uint8_t controls[4]) {
    unsigned mask = 0;
    for (unsigned bank = 0; bank < 4; ++bank)
        if ((controls[bank] & (bank < 2 ? 0x83 : 0x87)) == 0x80) mask |= 1u << bank;
    return static_cast<uint8_t>(mask);
}
bool display_capture_gpu_request(uint32_t control,const uint8_t controls[4],
                                 uint32_t display,DisplayCaptureGpuRequest& request){
    request=DisplayCaptureGpuRequest{};
    if(!(control&0x80000000u))return true;
    static const bool disabled=std::getenv("PLAYER_NO_GPU_CAPTURE")!=nullptr;
    if(disabled)return false;
    if(((control>>29)&3)!=0 || (control&(1u<<24)) || ((control>>20)&3)!=3 || ((control>>18)&3)>1 ||
       ((display>>16)&3)!=1)return false;
    const unsigned bank=(control>>16)&3;
    if(!(display_capture_lcdc_mask(controls)&(1u<<bank)))return false;
    request.active=true;request.bank=bank;request.byte_offset=((control>>18)&3)<<15;
    return true;
}
void DisplayCapture::reset() { control_ = 0; latched_ = false; }
bool DisplayCapture::write_control(unsigned offset, uint32_t value, unsigned width) {
    if (width == 4 && offset == 0) control_ = value & kWritable;
    else if (width == 2 && (offset == 0 || offset == 2)) {
        const unsigned shift = offset * 8;
        const uint32_t mask = 0xffffu << shift;
        control_ = (control_ & ~mask) | ((value << shift) & mask & kWritable);
    } else return false;
    return true;
}
void DisplayCapture::vblank() {
    if (latched_) { control_ &= ~(1u << 31); latched_ = false; }
}
DisplayCaptureSources DisplayCapture::sources(unsigned line,bool outside,uint8_t lcdc_mask)const {
    const unsigned heights[4]={128,64,128,192};
    const bool active=(latched_ || (line==0 && (control_&(1u<<31)))) && !outside &&
        line<heights[(control_>>20)&3] && (lcdc_mask&(1u<<((control_>>16)&3)));
    if(!active)return {};
    const unsigned mode=(control_>>29)&3;
    const bool a=mode!=1,raw=(control_&(1u<<24))!=0;
    return {true,a&&!raw,a&&raw,mode!=0 && (control_&(1u<<25))!=0};
}
DisplayCaptureResult DisplayCapture::scanline(unsigned line, bool outside,
                                             uint32_t display, DisplayCaptureMemory& memory,
                                             const uint32_t* composite, const uint32_t* geometry,
                                             const uint16_t* fifo) {
    if (line == 0 && (control_ & (1u << 31)) && !outside) latched_ = true;
    if (!latched_) return {DisplayCaptureStatus::Inactive};
    const unsigned size = (control_ >> 20) & 3;
    const unsigned heights[4] = {128, 64, 128, 192};
    if (outside || line >= heights[size]) return {DisplayCaptureStatus::OutsideCapture};
    const unsigned bank = (control_ >> 16) & 3, width = size ? 256 : 128;
    if (!(memory.lcdc_mask & (1u << bank)) || !memory.banks[bank])
        return {DisplayCaptureStatus::DestinationUnmapped, bank};
    const unsigned mode = (control_ >> 29) & 3;
    const uint32_t* source_a = (control_ & (1u << 24)) ? geometry : composite;
    if (mode != 1 && !source_a)
        return {(control_ & (1u << 24)) ? DisplayCaptureStatus::MissingGeometry :
                                                         DisplayCaptureStatus::MissingComposite, bank};
    const bool use_fifo = (control_ & (1u << 25)) != 0;
    if (mode != 0 && use_fifo && !fifo) return {DisplayCaptureStatus::MissingFifo, bank};
    const unsigned source_bank = (display >> 18) & 3;
    const uint8_t* source_b = (memory.lcdc_mask & (1u << source_bank)) ? memory.banks[source_bank] : nullptr;
    unsigned read = line * 256;
    if (((display >> 16) & 3) != 2) read += ((control_ >> 26) & 3) << 14;
    unsigned write = ((((control_ >> 18) & 3) << 14) + line * width) & 0xffff;
    const unsigned first = write, eva = (control_ & 31) > 16 ? 16 : control_ & 31;
    const unsigned ebv = (control_ >> 8) & 31, evb = ebv > 16 ? 16 : ebv;
    // Read and write sequentially: source and destination may be the same
    // physical bank, including overlapping offsets and wrap at 128 KiB.
    for (unsigned x = 0; x < width; ++x) {
        const uint16_t a = mode == 1 ? 0 : quantize(source_a[x]);
        const uint16_t b = mode == 0 ? 0 : use_fifo ? fifo[x] : source_b ? load(source_b, read+x) : 0;
        store(memory.banks[bank], write+x, mode == 0 ? a : mode == 1 ? b : blend(a, b, eva, evb));
    }
    if (memory.written) {
        const unsigned before_wrap = (0x10000-first) < width ? 0x10000-first : width;
        memory.written(memory.context, bank, first*2, before_wrap*2);
        if (before_wrap < width) memory.written(memory.context, bank, 0, (width-before_wrap)*2);
    }
    return {DisplayCaptureStatus::Captured, bank, first*2, width};
}
DisplayCaptureFrameResult DisplayCapture::frame(uint32_t display,uint16_t master,
                                                DisplayCaptureMemory& memory,
                                                const uint32_t* composite,const uint32_t* geometry,
                                                uint32_t* output,const uint16_t* fifo) {
    const unsigned mode=(display>>16)&3;
    const auto need=sources(0,false,memory.lcdc_mask);
    if((need.composite || (output && mode==1)) && !composite)
        return {DisplayCaptureStatus::MissingComposite};
    if(need.geometry && !geometry)return {DisplayCaptureStatus::MissingGeometry};
    if((need.fifo || (output && mode==3)) && !fifo)return {DisplayCaptureStatus::MissingFifo};
    DisplayCaptureFrameResult result{DisplayCaptureStatus::Inactive};
    for(unsigned line=0;line<192;++line){
        // Resolve display VRAM before any capture into the same physical row.
        if(output){
            const unsigned bank=(display>>18)&3;
            const uint8_t* bytes=(memory.lcdc_mask&(1u<<bank))?memory.banks[bank]:nullptr;
            for(unsigned x=0;x<256;++x){
                uint32_t color=0xffffffffu;
                if(mode==1)color=composite[line*256+x];
                else if(mode==2)color=display_color(bytes?load(bytes,line*256+x):0);
                else if(mode==3)color=display_color(fifo[line*256+x]);
                output[line*256+x]=color;
            }
        }
        const auto row=scanline(line,false,display,memory,
            composite?composite+line*256:nullptr,geometry?geometry+line*256:nullptr,
            fifo?fifo+line*256:nullptr);
        if(row.status==DisplayCaptureStatus::Captured){++result.captured_lines;result.status=row.status;}
        else if(row.status==DisplayCaptureStatus::DestinationUnmapped && !result.captured_lines)result.status=row.status;
        if(output && mode)for(unsigned x=0;x<256;++x)
            output[line*256+x]=master_color(output[line*256+x],master);
    }
    return result;
}
}
