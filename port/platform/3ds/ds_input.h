#pragma once
#include <cstdint>

namespace ds_input {
// libctru's first twelve buttons match the DS. Its digital Circle Pad
// directions (bits 28..31) feed the same DS directions (bits 4..7).
// Leave opposite-direction filtering to the original func_0203bc7c.
// This preserves the DS control modes (menus, dialogs, DS-mode movement).
// Analog Circle Pad movement is layered on top in analog_pad.cpp.
constexpr std::uint16_t buttons(std::uint32_t held) {
    return static_cast<std::uint16_t>((held & 0x0fff) |
                                      ((held >> 24) & 0x00f0));
}
}
