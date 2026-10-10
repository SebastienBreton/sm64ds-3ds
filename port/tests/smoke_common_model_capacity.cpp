#include "ntr/mmio.h"
#include "Heap.h"
#include "Model.h"
#include <cstdio>
extern "C" {
extern int data_0209cef8[];extern int data_0209cefc[];extern int data_0208e738[];
extern unsigned data_020a4b98[];
void CleanCommonModelDataArr();
}
#define CHECK(c) do{if(!(c)){std::printf("model capacity FAIL line%d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(){
 CHECK(ntr::io_init());CHECK(Heap::SetupRootHeap());
 static BMD_File files[100];data_0209cef8[0]=0;data_0208e738[0]=0;
 for(unsigned i=0;i<4;++i)data_020a4b98[i]=0x12345670+i;
 for(unsigned i=0;i<100;++i){
   auto* record=Model::AddToCommonModelDataArr(files[i]);
   CHECK(record==data_0209cefc+i*3);CHECK(data_0209cef8[0]==int(i+1));
   for(unsigned j=0;j<4;++j)CHECK(data_020a4b98[j]==0x12345670+j);
 }
 for(unsigned i=0;i<100;++i){
   CHECK(reinterpret_cast<BMD_File*>(data_0209cefc[i*3])==files+i);
   CHECK(Model::AddToCommonModelDataArr(files[i])==data_0209cefc+i*3);
 }
 CHECK(data_0209cef8[0]==100);CleanCommonModelDataArr();CHECK(data_0209cef8[0]==0);
 std::puts("smoke_common_model_capacity: PASS 100 ROM entries, duplicate reuse, scheduler canary, cleanup");return 0;
}
