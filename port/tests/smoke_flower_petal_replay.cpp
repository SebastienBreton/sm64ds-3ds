#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstring>
#include "flower_petal_fixture.h"
extern "C" {
void func_ov006_020c35e8(char*);
void func_ov006_020c3528(char*);
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(!ntr::io_init() || !Heap::SetupRootHeap())return 2;
 unsigned failures=0;
 for(unsigned test=0;test<7;++test){
 alignas(8) char petal[0x98];std::memcpy(petal,beforePetal[test],sizeof petal);
 func_ov006_020c35e8(petal);
 if(*reinterpret_cast<int*>(petal+0x40))func_ov006_020c3528(petal);
 // Model pointers are DS addresses and deliberately never dereferenced here.
 // Compare all evolving physics/quaternion fields, the rendered matrix and life.
 for(unsigned i=0;i<0x98;++i){
  if(i>=0x44 && i<0x60)continue;
  if(i>=0x90 && i<0x94)continue;
  if(static_cast<unsigned char>(petal[i])!=afterPetal[test][i]){
   ++failures;std::printf("petal mismatch case%u +%02x native=%02x DS=%02x\n",test,i,(unsigned char)petal[i],afterPetal[test][i]);
  }
 }
 }
 std::printf("smoke_flower_petal_replay: %s cases=7 mismatched_bytes=%u\n",failures?"FAIL":"PASS",failures);
 return failures?1:0;
}
