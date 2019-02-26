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

using namespace std;
#pragma warning(disable : 4996)


int main()
{	
	//一：学习方法
	//不但要学习老师编写程序的方法，风格，更要学习老师解决一个问题的思路。
	//编程语言、语法这种东西如果你不会，可以通过学习来解决，但是这种 解决问题的思路， 是一种只可意会难以言传的东西，
	   //却恰恰能够决定你在开发道路上走多远的东西，搞程序开发一定要培养自己非常清晰的逻辑思维，不然，这条程序开发之路
	     //你会走的特别艰辛；

	//二：多线程的提出
	//用 “线程” 来解决客户端发送过来的 数据包
	//一个进程 跑起来之后缺省 就自动启动了一个 “主线程”，也就是我们一个worker进程一启动就等于只有一个“主线程”在跑；
	//我们现在涉及到了业务逻辑层面，这个就要用多线程处理，所谓业务逻辑：充值，抽卡，战斗；
	  //充值，需要本服务器和专门的充值服务器通讯，一般需要数秒到数十秒的通讯时间。此时，我们必须采用多线程【100个多线程】处理方式；
	    //一个线程因为充值被卡住，还有其他线程可以提供给其他玩家及时的服务；

	//所以，我们服务器端处理用户需求【用户逻辑/业务】的时候一般都会启动几十甚至上百个线程来处理，以保证用户的需求能够得到及时处理；
	//epoll，  iocp(windows)，启动线程数cpu*2+2;
	//主线程 往消息队列中用inMsgRecvQueue()扔完整包（用户需求），那么一堆线程要从这个消息对列中取走这个包，所在必须要用互斥；
	//互斥技术在《c++从入门到精通 c++98/11/14/17》的并发与多线程一章详细介绍过；
	//多线程名词
	//a)POSIX：表示可移植操作系统接口（Portable Operating System Interface of UNIX)。
	//b)POSIX线程：是POSIX的线程标准【大概在1995年左右标准化的】；它定义了创建和操纵线程的一套API（Application Programming Interface：应用程序编程接口），
	   //说白了 定义了一堆我们可以调用的函数，一般是以pthread_开头，比较成熟，比较好用；我们就用这个线程标准；
	

	//三：线程池实战代码
	 //（3.1）为什么引入线程池 
	//我们完全不推荐用单线程的方式解决逻辑业务问题，我们推荐多线程开发方式；
	//线程池：说白了 就是 我们提前创建好一堆线程，并搞一个雷来统一管理和调度这一堆线程【这一堆线程我们就叫做线程池】,
	   //当来了一个任务【来了一个消息】的时候，我从这一堆线程中找一个空闲的线程去做这个任务【去干活/去处理这个消息】， 
	     //活干完之后，我这个线程里边有一个循环语句，我可以循环回来等待新任务，再有新任务的时候再去执行新的任务；
	      //就好像这个线程可以回收再利用 一样；
	//线程池存在意义和价值；
	//a)实现创建好一堆线程，避免动态创建线程来执行任务，提高了程序的稳定性；有效的规避程序运行之中创建线程有可能失败的风险；
	//b)提高程序运行效率：线程池中的线程，反复循环再利用；
	//大家有兴趣，可以百度 线程池； 但是说到根上，用线程池的目的无非就两条：提高稳定性，提升整个程序运行效率，容易管理【使编码更清晰简单】

	//【pthread多线程库】 gcc 末尾要增加  -lpthread；
	//$(CC) - o $@ $^ -lpthread
    //CThreadPool【线程池管理类】
	//讲解了 Create()，ThreadFunc(),StopAll();

	//四：线程池的使用
	//（4.1）线程池的初始化 ：Create();
	//（4.2）线程池工作的激发,所谓激发，就是让线程池开始干活了；
	//激发的时机：当我收到了一个完整的用户来的消息的时候，我就要激发这个线程池来获取消息开始工作；
	  //那我激发代码放在哪里呢？
	//（4.3）线程池完善和测试
	//a)我只开一个线程【线程数量过少，线程池中只有一个线程】,我们需要报告；
	//b)来多个消息会堆积，但是不会丢消息，消息会逐条处理；
	//c)开两个线程,执行正常，每个线程，都得到了一个消息并且处理；表面看起来，正常；


	   	  
	//程序执行流程【可能不太全，后续讲到哪里缺了再补充不着急】
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
	//(i)                        g_socket.ngx_epoll_init();  //初始化epoll相关内容，同时 往监听socket上增加监听事件，从而开始让监听端口履行其职责
	//(i)                            m_epollhandle = epoll_create(m_worker_connections); 
	//(i)                            ngx_epoll_add_event((*pos)->fd....);
	//(i)                                epoll_ctl(m_epollhandle,eventtype,fd,&ev);
	//(i)                    ngx_setproctitle(pprocname);          //重新为子进程设置标题为worker process
	//(i)                    for ( ;; ) {}. ....                   //子进程开始在这里不断的死循环

	//(i)    sigemptyset(&set); 
	//(i)    for ( ;; ) {}.                //父进程[master进程]会一直在这里循环

}


