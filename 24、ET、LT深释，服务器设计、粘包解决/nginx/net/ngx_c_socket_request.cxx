
//和网络  中 客户端请求数据有关的代码
/*
王健伟老师 《Linux C++通讯架构实战》
商业级质量的代码，完整的项目，帮你提薪至少10K
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
//#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"

//来数据时候的处理，当连接上有数据来的时候，本函数会被ngx_epoll_process_events()所调用  ,官方的类似函数为ngx_http_wait_request_handler();
void CSocekt::ngx_wait_request_handler(lpngx_connection_t c)
{  
    //ngx_log_stderr(errno,"22222222222222222222222.");
    /*
    //ET测试代码
    unsigned char buf[10]={0};
    memset(buf,0,sizeof(buf));    
    do
    {
        int n = recv(c->fd,buf,2,0); //每次只收两个字节    
        if(n == -1 && errno == EAGAIN)
            break; //数据收完了
        else if(n == 0)
            break; 
        ngx_log_stderr(0,"OK，收到的字节数为%d,内容为%s",n,buf);
    }while(1);*/

    //LT测试代码
    /*unsigned char buf[10]={0};
    memset(buf,0,sizeof(buf));  
    int n = recv(c->fd,buf,2,0);
    if(n  == 0)
    {
        //连接关闭
        ngx_free_connection(c);
        close(c->fd);
        c->fd = -1;
    }
    ngx_log_stderr(0,"OK，收到的字节数为%d,内容为%s",n,buf);
    */
   

    
   
    


    /*
    ngx_epoll_add_event(c->fd,                 //socket句柄
                                1,0,              //读，写 ,这里读为1，表示客户端应该主动给我服务器发送消息，我服务器需要首先收到客户端的消息；
                                EPOLLET,          //其他补充标记【EPOLLET(高速模式，边缘触发ET)】
                                EPOLL_CTL_MOD,    //事件类型【增加，还有删除/修改】                                    
                                c              //连接池中的连接
                                );
    */ 
   



    return;
}