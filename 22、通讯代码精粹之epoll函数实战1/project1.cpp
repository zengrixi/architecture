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
	//一：一个更正，一个注意
	//更正：kqueue
	//注意：即将进入最重要，最核心的内容讲解；
	//戒骄戒躁，代码精华。简单，容易理解；大家要认真学习老师给出来的代码；

	//二：配置文件的修改
	//增加worker_connections项

	//三：epoll函数实战
	//epoll_create(),epoll_ctl(),epoll_wait();系统提供的函数调用
	//（3.1）ngx_epoll_init函数内容
	//epoll_create()：创建一个epoll对象，创建了一个红黑树，还创建了一个双向链表；

	//连接池： 数组，元素数量就是worker_connections【1024】，每个数组元素类型为 ngx_connection_t【结构】； ---结构数组；
	 //为什么要引入这个数组：  2个监听套接字， 用户连入进来，每个用户多出来一个套接字；
	   //把 套接字数字跟一块内存捆绑，达到的效果就是将来我通过这个套接字，就能够把这块内存拿出来；
	//ngx_get_connection()重要函数：从连接池中找空闲连接；
	//a)epoll_create()  *****************
	//b)连接池（找空闲连接）
	//c)ngx_epoll_add_event（） ************
	//    epoll_ctl();
	//d)ev.data.ptr = (void *)( (uintptr_t)c | c->instance);    把一个指针和一个位 合二为一，塞到一个void *中去，
	     //后续能够把这两个值全部取出来，如何取，取出来干嘛，后续再说；

	//ps -eo pid,ppid,sid,tty,pgrp,comm,stat,cmd | grep -E 'bash|PID|nginx'

	//如下命令用root权限执行
	//sudo su       获得root权限
	//lsof -i:80    列出哪些进程在监听80端口
	//netstat -tunlp | grep 80

	//总结：
	//a)epoll_create();epoll_ctl();
	//b)连接池技巧ngx_get_connection（），ngx_free_connection（）；学习这种编程方法；
	//c)同时传递 一个指针和一个二进制数字技巧；

	//（3.2）ngx_epoll_init函数的调用（要在子进程中执行）
	//四章，四节 project1.cpp：nginx中创建worker子进程；
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
	//(i)                        g_socket.ngx_epoll_init();  //初始化epoll相关内容，同时 往监听socket上增加监听事件，从而开始让监听端口履行其职责
	//(i)                            m_epollhandle = epoll_create(m_worker_connections); 
	//(i)                            ngx_epoll_add_event((*pos)->fd....);
	//(i)                                epoll_ctl(m_epollhandle,eventtype,fd,&ev);
	//(i)                    ngx_setproctitle(pprocname);          //重新为子进程设置标题为worker process
	//(i)                    for ( ;; ) {}. ....                   //子进程开始在这里不断的死循环

	//(i)    sigemptyset(&set); 
	//(i)    for ( ;; ) {}.                //父进程[master进程]会一直在这里循环
	   	  
}


