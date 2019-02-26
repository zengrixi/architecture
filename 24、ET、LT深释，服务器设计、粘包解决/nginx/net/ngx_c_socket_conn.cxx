
//和网络 中 连接/连接池 有关的函数放这里
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

//从连接池中获取一个空闲连接【当一个客户端连接TCP进入，我希望把这个连接和我的 连接池中的 一个连接【对象】绑到一起，后续 我可以通过这个连接，把这个对象拿到，因为对象里边可以记录各种信息】
lpngx_connection_t CSocekt::ngx_get_connection(int isock)
{
    lpngx_connection_t  c = m_pfree_connections; //空闲连接链表头

    if(c == NULL)
    {
        //系统应该控制连接数量，防止空闲连接被耗尽，能走到这里，都不正常
        ngx_log_stderr(0,"CSocekt::ngx_get_connection()中空闲链表为空,这不应该!");
        return NULL;
    }

    m_pfree_connections = c->data;                       //指向连接池中下一个未用的节点
    m_free_connection_n--;                               //空闲连接少1
    
    //(1)注意这里的操作,先把c指向的对象中有用的东西搞出来保存成变量，因为这些数据可能有用
    uintptr_t  instance = c->instance;   //常规c->instance在刚构造连接池时这里是1【失效】
    uint64_t iCurrsequence = c->iCurrsequence;
    //....其他内容再增加


    //(2)把以往有用的数据搞出来后，清空并给适当值
    memset(c,0,sizeof(ngx_connection_t));                //注意，类型不要用成lpngx_connection_t，否则就出错了
    c->fd = isock;                                       //套接字要保存起来，这东西具有唯一性    
    //....其他内容再增加

    //(3)这个值有用，所以在上边(1)中被保留，没有被清空，这里又把这个值赋回来
    c->instance = !instance;                            //抄自官方nginx，到底有啥用，以后再说【分配内存时候，连接池里每个连接对象这个变量给的值都为1，所以这里取反应该是0【有效】；】
    c->iCurrsequence=iCurrsequence;++c->iCurrsequence;  //每次取用该值都增加1

    //wev->write = 1;  这个标记有没有 意义加，后续再研究
    return c;    
}

//归还参数c所代表的连接到到连接池中，注意参数类型是lpngx_connection_t
void CSocekt::ngx_free_connection(lpngx_connection_t c) 
{
    c->data = m_pfree_connections;                       //回收的节点指向原来串起来的空闲链的链头

    //节点本身也要干一些事
    ++c->iCurrsequence;                                  //回收后，该值就增加1,以用于判断某些网络事件是否过期【一被释放就立即+1也是有必要的】

    m_pfree_connections = c;                             //修改 原来的链头使链头指向新节点
    ++m_free_connection_n;                               //空闲连接多1    
    return;
}

