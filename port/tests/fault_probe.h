// Shared crash probe for the gate smokes: prints the module-relative fault
// address and a frame-pointer backtrace, resolvable against the /MAP file.
#ifndef PORT_FAULT_PROBE_H
#define PORT_FAULT_PROBE_H

#if defined(__3DS__)
// No SEH on the 3DS; Azahar logs unmapped accesses with their PC instead.
#define PORT_INSTALL_FAULT_PROBE() ((void)0)
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static LONG WINAPI port_fault_probe(EXCEPTION_POINTERS *ep)
{
    char *base = (char *)GetModuleHandleA(0);
    fprintf(stderr, "FAULT code %08lx at +0x%08x accessing %08x\n",
            ep->ExceptionRecord->ExceptionCode,
            (unsigned)((char *)ep->ExceptionRecord->ExceptionAddress - base),
            (unsigned)(ep->ExceptionRecord->NumberParameters > 1
                       ? ep->ExceptionRecord->ExceptionInformation[1] : 0));
    void *frames[12];
    unsigned n = CaptureStackBackTrace(0, 12, frames, 0);
    for (unsigned i = 0; i < n; ++i)
        fprintf(stderr, "  frame %u: +0x%08x\n", i,
                (unsigned)((char *)frames[i] - base));
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

#define PORT_INSTALL_FAULT_PROBE() SetUnhandledExceptionFilter(port_fault_probe)
#endif

#endif
