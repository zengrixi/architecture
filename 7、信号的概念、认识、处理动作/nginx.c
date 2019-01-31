

#include <stdio.h>
#include <unistd.h>

int main(int argc, char *const *argv)
{             
    printf("非常高兴，大家和老师一起学习《linux c++通讯架构实战》\n");

    //signal(SIGHUP,SIG_IGN); 我收到这个信号后，请操作系统不要干掉我；
    for(;;)
    {
        sleep(1); //休息1秒
        printf("休息1秒\n");
    }
    printf("程序退出，再见!\n");
    return 0;
}


