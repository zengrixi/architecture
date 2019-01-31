// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>

#pragma warning(disable : 4996)

int main()
{
	//一：信号的基本概念
	//进程之间的常用通信手段：发送信号,kill  第二章第二节讲过；
	//上节课讨论过 SIGHUP
	//信号 ：通知（事情通知），用来通知某个进程发生了某一个事情；
	  //事情，信号都是突发事件， 信号是异步发生的，信号也被称呼为“软件中断”

	//信号如何产生：
	//a)某个进程发送给另外一个进程或者发送给自己；
	//b)由内核(操作系统)发送给某个进程
	 //b.1)通过在键盘输入命令ctrl+c[中断信号],kill命令
	 //b.2)内存访问异常，除数为0等等，硬件都会检测到并且通知内核；

	//信号名字，都是以SIG开头，上节课SIGHUP（终端断开信号）
	 //UNIX以及类（类似）UNIX操作系统(linux，freebsd,solaris）；支持的信号数量各不相同。10-60多个之间；

	//信号既有名字，其实也都是一些数字，信号是一些正整数常量；信号就是宏定义（数字，从1开始）
	//#include <signal.h>(/usr/include/)
	//gcc 
	//头文件，包含路径：/user/local/include/   /usr/include/
	//库文件，连接路径：/usr/ local/lib/      /usr/lib

	//sudo find / -name "signal.h" | xargs grep -in "SIGHUP"

	//二：通过kill命令认识一些信号
	//kill :kill 进程id ,他的工作是发个信号给进程；
	//kill能给进程发送多种信号；
	//ps -eo pid,ppid,sid,tty,pgrp,comm | grep -E 'bash|PID|nginx'
	//sudo strace -e trace=signal -p 1184

	//a)如果你单纯的用kill 进程id，那么就是往 进程发送SIGTERM信号（终止信号）
	//kill -数字 进程id，能发出跟这个数字对应的信号  -1 进程id，SIGHUP信号去

	//b)如果我门用kill -1 进程id，那么就是往进程nginx发送SIGHUP终止信号；同时进程nginx就被终止掉了；
	//c)kill -2 进程id，发送SIGINT信号；
	//kill -数字 进程id ，能发送出多种信号；

	//三：进程的状态
	//ps -eo pid,ppid,sid,tty,pgrp,comm,stat | grep -E 'bash|PID|nginx'
	//ps aux | grep -E 'bash|PID|nginx'    //aux所谓BSD风格显示格式;
	//kill只是发个信号，而不是单纯的杀死的意思。

	//四：常用的信号列举

	//五：信号处理的相关动作
	//当某个信号出现时，我们可以按三种方式之一进行处理，我们称之为信号的处理或者与信号相关的动作；
	//(1)执行系统默认动作 ,绝大多数信号的默认动作是杀死你这个进程；
	//(2)忽略此信号(但是不包括SIGKILL和SIGSTOP)
	//kill -9 进程id，是一定能够把这个进程杀掉的；
	//(3)捕捉该信号：我写个处理函数，信号来的时候，我就用处理函数来处理；(但是不包括SIGKILL和SIGSTOP)






}


