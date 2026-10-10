#pragma once
// Cumulative guest-tick counters of the presentation path (castle-30fps).
// Written by the main thread and the core-1 presentation worker; read and
// reset by diagnostic mains between frames. Each slot is owned by one thread.
#include <cstdint>
extern "C" volatile uint64_t sm64ds_frame_prof[48];
namespace frame_profile {
enum Slot : unsigned {
    MainWaitIdle=0,MainPrepareB=1,MainCompiledFrame=2,MainPreparePpu=3,MainPresentTotal=4,
    WorkerMesh=5,WorkerClassify=6,WorkerMeshPrepare=7,WorkerCore=8,MainObjPrepare=9,
    MainSynchronous=10,WorkerTextures=11,WorkerDraw=12,WorkerCompose=13,WorkerFrameBegin=14,MainPpuA=15,MainHeldCheck=16,GameFsRead=20,GameFsLoad=21,GameFsOpen=22,ClassifyCross=23,ClassifyGroups=24,ClassifyTriangles=25,ClassifyBulk=26,Count=32
};
uint64_t ticks();
struct Scope {
    unsigned slot;uint64_t start;
    explicit Scope(unsigned s):slot(s),start(ticks()){}
    ~Scope(){sm64ds_frame_prof[slot]+=ticks()-start;}
};
}
