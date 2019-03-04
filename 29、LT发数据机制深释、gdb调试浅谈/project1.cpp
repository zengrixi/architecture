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
	//一：水平触发模式（LT）下发送数据深度解释
	//在水平触发模式下，发送数据有哪些注意事项；
	//a)一个问题
	//通过调用 ngx_epoll_oper_event(EPOLL_CTL_MOD,EPOLLOUT),那么当socket可写的时候，
		//会触发socket的可写事件，我得到了这个事件我就可以发送数据了；
	//什么叫socekt可写；    每一个tcp连接(socket)，都会有一个接收缓冲区  和 一个发送缓冲；
	    //发送缓冲区缺省大小一般10几k，接收缓冲区大概几十k，setsocketopt()来设置；
	      //send(),write()发送数据时，实际上这两个函数是把数据放到了发送缓冲区，之后这两个函数返回了；
	        //客户端用recv(),read()；
	      //如果服务器端的发送 缓冲区满了，那么服务器再调用send(),write()发送数据的时候，那么send(),write()函数就会返回一个EAGAIN；
	        //EAGAIN不是一个错误，只是示意发送缓冲区已经满了，迟一些再调用send(),write()来发送数据吧；

	//二：gdb调试浅谈
	//当socket可写的时候【发送缓冲区没满】，会不停的触发socket可写事件【水平触发模式】，已经验证；
	//遇到程序崩溃问题，所以需要借助gdb调试来找到崩溃行；
	//好在：我们的错误能够重现[必现的错误，是很好找的]；
	//最怕的就是偶尔出现的bug；有的时候运行三个小时就出现，有的时候运行两天也不出现；
	//a)编译时g++ 要带这个 -g选项；
	//b)su进入root权限，然后gdb nginx调试
	//c)gdb缺省调试主进程，但是gdb 7.0以上版本可以调试子进程【我们需要调试子进程，因为干活的是worker process是子进程】； 
	 //命令 行下 :gdb -v看版本
	//d)为了让gdb支持多进程调试，要设置一下  follow-fork-mode选项 ，这是个调试多进程的开关； 
	       //取值可以是parent[主] /child[子] ，我们这里需要设置成child才能调试worker process子进程；
	   //查看follow-fork-mode：  在gdb下输入show follow-fork-mode
	    //输入 set follow-fork-mode child
	//(e)  还有个选项 detach-on-fork， 取值为 on/off，默认是on【表示只调试父进程或者子进程其中的一个】
	            //调试是父进程还是子进程，由上边的 follow-fork-mode选项说了算；
	            //如果detach-on-fork = off，就表示父子都可以调试，调试一个进程时，另外一个进程会被暂停；
	    //查看 show detach-on-fork     
	       //输入set show detach-on-fork off   ，如果设置为off并且 follow-fork-mode选项为parent，那么fork()后的子进程并不运行，而是处于暂停状态；
	//(f)b logic/ngx_c_slogic.cxx:198
	//(g)run 运行程序运行到断点；
	//(h)print。。..打印变量值。这些调试手段,大家自己百度学习；
	//(i)c命令，继续运行


	//针对 当socket可写的时候【发送缓冲区没满】，会不停的触发socket可写事件 ,我们提出两种解决方案【面试可能考试】；
	//b)两种解决方案，来自网络,意义在于我们可以通过这种解决方案来指导我们写代码；
	//b.1)第一种最普遍的解决方案:
	   //需要向socket写数据的时候把socket写事件通知加入到epoll中，等待可写事件，当可写事件来时操作系统会通知咱们；
	    //此时咱们可以调用wirte/send函数发送数据，当发送数据完毕后，把socket的写事件通知从红黑树中移除；
	   //缺点：即使发送很少的数据，也需要把事件通知加入到epoll，写完毕后，有需要把写事件通知从红黑树干掉,对效率有一定的影响【有一定的操作代价】

	//b.2)改进方案；
	//开始不把socket写事件通知加入到epoll,当我需要写数据的时候，直接调用write/send发送数据；
	  //如果返回了EAGIN【发送缓冲区满了，需要等待可写事件才能继续往缓冲区里写数据】，此时，我再把写事件通知加入到epoll，
	     //此时，就变成了在epoll驱动下写数据，全部数据发送完毕后，再把写事件通知从epoll中干掉；
	//优点：数据不多的时候，可以避免epoll的写事件的增加/删除，提高了程序的执行效率；

	//老师准备采用b.2)改进方案来指导咱们后续发送数据的代码；































	

	
}

