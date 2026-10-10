#pragma once
#include "sound_commands.h"
struct Sm64dsSoundOutputState;
// Call on the game thread once per frame (and after long synchronous loads).
// Decoder work never runs on the NDSP worker. The command service must outlive us.
class Sm64dsSoundOutput {
    static constexpr unsigned Frames=512, Count=3;
    Sm64dsSoundCommands* service=nullptr;
    Sm64dsSoundOutputState* state=nullptr;
    bool initialized=false;
    void* serviceLock=nullptr;
    unsigned submitted=0,completed=0;
    char failure[256]{};
public:
    Sm64dsSoundOutput()=default;
    ~Sm64dsSoundOutput(){close();}
    Sm64dsSoundOutput(const Sm64dsSoundOutput&)=delete;
    Sm64dsSoundOutput& operator=(const Sm64dsSoundOutput&)=delete;
    bool open(Sm64dsSoundCommands&);
    bool pump();
    // Optional RecursiveLock* guarding the command service; each render chunk
    // takes it, so a concurrent game-thread command waits at most one chunk.
    void setLock(void* lock){serviceLock=lock;}
    void close();
    unsigned submittedBuffers() const{return submitted;}
    unsigned completedBuffers() const{return completed;}
    const char* error() const{return failure;}
};
