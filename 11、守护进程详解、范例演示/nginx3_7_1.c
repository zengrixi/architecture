#include <stdio.h>
#include <stdlib.h>  //malloc
#include <unistd.h>
#include <signal.h>

int main(int argc, char *const *argv)
{  
    printf("进程开始执行!\n");
    write(STDOUT_FILENO,"aaaabbb",6);
    
    for(;;)
    {        
        sleep(1); //休息1秒
        //printf("休息1秒，进程id=%d!\n",getpid()); 
    }
    printf("再见了!\n");
    return 0;
}




