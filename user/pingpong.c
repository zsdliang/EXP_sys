#include "kernel/types.h"
#include "user.h"

int main(int argc,char* argv[]){
    int p1[2];
    pipe(p1);
    int p2[2];
    pipe(p2);

    int a = fork();

    if(a < 0)
    {
        printf("error\n");
        exit(1);
    }

    if(a > 0)
    {
        char string[4];
        write(p1[1],"ping",4);
        read(p2[0],&string[0],4);
        printf("%d: received %s\n",getpid(),string);
    }
    else if(a == 0)
    {
        char string[4];
        read(p1[0],&string[0],4);
        
        printf("%d: received %s\n",getpid(),string);
        write(p2[1],"pong",4);
    }
    exit(0); //确保进程退出
}