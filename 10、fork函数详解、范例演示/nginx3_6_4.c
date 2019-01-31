

#include <stdio.h>
#include <stdlib.h>  //malloc,exit
#include <unistd.h>  //fork
#include <signal.h>

int g_mygbltest = 0;
int main(int argc, char *const *argv)
{
    pid_t  pid;
    printf("进程开始执行!\n");   
    //---------------------------------
    pid = fork();  //创建一个子进程

    //要判断子进程是否创建成功
    if(pid < 0)
    {
        printf("子进程创建失败，很遗憾!\n");
        exit(1);
    }

    //现在，父进程和子进程同时开始 运行了 
    //for(;;)
    //{        
    //    sleep(1); //休息1秒
    //    printf("休息1秒，进程id=%d!\n",getpid());
    //}
    //printf("再见了!\n");

    //走到这里，fork()成功，执行后续代码的可能是父进程，也可能是子进程
    if(pid == 0)
    {
        //子进程，因为子进程的fork()返回值会是0；
        //这里专门针对子进程的处理代码
        while(1)
        {
            g_mygbltest++;
            sleep(1); //休息1秒
            printf("真是太高兴了，我是子进程的，我的进程id=%d,g_mygbltest=%d!\n",getpid(),g_mygbltest);
        }
    }
    else
    {
        //这里就是父进程，因为父进程的fork()返回值会 > 0（实际返回的是子进id程）
        //这是专门针对父进程的处理代码
        while(1)
        {
            g_mygbltest++;
            sleep(5); //休息5秒
            printf("......。。，我是父进程的，我的进程id=%d,g_mygbltest=%d!\n",getpid(),g_mygbltest);
        }
    }

    return 0;
}


