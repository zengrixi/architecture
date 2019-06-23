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
	//一：cpu占比与惊群
	//top -p pid,推荐文章：https://www.cnblogs.com/dragonsuc/p/5512797.html
	//惊群：1个master进程  4个worker进程
	//一个连接进入，惊动了4个worker进程，但是只有一个worker进程accept();其他三个worker进程被惊动，这就叫惊群；
	//但是，这三个被惊动的worker进程都做了无用功【操作系统本身的缺陷】；
	//官方nginx解决惊群的办法：锁，进程之间的锁；谁获得这个锁，谁就往监听端口增加EPOLLIN标记，有了这个标记，客户端连入就能够被服务器感知到；

	//3.9以上内核版本的linux，在内核中解决了惊群问题；而且性能比官方nginx解决办法效率高很多；
	  //reuseport【复用端口】,是一种套接字的复用机制，允许将多个套接字bind到同一个ip地址/端口上，这样一来，就可以建立多个服务器
	    //来接收到同一个端口的连接【多个worker进程能够监听同一个端口】；
	//大家注意一点：
	//a)很多 套接字配置项可以通过setsockopt()等等函数来配置；
	//b)还有一些tcp/ip协议的一些配置项我们可以通过修改配置文件来生效；

	//课后作业：
	//(1)在worker进程中实现ngx_open_listening_sockets()函数；
	//(2)观察，是否能解决惊群问题；
	//(3)如果在master进程中调用ngx_open_listening_sockets()函数，那么建议master进程中把监听socket关闭；

	// if (setsockopt(isock, SOL_SOCKET, SO_REUSEPORT,(const void *) &reuseport, sizeof(int))== -1)

	//二：性能优化大局观
	//a)性能优化无止境无极限
	//b)没有一个放之四海皆准的优化方法，只能够一句具体情况而定
	//c)老师在这里也不可能把性能优化方方面面都谈到，很多方面，大家都需要不断的探索和尝试；

	//从两个方面看下性能优化问题；
	//软件层面：
	//a)充分利用cpu，比如刚才惊群问题；
	//b)深入了解tcp/ip协议，通过一些协议参数配置来进一步改善性能；
	//c)处理业务逻辑方面，算法方面有些内容，可以提前做好；
	//硬件层面【花钱搞定】：
	//a)高速网卡，增加网络带宽；
	//b)专业服务器；数十个核心，马力极其强；
	//c)内存：容量大，访问速度快；
	//d)主板啊，总线不断升级的；

	//三：性能优化的实施
	//（3.1）绑定cpu、提升进程优先级
	//a)一个worker进程运行在一个核上；为什么能够提高性能呢？
	//cpu：缓存；cpu缓存命中率问题；把进程固定到cpu核上，可以大大增加cpu缓存命中率，从而提高程序运行效率；
	//worker_cpu_affinity【cpu亲和性】，就是为了把worker进程固定的绑到某个cpu核上；
	//ngx_set_cpu_affinity,ngx_setaffinity;

	//b)提升进程优先级,这样这个进程就有机会被分配到更多的cpu时间（时间片【上下文切换】），得到执行的机会就会增多；
	//setpriority()；
	//干活时进程 chuyuR状态，没有连接连入时，进程处于S
	//pidstat - w - p 3660 1    看某个进程的上下文切换次数[切换频率越低越好]
	//cswch/s：主动切换/秒：你还有运行时间，但是因为你等东西，你把自己挂起来了，让出了自己时间片。
	//nvcswch/s：被动切换/秒：时间片耗尽了，你必须要切出去；

	//c)一个服务器程序，一般只放在一个计算机上跑,专用机；

	//（3.2）TCP / IP协议的配置选项
	//这些配置选项都有缺省值，通过修改，在某些场合下，对性能可能会有所提升；
	//若要修改这些配置项，老师要求大家做到以下几点：
	//a)对这个配置项有明确的理解；
	//b)对相关的配置项,记录他的缺省值，做出修改；
	//c)要反复不断的亲自测试，亲自验证；是否提升性能，是否有副作用；

	//五：TCP / IP协议的配置选项
	//（3.1）绑定cpu、提升进程优先级
	//（3.2）TCP / IP协议的配置选项
	//（3.3）TCP/IP协议额外注意的一些算法、概念等
	//a)滑动窗口的概念
	//b)Nagle算法的概念
	//c)Cork算法
	//d)Keep - Alive机制
	//e)SO_LINGER选项

	//四：配置最大允许打开的文件句柄数
	//cat /proc/sys/fs/file-max  ：查看操作系统可以使用的最大句柄数
	//cat /proc/sys/fs/file-nr   ：查看当前已经分配的，分配了没使用的，文件句柄最大数目

	//限制用户使用的最大句柄数
	// /etc/security/limit.conf文件；
	 // root soft nofile 60000  :setrlimit(RLIMIT_NOFILE)
	 // root hard nofile 60000
	
	//ulimit -n ：查看系统允许的当前用户进程打开的文件数限制
	//ulimit -HSn 5000   ：临时设置，只对当前session有效；
	//n:表示我们设置的是文件描述符
	//推荐文章：https://blog.csdn.net/xyang81/article/details/52779229


	//五：内存池补充说明
	//为什么没有用内存池技术：感觉必要性不大
	//TCMalloc,取代malloc();
	//库地址：https://github.com/gperftools/gperftools

}

