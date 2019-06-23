// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include <stdarg.h>
#include <stdio.h>
#include <vector>
#include <list>
#include <WinSock2.h>
#include <map>
#pragma  comment(lib, "ws2_32.lib")

#pragma warning(disable : 4996)

int main()
{	
	//一：输出一些观察信息
	//每隔10秒钟把一些关键信息显示在屏幕上；
	//(1)当前在线人数；
	//(2)和连接池有关：连接列表大小，空闲连接列表大小，将来释放的连接多少个；
	//(3)当前时间队列大小
	//(4)收消息队列和发消息队列大小；
	//打印统计信息的函数：printTDInfo()，每10秒打印一次重要信息；

	//二：遗漏的安全问题思考
	//（2.1）收到太多数据包处理不过来
	//限速：epoll技术，一个限速的思路；在epoll红黑树节点中，把这个EPOLLIN【可读】通知干掉；
	//在printTDInfo()中做了一个简单提示，大家根据需要自己改造代码；

	//（2.2）积压太多数据包发送不出去
	//见void CSocekt::msgSend(char *psendbuf) 
	//.......大家多思考，看有没有什么遗漏。。。。。。。。

	//（2.3）连入安全的进一步完善
	//void CSocekt::ngx_event_accept(lpngx_connection_t oldc)
	//if(m_connectionList.size() > (m_worker_connections * 5))
	//.......大家多思考，看有没有什么遗漏。。。。。。。。

	//三：压力测试前的准备工作
	//（3.1）配置文件内容和配置项确认
	//（3.2）整理业务逻辑函数

	//四：压力测试
	//非常希望同学们好老师 一起测试。一般要测试很多天，跑的时间长了可能 会暴露下次，跑的时间短了可能还暴露不出来；
	//ScanThread
	//    socket()
	//    connect()
	//    FuncsendrecvData()
    //        send()
	//        recv()
	//    FunccloseSocket()
	//        closesocket();
	//    FunccreateSocket()
	//        socket()
	//        connect(); 
	//老师建议：
	//(1)大家有设备，有条件，都来测试；每个人都要200%的用心测试；
	//收包，简单的逻辑处理，发包；
	//(2)建议如果有多个物理电脑；客户端单独放在一个电脑；
	//建议用高性能linux服务器专门运行服务器程序
	//windows也建议单独用一个电脑来测试；
	//(3)测试什么？
	//a)程序崩溃，这明显不行，肯定要解决
	//b)程序运行异常，比如过几个小时，服务器连接不上了；没有回应了，你发过来的包服务器处理不了了；
	//c)服务器程序占用的内存才能不断增加，增加到一定程度，可能导致整个服务器崩溃；
	//top -p 3645 ：显示进程占用的内存和cpu百分比，用q可以退出；
	//cat /proc/3645/status    ---------VmRSS:	    7700 kB

	//遇到错误，及时更正；最好放在不同的物理电脑上测试；

	//（4.1）最大连接只在1000多个
	//日志中报：CSocekt::ngx_event_accept()中accept4()失败
	//这个跟 用户进程可打开的文件数限制有关； 因为系统为每个tcp连接都要创建一个socekt句柄，每个socket句柄同时也是一个文件句柄；
	//ulimit -n     ---------1024   === 1018
	//我们就必须修改linux对当前用户的进程 同时打开的文件数量的限制；

	//（4.2）学习忠告
	//如何修改linux对当前用户的进程 同时打开的文件数量的限制,留给大家；
	//依赖性；老师作用不是做保姆，不是当拐棍；老师在关键时刻帮助大家应个急
	//首先：百度，求助其他同学； 老师是大家求助的最后一道防线；
	//程序人员每个人都必须要学会自己解决问题，这种自行解决问题的能力比学习这门课程本身更重要； --授自己以渔




	//nginx中创建worker子进程
	//官方nginx ,一个master进程，创建了多个worker子进程；
	// master process ./nginx 
	// worker process
	//(i)ngx_master_process_cycle()        //创建子进程等一系列动作
	//(i)    ngx_setproctitle()            //设置进程标题    
	//(i)    ngx_start_worker_processes()  //创建worker子进程   
	//(i)        for (i = 0; i < threadnums; i++)   //master进程在走这个循环，来创建若干个子进程
	//(i)            ngx_spawn_process(i,"worker process");
	//(i)                pid = fork(); //分叉，从原来的一个master进程（一个叉），分成两个叉（原有的master进程，以及一个新fork()出来的worker进程
	//(i)                //只有子进程这个分叉才会执行ngx_worker_process_cycle()
	//(i)                ngx_worker_process_cycle(inum,pprocname);  //子进程分叉
	//(i)                    ngx_worker_process_init();
	//(i)                        sigemptyset(&set);  
	//(i)                        sigprocmask(SIG_SETMASK, &set, NULL); //允许接收所有信号
	//(i)                        g_threadpool.Create(tmpthreadnums);  //创建线程池中线程
	//(i)                        _socket.Initialize_subproc();  //初始化子进程需要具备的一些多线程能力相关的信息
	//(i)                        g_socket.ngx_epoll_init();  //初始化epoll相关内容，同时 往监听socket上增加监听事件，从而开始让监听端口履行其职责
	//(i)                            m_epollhandle = epoll_create(m_worker_connections); 
	//(i)                            ngx_epoll_add_event((*pos)->fd....);
	//(i)                                epoll_ctl(m_epollhandle,eventtype,fd,&ev);
	//(i)                    ngx_setproctitle(pprocname);          //重新为子进程设置标题为worker process
	//(i)                    for ( ;; ) {
	//(i)                        ngx_process_events_and_timers(); //处理网络事件和定时器事件 
	//(i)                            g_socket.ngx_epoll_process_events(-1); //-1表示卡着等待吧
	//(i)                                epoll_wait();
	//(i)                            g_socket.printTDInfo();
	//(i)                    }. ....                   //子进程开始在这里不断的死循环
	//(i)                    g_threadpool.StopAll();      //考虑在这里停止线程池；
	//(i)					 g_socket.Shutdown_subproc(); //socket需要释放的东西考虑释放；	

	//(i)    sigemptyset(&set); 
	//(i)    for ( ;; ) {}.                //父进程[master进程]会一直在这里循环

	   	  
}

