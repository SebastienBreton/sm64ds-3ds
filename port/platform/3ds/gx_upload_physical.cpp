// Retail SDK upload windows for the opt-in physical VRAM bus. Isolated model
// gates retain the flat legacy windows supplied by hal/model_host.cpp.
#include "ntr/vram_bus.h"
#include <cstdint>

extern "C" {
std::uint16_t func_0205417c();
std::uint16_t func_02054168();
void func_02059fa8(int channel);
void _ZN2GX13SetBankForTexEt(std::uint16_t banks);
void _ZN2GX17SetBankForTexPlttEt(std::uint16_t banks);
extern std::uint32_t data_020a60ac, data_020a60bc, data_020a60c0;
extern std::uint32_t data_020a60b0;
extern int data_02099fd0;
std::uint32_t data_020a60b4, data_020a60b8;
}

namespace {
// Halfwords at retail ARM9 02086314 and 02086324. Texture masks describe
// logical consecutive slots backed by A-D; gaps require a second CPU window.
constexpr std::uint16_t palette_windows[8] = {
    0, 0x6880, 0x6890, 0x6880, 0x6894, 0, 0x6890, 0x6880
};
constexpr std::uint16_t texture_windows[16][3] = {
    {0,0,0}, {0x6800,0,0}, {0x6820,0,0}, {0x6800,0,0},
    {0x6840,0,0}, {0x6800,0x6840,0x20}, {0x6820,0,0}, {0x6800,0,0},
    {0x6860,0,0}, {0x6800,0x6860,0x20}, {0x6820,0x6860,0x20}, {0x6800,0x6860,0x40},
    {0x6840,0,0}, {0x6800,0x6840,0x20}, {0x6820,0,0}, {0x6800,0,0}
};
}

extern "C" void _ZN2GX12BeginLoadTexEv()
{
    if (!ntr::io_vram_banks()) return;
    const auto banks = func_0205417c(); // move texture banks to LCDC
    data_020a60b8 = banks;
    data_020a60ac = std::uint32_t(texture_windows[banks][0]) << 12;
    data_020a60bc = std::uint32_t(texture_windows[banks][1]) << 12;
    data_020a60c0 = std::uint32_t(texture_windows[banks][2]) << 12;
}

extern "C" void _ZN2GX10EndLoadTexEv()
{
    if (!ntr::io_vram_banks()) return;
    if (data_02099fd0 != -1) func_02059fa8(data_02099fd0);
    _ZN2GX13SetBankForTexEt(std::uint16_t(data_020a60b8));
    data_020a60c0 = 0;
    data_020a60bc = 0;
    data_020a60ac = 0;
    data_020a60b8 = 0;
}

extern "C" void _ZN2GX16BeginLoadTexPlttEv()
{
    if (!ntr::io_vram_banks()) return;
    const auto banks = func_02054168();
    data_020a60b4 = banks;
    data_020a60b0 = std::uint32_t(palette_windows[banks >> 4]) << 12;
}

extern "C" void _ZN2GX14EndLoadTexPlttEv()
{
    if (!ntr::io_vram_banks()) return;
    if (data_02099fd0 != -1) func_02059fa8(data_02099fd0);
    _ZN2GX17SetBankForTexPlttEt(std::uint16_t(data_020a60b4));
    data_020a60b4 = 0;
    data_020a60b0 = 0;
}
