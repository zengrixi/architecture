// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include <stdarg.h>
#include <stdio.h>
#include <vector>

using namespace std;
#pragma warning(disable : 4996)

int main()
{	
	//一：ngx_epoll_process_events函数调用位置
	//上节课：epoll_create();epoll_ctl()；--我们目前已经做好准备 等待迎接客户端主动发起三次握手连入；
	//介绍ngx_epoll_process_events();

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
	//(i)                        g_socket.ngx_epoll_init();  //初始化epoll相关内容，同时 往监听socket上增加监听事件，从而开始让监听端口履行其职责
	//(i)                            m_epollhandle = epoll_create(m_worker_connections); 
	//(i)                            ngx_epoll_add_event((*pos)->fd....);
	//(i)                                epoll_ctl(m_epollhandle,eventtype,fd,&ev);
	//(i)                    ngx_setproctitle(pprocname);          //重新为子进程设置标题为worker process
	//(i)                    for ( ;; ) 
    //(i)                    {
	//(i)                        //子进程开始在这里不断的死循环
	//(i)                        ngx_process_events_and_timers(); //处理网络事件和定时器事件
	//(i)                            g_socket.ngx_epoll_process_events(-1); //-1表示卡着等待吧
    //(i)                    }

	//(i)    sigemptyset(&set); 
	//(i)    for ( ;; ) {}.                //父进程[master进程]会一直在这里循环

	//从ngx_epoll_process_events()的函数调用位置，我们能够感觉到：
	//a)这个函数，仍旧是在子进程中被调用；
	//b)这个函数，放在了子进程的for ( ;; ) ，这意味着这个函数会被不断的调用；

	//二：ngx_epoll_process_events函数内容
	//用户三次握手成功连入进来，这个“连入进来”这个事件对于我们服务器来讲，就是 一个监听套接字上的可读事件；
	//（2.1）事件驱动：官方nginx本身的架构也被称为“事件驱动架构”；拆开：“事件”，“驱动”；
	//“驱动”：汽车靠发动机驱动，人体的发动机就是心脏；
	//“驱动”：动力的来源；驱动的问题，就是探讨程序怎么来干活的问题；
	//“事件驱动”，无非就是通过获取事件，通过获取到的事件并根据这个事件来调用适当的函数从而让整个程序干活，无非就是这点事；

	//三：ngx_event_accept函数内容
	//a)accept4/accept
	//b)ngx_get_connection/setnonblocking
	//c)ngx_epoll_add_event
	//（3.1）epoll的两种工作模式：LT和ET【面试可能问】
	//LT：level trigged， 水平触发，这种工作模式 低速模式（效率差） -------------epoll缺省用次模式
	//ET：edge trigged，  边缘触发/边沿触发，这种工作模式 高速模式（效率好）
	//现状：所有的监听套接字用的都是 水平触发；   所有的接入进来的用户套接字都是  边缘触发

	//水平触发的意思：来 一个事件，如果你不处理它，那么这个事件就会一直被触发；
	//边缘触发的意思：只对非阻塞socket有用；来一个事件，内核只会通知你一次【不管你是否处理，内核都不再次通知你】；
	       //边缘触发模式，提高系统运行效率，编码的难度加大；因为只通知一次，所以接到通知后，你必须要保证把该处理的事情处理利索；
	//程序高手能够洞察到很多普通程序员所洞察不到的问题，并且能够写出更好的，更稳定的，更少出错误的代码；


	//四：总结和测试
	//（1）服务器能够感知到客户端发送过来abc字符了；
	//（2）来数据会调用ngx_wait_request_handler（）

	//五：事件驱动总结：nginx所谓的事件驱动框架【面试可能问到】
	//我们的项目和官方nginx一样，都是事件驱动框架；
	//总结事件驱动框架/事件驱动架构
	//所谓事件驱动框架，就是由一些事件发生源【三次握手内核通知，事件发生源就是客户端】，通过事件收集器来手机和分发事件【调用函数处理】
	//【事件收集器：epoll_wait()函数】【ngx_event_accept（），ngx_wait_request_handler（）都属于事件处理器，用来消费事件】

	//六：一道腾讯后台开发的面试题
	//问题：使用Linux epoll模型，水平触发模式；当socket可写时，会不停的触发socket可写的事件，如何处理？

	/*
	答案：
		第一种最普遍的方式：
		需要向socket写数据的时候才把socket加入epoll【红黑树】，等待可写事件。接受到可写事件后，调用write或者send发送数据。当所有数据都写完后，把socket移出epoll。
		这种方式的缺点是，即使发送很少的数据，也要把socket加入epoll，写完后在移出epoll，有一定操作代价。

		一种改进的方式：
		开始不把socket加入epoll，需要向socket写数据的时候，直接调用write或者send发送数据。如果返回EAGAIN，把socket加入epoll，在epoll
		的驱动下写数据，全部数据发送完毕后，再移出epoll。
		这种方式的优点是：数据不多的时候可以避免epoll的事件处理，提高效率。
	*/
	   	  	   	  
}


