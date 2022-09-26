#include "kernel/types.h"
#include "user.h"
int main(int argc,char* argv[]){
    int p[2];
    pipe(p);
    printf("%d  %d\n",p[0],p[1]);
    pipe(p);
    printf("%d  %d\n",p[0],p[1]);
    pipe(p);
    printf("%d  %d\n",p[0],p[1]);
    pipe(p);
    printf("%d  %d\n",p[0],p[1]);
    exit(0); //确保进程退出
}
