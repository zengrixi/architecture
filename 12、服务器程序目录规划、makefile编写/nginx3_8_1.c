

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>


//信号处理函数
void sig_usr(int signo)
{         
    if(signo == SIGUSR1)
    {
        printf("收到了SIGUSR1信号，我休息10秒......!\n");
        sleep(10);
        printf("收到了SIGUSR1信号，我休息10秒完毕，苏醒了......!\n");
    }
    else if(signo == SIGUSR2)
    {
        printf("收到了SIGUSR2信号，我休息10秒......!\n");
        sleep(10);
        printf("收到了SIGUSR2信号，我休息10秒完毕，苏醒了......!\n");
    }
    else
    {
        printf("收到了未捕捉的信号%d!\n",signo);
    }
}

int main(int argc, char *const *argv)
{
    if(signal(SIGUSR1,sig_usr) == SIG_ERR)  //系统函数，参数1：是个信号，参数2：是个函数指针，代表一个针对该信号的捕捉处理函数
    {
        printf("无法捕捉SIGUSR1信号!\n");
    }
    if(signal(SIGUSR2,sig_usr) == SIG_ERR) 
    {
        printf("无法捕捉SIGUSR2信号!\n");
    }
    
    for(;;)
    {
        sleep(1); //休息1秒    
        printf("休息1秒~~~~!\n");
    }
    printf("再见!\n");
    return 0;
}


