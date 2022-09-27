#include "kernel/types.h"
#include "user.h"
int main(int argc,char* argv[]){
    int i = 2;
    int pNum;
    int p[2];
    int p2[2];
    int p_temp = 0;
    int counter = 0;
    int state;

    pipe(p);
    pNum = fork();
    if(pNum > 0)
    {
        for(i = 2;i <= 35;i++)
        {
            write(p[1],&i,4);
        }
        i = -1;
        write(p[1],&i,4);
        wait(&state);
    }
    else if(pNum == 0)
    {
        int firstNum;
        int num;
        read(p[0],&firstNum,4);
        num = firstNum;
        while(firstNum > 0)
        {   
            counter++;
            printf("prime %d\n",firstNum);
            pipe(p2);
            pNum = fork();
            if(pNum > 0)
            {
                while(num > 0)
                {
                    if(firstNum == 2)
                    {
                        read(p[0],&num,4);   
                    }
                    else
                    {
                        read(p_temp,&num,4);
                    }
                    if(num % firstNum != 0 || num == -1)
                    {
                        write(p2[1],&num,4);
                    }
                }
            }
            else if(pNum == 0)
            {
                read(p2[0],&firstNum,4);
                p_temp = p2[0];
            }
            if(num == -1)
            {
                break;
            }
        }
        wait(&state);
    }
    exit(0);
}