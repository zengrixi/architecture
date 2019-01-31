// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>

int main()
{
	//一：nginx的整体结构
	//（1.1）master进程和worker进程概览(父子关系)
	//启动nginx，看到了一个master进程，一个worker进程
	//ps -ef命令
	//第一列：UID，进程所属的用户id
	//第二列：进程ID（PID),用来唯一的标识一个进程
	//第三列：父进程ID（PPID）。 fork（），worker进程是被master进程通过fork()创建出来的-worker进程是master进程的子进程,master是父进程


	//（1.2）nginx进程模型
	//1个master进程，1到多个worker进程 这种工作机制来对外服务的；这种工作机制保证了 nginx能够稳定、灵活的运行；
	//a)master进程责任：监控进程，不处理具体业务，专门用来管理和监控worker进程；master，角色是监工，比如清闲；
	//b)worker进程：用来干主要的活的，（和用户交互）；
	//c)master进程和worker进程之间要通讯，可以用 信号 ，也可以用 共享内存 ；
	//d)稳定性，灵活性，体现之一：worker进程 一旦挂掉，那么master进程会立即fork()一个新的worker进程投入工作中去； 

	//（1.3）调整worker进程数量
	//worker进程几个合适呢？公认的做法： 多核计算机，就让每个worker运行在一个单独的内核上，最大限度减少CPU进程切换成本，提高系统运行效率；
	//物理机：4核(4个processors)；

	//工作站：2个物理cpu ,蓝色的一个cpu，红色的一个cpu
	//每个物理cpu里边内核数量，是4个；core1 --core4
	//每个core里边有两个逻辑处理器（超线程技术/siblings)
	//16个processors(最细小的单位，也就是平时大家说的处理器个数)

	//二：nginx进程模型细说
	//稳定  ，灵活
	//（2.1）nginx重载配置文件
	//（2.2）nginx热升级,热回滚
	//（2.3）nginx的关闭
	//（2.4）总结
	//多进程，多线程：
	//多线程模型的弊端：共享内存,如果某个线程报错一定会影响到其他线程,最终会导致整个服务器程序崩溃；






	  	  


   
}


