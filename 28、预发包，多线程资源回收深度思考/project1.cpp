// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include <stdarg.h>
#include <stdio.h>
#include <vector>
#include <list>
#include <WinSock2.h>
#pragma  comment(lib, "ws2_32.lib")

#pragma warning(disable : 4996)

int main()
{	
	//一：业务逻辑细节写法说明
	//_HandleRegister(),_HandleLogIn()里边到底执行什么，是属于业务逻辑代码；写法，大家自己决定
	//100元，  加血90，加魔80
	//发送数据代码下节实现

	//二：连接池中连接回收的深度思考
	//服务器 7*24不间断，服务器稳定性是第一位的；一个服务器，如果不稳定，那么谈别的都是虚的；
	//稳定性：连接池连接回收问题；
	//如果客户端【张三】断线，服务器端立即回收连接，这个连接很可能被紧随其后连入的新客户端【李四】所使用，那么这里就很可能产生麻烦；
	//a)张三 funca(); ---执行10秒，服务器从线程池中找一个线程来执行张三的任务；
	//b)执行到第5秒的时候，张三断线；
	//c)张三断线这个事情会被服务器立即感知到，服务器随后调用ngx_close_connection把原来属于张三这个连接池中的连接给回收了；
	//d)第7秒的时候，李四连上来了，系统会把刚才张三用过的连接池中的连接分配给李四【现在这个连接归李四使用】；
	//e)10秒钟到了，这个线程很可能会继续操纵 连接【修改读数据】；很可能导致服务器整个崩溃；这种可能性是非常有的；

	//一个连接，如果我们程序判断这个连接不用了；那么 不 应该把这个连接立即放到空闲队列里，而是 应该放到一个地方；
	  //等待一段时间【60】，60秒之后，我再真正的回收这个连接到 连接池/空闲队列 中去，这种连接才可以真正的分配给其他用户使用；
	   //为什么要等待60秒，就是需要确保即便用户张三真断线了，那么我执行的该用户的业务逻辑也一定能在这个等待时间内全部完成；
	//这个连接不立即回收是非常重要的，有个时间缓冲非常重要；这个可以在极大程度上确保服务器的稳定；
	//服务器程序要常年累月的优化；
	//（2.1）灵活创建连接池
	//（2.2）连接池中连接的回收
	//a)立即回收【accept用户没有接入时可以立即回收】  b)延迟回收【用户接入进来开始干活了】；
	//(2.2.1)立即回收 ngx_free_connection()；
	//(2.2.2)延迟回收 inRecyConnectQueue()，ServerRecyConnectionThread()

	//三：程序退出时线程的安全终止
	 //多线程开发技术在我们这个项目中是全面开花的

	//四：epoll事件处理的改造
	//（4.1）增加新的事件处理函数，引入ngx_epoll_oper_event()函数取代ngx_epoll_add_event()；
	//（4.2）调整对事件处理函数的调用:ngx_epoll_init(),ngx_event_accept(),ngx_epoll_process_events();

	//（5）连接延迟回收的具体应用
	//recvproc()调用了inRecyConnectQueue(延迟回收)取代了ngx_close_connection(立即回收)
	//Initialize_subproc()子进程中干；
	//Shutdown_subproc()子进程中干


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
	//(i)                    }. ....                   //子进程开始在这里不断的死循环
	//(i)                    g_threadpool.StopAll();      //考虑在这里停止线程池；
	//(i)					 g_socket.Shutdown_subproc(); //socket需要释放的东西考虑释放；	

	//(i)    sigemptyset(&set); 
	//(i)    for ( ;; ) {}.                //父进程[master进程]会一直在这里循环	
	
}

