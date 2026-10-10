#include "ntr/rt.h"
#include "ntr/mmio.h"
#include <cstdio>
#include <cstdint>
extern "C" {
void* data_020aa3f0=nullptr;
void func_020731dc(void*,void*,void**);
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
}
static void *main_node[3],*a[3],*b[3],*c[3],*d[3],*e[3],*null_node[3];
static int trace[16],count,failures;
static bool expect_masked;
static void record(void* value) {
    const int id=static_cast<int>(reinterpret_cast<uintptr_t>(value));
    if(count<16) trace[count++]=id;else ++failures;
    if(ntr::rt_irq_masked()!=expect_masked)++failures;
    if(id==3)func_020731dc(reinterpret_cast<void*>(4),reinterpret_cast<void*>(record),d);
}
static void add(int id,void** node) {func_020731dc(reinterpret_cast<void*>(static_cast<uintptr_t>(id)),reinterpret_cast<void*>(record),node);}
#define CHECK(x) do {if(!(x)){++failures;std::printf("FAIL: %s\n",#x);}} while(0)
int main() {
    if(!ntr::io_init())return 2;
    ntr::rt_irq_enable();
    add(0,main_node);
    CHECK(sm64ds_set_overlay_scope(9)==-1);add(1,a);
    CHECK(sm64ds_set_overlay_scope(100)==9);add(2,b);
    sm64ds_set_overlay_scope(9);add(3,c);
    func_020731dc(nullptr,nullptr,null_node);
    sm64ds_set_overlay_scope(100);
    CHECK(sm64ds_destroy_overlay_resources(9)==4);
    CHECK(count==3 && trace[0]==3 && trace[1]==1 && trace[2]==4);
    CHECK(data_020aa3f0==b && b[0]==main_node && main_node[0]==nullptr);
    CHECK(sm64ds_destroy_overlay_resources(9)==0);
    add(5,e); // Restored caller scope must be100.
    expect_masked=true;ntr::rt_irq_disable();
    CHECK(sm64ds_destroy_overlay_resources(100)==2);
    CHECK(ntr::rt_irq_masked());
    CHECK(count==5 && trace[3]==5 && trace[4]==2);
    CHECK(data_020aa3f0==main_node && main_node[0]==nullptr);
    // Reuse an already destroyed registration node on another load.
    sm64ds_set_overlay_scope(9);add(6,a);
    CHECK(sm64ds_destroy_overlay_resources(9)==1);
    CHECK(count==6 && trace[5]==6 && data_020aa3f0==main_node);
    sm64ds_set_overlay_scope(-1);ntr::rt_irq_enable();
    std::printf("smoke_overlay_destructors: %s callbacks=%d failures=%d order=3,1,4,5,2,6\n",failures?"FAIL":"PASS",count,failures);
    return failures?1:0;
}
