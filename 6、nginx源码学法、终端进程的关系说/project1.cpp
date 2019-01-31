// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>

#pragma warning(disable : 4996)

int main()
{
	//一：nginx源码学习方法
	//(1)简单粗暴，啃代码，比较艰苦，需要比较好的基础
	//(2)看书看资料,逃脱不了啃代码的过程
	//(3)跟着老师学，让大家用最少的力气掌握nginx中最有用的东西。
	//架构课程前期学习两个主要任务；
	//(1)泛读nginx中的一些重要代码；
	//(2)把nginx中最重要的代码提取出来作为我们自己知识库的一部分以备将来使用；


	//二：终端和进程的关系
	//（2.1）终端与bash进程
	//ps -ef | grep bash
	//pts(虚拟终端)，每连接一个虚拟终端到乌班图linux操作系统，
				//就会出现 一个bash进程（shell[壳]),黑窗口，用于解释用户输入的命令
	             //bash = shell = 命令行解释器
	//whereis bash

	//（2.2）终端上的开启进程
	//ps -la
	//man ps
	//随着终端的退出，这个终端上运行的进程nginx也退出了；
	//可执行程序nginx是bash的子进程；
	//《unix环境高级编程》第九章 进程关系；

	//（2.3）进程关系进一步分析
	//每个进程还属于一个进程组:一个或者多个进程的集合，每个进程组有一个唯一的进程组ID，可以调用系统 函数来创建进程组、加入进程组
	//“会话”(session)：是一个或者多个进程组的集合
	//一般，只要不进行特殊的系统函数调用，一个bash(shell)上边运行的所有程序都属于一个会话，而这个会话有一个session leader；
	   //那么这个bash(shell)通常就是session leader; 你可以调用系统功函数创建新的session。

	//ps -eo pid,ppid,sid,tty,pgrp,comm | grep -E 'bash|PID|nginx'

	//a)如果我 xshell终端要断开的话，系统就会发送SIGHUP信号（终端断开信号），给session leader,也就是这个bash进程
	//b)bash进程 收到 SIGHUP信号后，bash会把这个信号发送给session里边的所有进程，收到这个SIGHUP信号的进程的缺省动作就是退出；

	//（2.4）strace工具的使用
	//linux下调试分析诊断工具:可以跟踪程序执行时进程的系统调用以及所收到的信号；
	//a)跟踪nginx进程   :   sudo strace -e trace=signal -p 1359

	//kill(4294965937, SIGHUP)   ：发送信号SIGHUP给这个 -1359的绝对值所在的进程组；所以nginx进程就收到了SIGHUP信号
	//综合来讲，这个bash先发送SIGHUP给  同一个session里边的所有进程；
	   //然后再发送SIGHUP给自己；

	//int abc = 4294965937; //-1359

	//（2.5）终端关闭时如何让进程不退出
	//设想
	//a)nginx进程拦截（忽略）SIGHUP(nginx收到这个信号并告诉操作系统，我不想死，请不要把我杀死)信号，是不是可以；
	//b)nginx进程和bash进程不再同一个seeion里；
	//孤儿进程
	//setsid函数不适合进程组组长调用；

	//setsid命令:启动一个进程，而且能够使启动的进程在一个新的session中，这样的话，终端关闭时该进程就不会退出，试试
	//setsid ./nginx

	//nohup(no hang up不要挂断）,用该命令启动的进程跟上边忽略掉SIGHUP信号，道理相同
	//该命令会把屏幕输出重新定位到当前目录的nohup.out

	//ps -eo pid,ppid,sid,tty,pgrp,comm,cmd|grep -E 'bash|PID|nginx'


	//（2.6）后台运行 &
	//后台执行，执行这个程序的同时，你的终端能够干其他事情；你如果不用 后台执行，那么你执行这个程序后
	  //你的终端就只能等这个程序完成后才能继续执行其他的操作；
	//fg切换到前台

	   	  


}


