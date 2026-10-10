#pragma once
#include <cstdint>
#include <cstddef>
// ARM9/ARM7 shared status ABI. Queue completion belongs to the IPC layer;
// publishing audio state must not acknowledge commands or overwrite padding.
struct Sm64dsSoundPlayerStatus {
    std::int16_t variables[16];
    std::uint32_t ticks;
};
struct Sm64dsSoundStatus {
    std::uint32_t completedBatches;
    std::uint32_t activePlayers;
    std::uint16_t enabledChannels;
    std::uint16_t activeCaptures;
    std::uint8_t reserved[0x14];
    Sm64dsSoundPlayerStatus players[16];
    std::int16_t globals[16];
};
static_assert(sizeof(Sm64dsSoundPlayerStatus)==0x24,"player status ABI");
static_assert(offsetof(Sm64dsSoundPlayerStatus,ticks)==0x20,"tick ABI");
static_assert(offsetof(Sm64dsSoundStatus,players)==0x20,"local variables ABI");
static_assert(offsetof(Sm64dsSoundStatus,globals)==0x260,"global variables ABI");
static_assert(sizeof(Sm64dsSoundStatus)==0x280,"shared status ABI");
