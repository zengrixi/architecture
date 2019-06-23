
//和网络 中 连接/连接池 有关的函数放这里
/*
公众号：程序员速成     q群：716480601
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
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

//---------------------------------------------------------------
//连接池成员函数
ngx_connection_s::ngx_connection_s()//构造函数
{		
    iCurrsequence = 0;    
    pthread_mutex_init(&logicPorcMutex, NULL); //互斥量初始化
}
ngx_connection_s::~ngx_connection_s()//析构函数
{
    pthread_mutex_destroy(&logicPorcMutex);    //互斥量释放
}
//分配出去一个连接的时候初始化一些内容,原来内容放在 ngx_get_connection()里，现在放在这里
void ngx_connection_s::GetOneToUse()
{
    ++iCurrsequence;

    fd  = -1;                                         //开始先给-1
    curStat = _PKG_HD_INIT;                           //收包状态处于 初始状态，准备接收数据包头【状态机】
    precvbuf = dataHeadInfo;                          //收包我要先收到这里来，因为我要先收包头，所以收数据的buff直接就是dataHeadInfo
    irecvlen = sizeof(COMM_PKG_HEADER);               //这里指定收数据的长度，这里先要求收包头这么长字节的数据
    
    precvMemPointer   = NULL;                         //既然没new内存，那自然指向的内存地址先给NULL
    iThrowsendCount   = 0;                            //原子的
    psendMemPointer   = NULL;                         //发送数据头指针记录
    events            = 0;                            //epoll事件先给0 
    lastPingTime      = time(NULL);                   //上次ping的时间

    FloodkickLastTime = 0;                            //Flood攻击上次收到包的时间
	FloodAttackCount  = 0;	                          //Flood攻击在该时间内收到包的次数统计
    iSendCount        = 0;                            //发送队列中有的数据条目数，若client只发不收，则可能造成此数过大，依据此数做出踢出处理 
}

//回收回来一个连接的时候做一些事
void ngx_connection_s::PutOneToFree()
{
    ++iCurrsequence;   
    if(precvMemPointer != NULL)//我们曾经给这个连接分配过接收数据的内存，则要释放内存
    {        
        CMemory::GetInstance()->FreeMemory(precvMemPointer);
        precvMemPointer = NULL;        
    }
    if(psendMemPointer != NULL) //如果发送数据的缓冲区里有内容，则要释放内存
    {
        CMemory::GetInstance()->FreeMemory(psendMemPointer);
        psendMemPointer = NULL;
    }

    iThrowsendCount = 0;                              //设置不设置感觉都行         
}

//---------------------------------------------------------------
//初始化连接池
void CSocekt::initconnection()
{
    lpngx_connection_t p_Conn;
    CMemory *p_memory = CMemory::GetInstance();   

    int ilenconnpool = sizeof(ngx_connection_t);    
    for(int i = 0; i < m_worker_connections; ++i) //先创建这么多个连接，后续不够再增加
    {
        p_Conn = (lpngx_connection_t)p_memory->AllocMemory(ilenconnpool,true); //清理内存 , 因为这里分配内存new char，无法执行构造函数，所以如下：
        //手工调用构造函数，因为AllocMemory里无法调用构造函数
        p_Conn = new(p_Conn) ngx_connection_t();  //定位new【不懂请百度】，释放则显式调用p_Conn->~ngx_connection_t();		
        p_Conn->GetOneToUse();
        m_connectionList.push_back(p_Conn);     //所有链接【不管是否空闲】都放在这个list
        m_freeconnectionList.push_back(p_Conn); //空闲连接会放在这个list
    } //end for
    m_free_connection_n = m_total_connection_n = m_connectionList.size(); //开始这两个列表一样大
    return;
}

//最终回收连接池，释放内存
void CSocekt::clearconnection()
{
    lpngx_connection_t p_Conn;
	CMemory *p_memory = CMemory::GetInstance();
	
	while(!m_connectionList.empty())
	{
		p_Conn = m_connectionList.front();
		m_connectionList.pop_front(); 
        p_Conn->~ngx_connection_t();     //手工调用析构函数
		p_memory->FreeMemory(p_Conn);
	}
}

//从连接池中获取一个空闲连接【当一个客户端连接TCP进入，我希望把这个连接和我的 连接池中的 一个连接【对象】绑到一起，后续 我可以通过这个连接，把这个对象拿到，因为对象里边可以记录各种信息】
lpngx_connection_t CSocekt::ngx_get_connection(int isock)
{
    //因为可能有其他线程要访问m_freeconnectionList，m_connectionList【比如可能有专门的释放线程要释放/或者主线程要释放】之类的，所以应该临界一下
    CLock lock(&m_connectionMutex);  

    if(!m_freeconnectionList.empty())
    {
        //有空闲的，自然是从空闲的中摘取
        lpngx_connection_t p_Conn = m_freeconnectionList.front(); //返回第一个元素但不检查元素存在与否
        m_freeconnectionList.pop_front();                         //移除第一个元素但不返回	
        p_Conn->GetOneToUse();
        --m_free_connection_n; 
        p_Conn->fd = isock;
        return p_Conn;
    }

    //走到这里，表示没空闲的连接了，那就考虑重新创建一个连接
    CMemory *p_memory = CMemory::GetInstance();
    lpngx_connection_t p_Conn = (lpngx_connection_t)p_memory->AllocMemory(sizeof(ngx_connection_t),true);
    p_Conn = new(p_Conn) ngx_connection_t();
    p_Conn->GetOneToUse();
    m_connectionList.push_back(p_Conn); //入到总表中来，但不能入到空闲表中来，因为凡是调这个函数的，肯定是要用这个连接的
    ++m_total_connection_n;             
    p_Conn->fd = isock;
    return p_Conn;

    //因为我们要采用延迟释放的手段来释放连接，因此这种 instance就没啥用，这种手段用来处理立即释放才有用。

    /*
    lpngx_connection_t  c = m_pfree_connections; //空闲连接链表头

    if(c == NULL)
    {
        //系统应该控制连接数量，防止空闲连接被耗尽，能走到这里，都不正常
        ngx_log_stderr(0,"CSocekt::ngx_get_connection()中空闲链表为空,这不应该!");
        return NULL;
    }

    m_pfree_connections = c->next;                       //指向连接池中下一个未用的节点
    m_free_connection_n--;                               //空闲连接少1
    
    //(1)注意这里的操作,先把c指向的对象中有用的东西搞出来保存成变量，因为这些数据可能有用
    uintptr_t  instance = c->instance;            //常规c->instance在刚构造连接池时这里是1【失效】
    uint64_t   iCurrsequence = c->iCurrsequence;  //序号也暂存，后续用于恢复
    //....其他内容再增加


    //(2)把以往有用的数据搞出来后，清空并给适当值
    memset(c,0,sizeof(ngx_connection_t));                //注意，类型不要用成lpngx_connection_t，否则就出错了
    c->fd      = isock;                                  //套接字要保存起来，这东西具有唯一性    


    c->curStat = _PKG_HD_INIT;                           //收包状态处于 初始状态，准备接收数据包头【状态机】

    c->precvbuf = c->dataHeadInfo;                       //收包我要先收到这里来，因为我要先收包头，所以收数据的buff直接就是dataHeadInfo
    c->irecvlen = sizeof(COMM_PKG_HEADER);               //这里指定收数据的长度，这里先要求收包头这么长字节的数据

    
    c->precvMemPointer = NULL;                            //既然没new内存，那自然指向的内存地址先给NULL
    c->iThrowsendCount = 0;                              //原子的
    c->psendMemPointer = NULL;                          //发送数据头指针记录
    //....其他内容再增加


    //(3)这个值有用，所以在上边(1)中被保留，没有被清空，这里又把这个值赋回来
    c->instance = !instance;                            //抄自官方nginx，到底有啥用，以后再说【分配内存时候，连接池里每个连接对象这个变量给的值都为1，所以这里取反应该是0【有效】；】
    c->iCurrsequence=iCurrsequence;++c->iCurrsequence;  //每次取用该值都增加1

    //wev->write = 1;  这个标记有没有 意义加，后续再研究
    return c;    
    */
}

