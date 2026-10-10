#include <cstdio>
extern "C" int func_0205f9ac(int,int);
extern "C" int func_0205f8e0(int*,int*);
int main() {
    const int commands[][4]={{2,0,0,0},{0,1,1,0},{1,1,1,1},
                            {0,0,0,1},{1,0,0,0},{2,1,1,1}};
    int failure=0;
    for (const auto& command:commands) {
        int top=-1,bottom=-1;
        const int write=func_0205f9ac(command[0],command[1]);
        const int read=func_0205f8e0(&top,&bottom);
        std::printf("backlight screen=%d enabled=%d result=%08x read=%d state=%d,%d\n",
                    command[0],command[1],write,read,top,bottom);
        if (write || read || top!=command[2] || bottom!=command[3]) { failure=1;break; }
    }
    if(func_0205f9ac(3,1)!=0xffff || func_0205f9ac(0,2)!=0xffff) failure=1;
    const int restored=func_0205f9ac(2,1);
    if (restored) failure=1;
    std::puts("Backlight service and tracked state checked; physical luminance not measured");
    return failure;
}
