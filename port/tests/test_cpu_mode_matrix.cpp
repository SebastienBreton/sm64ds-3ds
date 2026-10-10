// Standalone CPU audit must load run.py's guest env file explicitly.
#define main matrix_benchmark_main
#include "test_fixed_direction_matrix.cpp"
#undef main
#include <cstdlib>
extern char** environ;
int main(){
    static char* empty[]={nullptr};environ=empty;
    if(auto* settings=std::fopen("sdmc:/sm64ds/env.txt","r")){
        char line[512];
        while(std::fgets(line,sizeof line,settings)){
            line[std::strcspn(line,"\r\n")]=0;
            if(auto* equal=std::strchr(line,'=')){*equal=0;setenv(line,equal+1,1);}
        }
        std::fclose(settings);
    }
    return matrix_benchmark_main();
}
