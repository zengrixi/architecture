
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>


#define SERV_PORT 9000    //要连接到的服务器端口，服务器必须在这个端口上listen着

int main(int argc, char *const *argv)
{    
    //这些演示代码的写法都是固定套路，一般都这么写
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); //创建客户端的socket，大家可以暂时不用管这里的参数是什么，知道这个函数大概做什么就行

    struct sockaddr_in serv_addr; 
    memset(&serv_addr,0,sizeof(serv_addr));

    //设置要连接到的服务器的信息
    serv_addr.sin_family = AF_INET;                //选择协议族为IPV4
    serv_addr.sin_port = htons(SERV_PORT);         //连接到的服务器端口，服务器监听这个地址
    //这里为了方便演示，要连接的服务器地址固定写
    if(inet_pton(AF_INET,"192.168.1.126",&serv_addr.sin_addr) <= 0)  //IP地址转换函数,把第二个参数对应的ip地址转换第三个参数里边去，固定写法
    {
        printf("调用inet_pton()失败，退出！\n");
        exit(1);
    }

    //连接到服务器
    if(connect(sockfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr)) < 0)
    {
        printf("调用connect()失败，退出！\n");
        exit(1);
    }

    int n;
    char recvline[1000 + 1]; 
    while(( n = read(sockfd,recvline,1000)) > 0) //仅供演示，非商用，所以不检查收到的宽度，实际商业代码，不可以这么写
    {
        recvline[n] = 0; //实际商业代码要判断是否收取完毕等等，所以这个代码只有学习价值，并无商业价值
        printf("收到的内容为：%s\n",recvline);
    }
    close(sockfd); //关闭套接字
    printf("程序执行完毕，退出!\n");
    return 0;
}





