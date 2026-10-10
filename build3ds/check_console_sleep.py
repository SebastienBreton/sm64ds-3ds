#!/usr/bin/env python3
"""Exercise actual native sleep adapter with controlled APT/PTMU responses."""
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as d:
    d=Path(d);(d/'ntr').mkdir()
    (d/'3ds.h').write_text("""#pragma once
using Result=int;using u8=unsigned char;
#define R_FAILED(x) ((x)<0)
Result ptmuInit();void ptmuExit();Result PTMU_GetShellState(u8*);
bool aptIsSleepAllowed();void aptSetSleepAllowed(bool);Result APT_SleepIfShellClosed();
void aptHandleSleep();bool aptShouldClose();void svcSleepThread(long long);
""")
    (d/'ntr/mmio.h').write_text("""#pragma once
#include <cstdint>
extern unsigned short lid;extern unsigned ime;
namespace ntr {inline uint64_t io_read(uint32_t,unsigned){return ime;}
template<class T>T* xlate(void*){return reinterpret_cast<T*>(&lid);}}
""")
    (d/'harness.cpp').write_text("""#include <3ds.h>
#include <cassert>
#include <cstdio>
#include <cstring>
unsigned short lid=0x9234;unsigned ime=1;
bool allowed=true,opened=false;int polls=0,requests=0,waits=0,exits=0,changes=0;
Result ptmuInit(){return 0;}void ptmuExit(){++exits;}
Result PTMU_GetShellState(u8* out){*out=opened;return 0;}
bool aptIsSleepAllowed(){return allowed;}void aptSetSleepAllowed(bool v){allowed=v;++changes;}
Result APT_SleepIfShellClosed(){++requests;return 0;}
void aptHandleSleep(){if(++polls==3)opened=true;}
bool aptShouldClose(){return false;}void svcSleepThread(long long n){assert(n==1000000);++waits;}
extern "C" int func_0205f77c(int,int,int);
int main(int argc,char** argv){
 if(argc>1 && !std::strcmp(argv[1],"invalid")){func_0205f77c(0,0,0);return 9;}
 for(bool initial:{false,true}){
  allowed=initial;opened=false;polls=requests=waits=exits=changes=0;lid=0x9234;
  assert(func_0205f77c(0xc,0,0)==1);
  assert(opened && allowed==initial && polls==3 && requests==1 && waits==2 && exits==1);
  assert(changes==(initial?0:2) && lid==0x1234 && ime==1);
 }
 opened=true;polls=requests=waits=exits=changes=0;lid=0x9234;
 assert(func_0205f77c(0xc,0,0)==1 && polls==0 && requests==0 && exits==1 && lid==0x1234);
 std::puts("PASS mocked APT: asynchronous sleep request/wakeup, prior permission and IRQ preservation, open shell");
}
""".replace('#include <cstring>','#include <cstring>\n#include <initializer_list>'))
    source=root/'port/platform/3ds/console_sleep.cpp'
    exe=d/'check'
    subprocess.run(['c++','-std=c++17','-I',str(d),str(source),str(d/'harness.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    result=subprocess.run([str(exe),'invalid'],capture_output=True,text=True)
    assert result.returncode!=0 and 'unsupported wake mode' in result.stderr
    print('PASS unsupported wake mode fails explicitly; physical sleep is not covered by mocks')