//归还参数pConn所代表的连接到到连接池中，注意参数类型是lpngx_connection_t
void CSocekt::ngx_free_connection(lpngx_connection_t pConn) 
{
    //因为有线程可能要动连接池中连接，所以在合理互斥也是必要的
    CLock lock(&m_connectionMutex);  

    //首先明确一点，连接，所有连接全部都在m_connectionList里；
    pConn->PutOneToFree();

    //扔到空闲连接列表里
    m_freeconnectionList.push_back(pConn);

    //空闲连接数+1
    ++m_free_connection_n;

    /*
    if(c->precvMemPointer != NULL)
    {
        //我们曾经给这个连接分配过内存，则要释放内存        
        CMemory::GetInstance()->FreeMemory(c->precvMemPointer);
        c->precvMemPointer = NULL;
        //c->ifnewrecvMem = false;  //这行有用？
    }
    if(c->psendMemPointer != NULL) //如果发送数据的缓冲区里有内容，则要释放内存
    {
        CMemory::GetInstance()->FreeMemory(c->psendMemPointer);
        c->psendMemPointer = NULL;
    }

    c->next = m_pfree_connections;                       //回收的节点指向原来串起来的空闲链的链头

    //节点本身也要干一些事
    ++c->iCurrsequence;                                  //回收后，该值就增加1,以用于判断某些网络事件是否过期【一被释放就立即+1也是有必要的】
    c->iThrowsendCount = 0;                              //设置不设置感觉都行     

    m_pfree_connections = c;                             //修改 原来的链头使链头指向新节点
    ++m_free_connection_n;                               //空闲连接多1    
    */
    return;
}


