

#include <stdio.h>
#include <unistd.h>

//#include <signal.h>

int main(int argc, char *const *argv)
{       
    pid_t pid;    
    printf("非常高兴，大家和老师一起学习《linux c++通讯架构实战》\n");


    //系统函数，设置某个信号来的时候处理程序（用哪个函数处理）
    //signal(SIGHUP,SIG_IGN); //SIG_IGN标志：我要求忽略这个信号，请操作系统不要用缺省的处理方式来对待我（不要把我杀掉）；

    pid = fork(); //系统函数，用来创建新进程。子进程会从fork()调用之后开始执行
    if(pid < 0)
    {
        printf("fork()进程出错！\n");
    }
    else if(pid == 0)
    {
        //子进程这个条件会满足
        printf("子进程开始执行！\n");
        setsid(); //新建立一个不同的session,但是进程组组长调用setsid()是无效的
        for(;;)
        {
            sleep(1); //休息1秒
            printf("子进程休息1秒\n");
        }
        return 0;
    }
    else
    {
        //父进程会走到这里
        for(;;)
        {
            sleep(1); //休息1秒
            printf("父进程休息1秒\n");
        }
        return 0;
    }

    //for(;;)
    //{
    //    sleep(1); //休息1秒
    //    printf("休息1秒\n");
    //}
    printf("程序退出，再见!\n");
    return 0;
}

