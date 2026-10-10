// Original entrance wipe operation with the shared native FaderWipe ABI.
#include "dScene_c.h"
#include "FaderWipe.h"
extern "C" FaderWipe *data_0209f324;
extern "C" void StartEntranceFaderWipe(int index) {
    FaderWipe *f=&data_0209f324[index];
    dScene_c::SetFaders(f);
    f->SetToEnd();
}

extern "C" void StartExitFaderWipe(int index) {
    FaderWipe *f=&data_0209f324[index];
    dScene_c::SetFaders(f);
    f->SetToStart();
}