//将要回收的连接放到一个队列中来，后续有专门的线程会处理这个队列中的连接的回收
//有些连接，我们不希望马上释放，要隔一段时间后再释放以确保服务器的稳定，所以，我们把这种隔一段时间才释放的连接先放到一个队列中来
void CSocekt::inRecyConnectQueue(lpngx_connection_t pConn)
{
    //ngx_log_stderr(0,"哎呀我去");

    //ngx_log_stderr(0,"CSocekt::inRecyConnectQueue()执行，连接入到回收队列中.");

    std::list<lpngx_connection_t>::iterator pos;
    bool iffind = false;
        
    CLock lock(&m_recyconnqueueMutex); //针对连接回收列表的互斥量，因为线程ServerRecyConnectionThread()也有要用到这个回收列表；

    //如下判断防止连接被多次扔到回收站中来
    for(pos = m_recyconnectionList.begin(); pos != m_recyconnectionList.end(); ++pos)
	{
		if((*pos) == pConn)		
		{	
			iffind = true;
			break;			
		}
	}
    if(iffind == true) //找到了，不必再入了
	{
		//我有义务保证这个只入一次嘛
        return;
    }

    pConn->inRecyTime = time(NULL);        //记录回收时间
    ++pConn->iCurrsequence;
    m_recyconnectionList.push_back(pConn); //等待ServerRecyConnectionThread线程自会处理 
    ++m_totol_recyconnection_n;            //待释放连接队列大小+1
    --m_onlineUserCount;                   //连入用户数量-1
    return;
}

