#pragma once
#include "sseq_decoder.h"
#include "sound_status.h"
#include <cstdint>
// Synchronous command executor with a shared hardware-sized mixer.
// IPC acknowledgement is separate; unsupported commands must not be acknowledged.
class Sm64dsSoundCommands {
    Sm64dsMixer* mixer=nullptr;
    Sm64dsAlarmCallback alarmCallback=nullptr;
    void* alarmContext=nullptr;
    Sm64dsSoundBuffer resources[256]{};
    const char* failure="";
    Sm64dsSoundBuffer find(std::uintptr_t address) const;
public:
    ~Sm64dsSoundCommands();
    Sm64dsSoundCommands()=default;
    Sm64dsSoundCommands(const Sm64dsSoundCommands&)=delete;
    Sm64dsSoundCommands& operator=(const Sm64dsSoundCommands&)=delete;
    bool registerResource(Sm64dsSoundBuffer);
    void unregisterResource(const void*);
    bool execute(unsigned command,const std::uintptr_t args[4]);
    bool render(std::int16_t* stereo,unsigned frames);
    // Unlocked game-thread pre-pass for a queued start (command 0) and the
    // matching release after the batch executed. See sm64ds_mixer_prepare.
    void prepare(unsigned command,const std::uintptr_t args[4]);
    void releasePrepared(){sm64ds_mixer_release_prepared(mixer);}
    void setAlarmCallback(Sm64dsAlarmCallback,void*);
    void collect(){sm64ds_mixer_collect(mixer);}
    unsigned activeMask() const;
    void publishStatus(Sm64dsSoundStatus&) const;
    bool ticks(unsigned player,std::uint32_t* value) const;
    bool variable(unsigned player,unsigned index,std::int16_t* value) const;
    const char* error() const { return failure; }
};

// Original32-bit IPC node ABI. The caller owns initialized status and node pool.
struct Sm64dsSoundNode {
    std::uint32_t next,command,args[4];
};
static_assert(sizeof(Sm64dsSoundNode)==24,"sound command node ABI");
class Sm64dsSoundQueue {
    Sm64dsSoundCommands& service;
    Sm64dsSoundNode* pool;
    unsigned count;
    Sm64dsSoundStatus& status;
    bool bound=false,failed=false;
    char failureDetails[96]{};
    const char* failure="";
public:
    Sm64dsSoundQueue(Sm64dsSoundCommands& s,Sm64dsSoundNode* nodes,unsigned size,Sm64dsSoundStatus& shared)
        :service(s),pool(nodes),count(size),status(shared){}
    // Synchronous game-thread execution. Runtime failure latches: never replay
    // a partially executed batch. IPC callers must report it as a fatal error.
    bool submit(std::uintptr_t head);
    // Read-only walk of a not-yet-submitted list; never executes commands.
    void prepare(std::uintptr_t head);
    bool refresh();
    const char* error() const{return failure;}
};
