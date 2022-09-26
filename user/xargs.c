#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc, char *argv[])
{
    int i;
    int state;
    char *new_argv[8];
    char *input = malloc(sizeof(char)*512);
    for(i = 1;i < argc;i++)
    {
        new_argv[i-1] = argv[i];
    }
    while(1)
    {
        gets(input,512);
        *(input+strlen(input)-1) = '\0'; //去\n
        if(strcmp(input,"end") == 0||*input == 0)
        {
            break;
        }
        new_argv[i-1] = input;
        if(fork() == 0)
        {
            exec(argv[1],new_argv);
        }
        else
        {
            wait(&state);
        }   
    }
	exit(0);
}