//处理连接回收的线程
void* CSocekt::ServerRecyConnectionThread(void* threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CSocekt *pSocketObj = pThread->_pThis;
    
    time_t currtime;
    int err;
    std::list<lpngx_connection_t>::iterator pos,posend;
    lpngx_connection_t p_Conn;
    
    while(1)
    {
        //为简化问题，我们直接每次休息200毫秒
        usleep(200 * 1000);  //单位是微妙,又因为1毫秒=1000微妙，所以 200 *1000 = 200毫秒

        //不管啥情况，先把这个条件成立时该做的动作做了
        if(pSocketObj->m_totol_recyconnection_n > 0)
        {
            currtime = time(NULL);
            err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);  
            if(err != 0) ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);

lblRRTD:
            pos    = pSocketObj->m_recyconnectionList.begin();
			posend = pSocketObj->m_recyconnectionList.end();
            for(; pos != posend; ++pos)
            {
                p_Conn = (*pos);
                if(
                    ( (p_Conn->inRecyTime + pSocketObj->m_RecyConnectionWaitTime) > currtime)  && (g_stopEvent == 0) //如果不是要整个系统退出，你可以continue，否则就得要强制释放
                    )
                {
                    continue; //没到释放的时间
                }    
                //到释放的时间了: 
                //......这将来可能还要做一些是否能释放的判断[在我们写完发送数据代码之后吧]，先预留位置
                //....

                //我认为，凡是到释放时间的，iThrowsendCount都应该为0；这里我们加点日志判断下
                //if(p_Conn->iThrowsendCount != 0)
                if(p_Conn->iThrowsendCount > 0)
                {
                    //这确实不应该，打印个日志吧；
                    ngx_log_stderr(0,"CSocekt::ServerRecyConnectionThread()中到释放时间却发现p_Conn.iThrowsendCount!=0，这个不该发生");
                    //其他先暂时啥也不敢，路程继续往下走，继续去释放吧。
                }

                //流程走到这里，表示可以释放，那我们就开始释放
                --pSocketObj->m_totol_recyconnection_n;        //待释放连接队列大小-1
                pSocketObj->m_recyconnectionList.erase(pos);   //迭代器已经失效，但pos所指内容在p_Conn里保存着呢

                //ngx_log_stderr(0,"CSocekt::ServerRecyConnectionThread()执行，连接%d被归还.",p_Conn->fd);

                pSocketObj->ngx_free_connection(p_Conn);	   //归还参数pConn所代表的连接到到连接池中
                goto lblRRTD; 
            } //end for
            err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex); 
            if(err != 0)  ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
        } //end if

        if(g_stopEvent == 1) //要退出整个程序，那么肯定要先退出这个循环
        {
            if(pSocketObj->m_totol_recyconnection_n > 0)
            {
                //因为要退出，所以就得硬释放了【不管到没到时间，不管有没有其他不 允许释放的需求，都得硬释放】
                err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);  
                if(err != 0) ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()中pthread_mutex_lock2()失败，返回的错误码为%d!",err);

        lblRRTD2:
                pos    = pSocketObj->m_recyconnectionList.begin();
			    posend = pSocketObj->m_recyconnectionList.end();
                for(; pos != posend; ++pos)
                {
                    p_Conn = (*pos);
                    --pSocketObj->m_totol_recyconnection_n;        //待释放连接队列大小-1
                    pSocketObj->m_recyconnectionList.erase(pos);   //迭代器已经失效，但pos所指内容在p_Conn里保存着呢
                    pSocketObj->ngx_free_connection(p_Conn);	   //归还参数pConn所代表的连接到到连接池中
                    goto lblRRTD2; 
                } //end for
                err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex); 
                if(err != 0)  ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock2()失败，返回的错误码为%d!",err);
            } //end if
            break; //整个程序要退出了，所以break;
        }  //end if
    } //end while    
    
    return (void*)0;
}

//用户连入，我们accept4()时，得到的socket在处理中产生失败，则资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】
//我们把ngx_close_accepted_connection()函数改名为让名字更通用，并从文件ngx_socket_accept.cxx迁移到本文件中，并改造其中代码，注意顺序
void CSocekt::ngx_close_connection(lpngx_connection_t pConn)
{    
    //pConn->fd = -1; //官方nginx这么写，这么写有意义；    不要这个东西，回收时不要轻易东连接里边的内容
    ngx_free_connection(pConn); 
    if(pConn->fd != -1)
    {
        close(pConn->fd);
        pConn->fd = -1;
    }    
    return;
}
