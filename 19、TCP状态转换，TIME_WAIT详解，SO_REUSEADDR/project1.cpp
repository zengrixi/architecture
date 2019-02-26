// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include <stdarg.h>
#include <stdio.h>

using namespace std;
#pragma warning(disable : 4996)


int main()
{
	//一：TCP状态转换
	//同一个IP（INADDR_ANY），同一个端口SERV_PORT，只能被成功的bind()一次，若再次bind()就会失败，并且显示：Address already in use
	   //就好像一个班级里不能有两个人叫张三；
	//结论：相同IP地址的相同端口，只能被bind一次；第二次bind会失败；

	//介绍命令netstat：显示网络相关信息
	//-a:显示所有选项
	//-n:能显示成数字的内容全部显示成数字
	//-p：显示段落这对应程序名
	//netstat -anp | grep -E 'State|9000'

	//我们用两个客户端连接到服务器，服务器给每个客户端发送一串字符"I sent sth to client!\n"，并关闭客户端;
	//我们用netstat观察，原来那个监听端口 一直在监听【listen】，但是当来了两个连接之后【连接到服务器的9000端口】，
	     //虽然这两个连接被close掉了，但是产生了两条TIME_WAIT状态的信息【因为你有两个客户端连入进来】

	//只要客户端 连接到服务器，并且 服务器把客户端关闭，那么服务器端就会产生一条针对9000监听端口的 状态为 TIME_WAIT 的连接；
	 //只要用netstat看到 TIME_WAIT状态的连接，那么此时， 你杀掉服务器程序再重新启动，就会启动失败，bind()函数返回失败：
	     //bind返回的值为-1,错误码为:98，错误信息为:Address already in use
	//TIME_WAIT：涉及到TCP状态转换这个话题了；
	//《Unix网络编程 第三版 卷1》有第二章第六节，2.6.4小节，里边就有一个TCP状态转换图；
	 //第二章第七节，专门介绍了 TIME_WAIT状态；

	//TCP状态转换图【11种状态】 是 针对“一个TCP连接【一个socket连接】”来说的；
	//客户端：   CLOSED ->SYN_SENT->ESTABLISHED【连接建立，可以进行数据收发】
	//服务端：   CLOSED ->LISTEN->【客户端来握手】SYN_RCVD->ESTABLISHED【连接建立，可以进行数据收发】
	//谁主动close连接，谁就会给对方发送一个FIN标志置位的一个数据包给对方；【服务器端发送FIN包给客户端】
	//服务器主动关闭连接：ESTABLISHED->FIN_WAIT1->FIN_WAIT2->TIME_WAIT
	//客户端被动关闭：ESTABLISHED->CLOSE_WAIT->LAST_ACK

	//二：TIME_WAIT状态
	//具有TIME_WAIT状态的TCP连接，就好像一种残留的信息一样；当这种状态存在的时候，服务器程序退出并重新执行会失败，会提示：
	   //bind返回的值为-1,错误码为:98，错误信息为:Address already in use
	//所以，TIME_WAIT状态是一个让人不喜欢的状态；
	//连接处于TIME_WAIT状态是有时间限制的（1-4分钟之间） = 2 MSL【最长数据包生命周期】；
	//引入TIME_WAIT状态【并且处于这种状态的时间为1-4分钟】 的原因：
	//(1)可靠的实现TCP全双工的终止
	    //如果服务器最后发送的ACK【应答】包因为某种原因丢失了，那么客户端一定 会重新发送FIN，这样
	      //因为服务器端有TIME_WAIT的存在，服务器会重新发送ACK包给客户端，但是如果没有TIME_WAIT这个状态，那么
	       //无论客户端收到ACK包，服务器都已经关闭连接了，此时客户端重新发送FIN，服务器给回的就不是ACK包，
	        //而是RST【连接复位】包，从而使客户端没有完成正常的4次挥手，不友好，而且有可能造成数据包丢失；
	         //也就是说，TIME_WAIT有助于可靠的实现TCP全双工连接的终止；

		//（二.一）RST标志
			//对于每一个TCP连接，操作系统是要开辟出来一个收缓冲区，和一个发送缓冲区 来处理数据的收和发；
			//当我们close一个TCP连接时，如果我们这个发送缓冲区有数据，那么操作系统会很优雅的把发送缓冲区里的数据发送完毕，然后再发fin包表示连接关闭；
			//FIN【四次挥手】，是个优雅的关闭标志，表示正常的TCP连接关闭；

			//反观RST标志：出现这个标志的包一般都表示 异常关闭；如果发生了异常，一般都会导致丢失一些数据包；
			//如果将来用setsockopt(SO_LINGER)选项要是开启；发送的就是RST包，此时发送缓冲区的数据会被丢弃；
			//RST是异常关闭，是粗暴关闭，不是正常的四次挥手关闭，所以如果你这么关闭tcp连接，那么主动关闭一方也不会进入TIME_WAIT；

	//(2)允许老的重复的TCP数据包在网络中消逝；

	//三：SO_REUSEADDR选项
	//setsockopt（SO_REUSEADDR）用在服务器端，socket()创建之后，bind()之前
	//SO_REUSEADDR的能力：
	//（1）SO_REUSEADDR允许启动一个监听服务器并捆绑其端口，即使以前建立的将端口用作他们的本地端口的连接仍旧存在；
	  //【即便TIME_WAIT状态存在，服务器bind()也能成功】
	//（2）允许同一个端口上启动同一个服务器的多个实例，只要每个实例捆绑一个不同的本地IP地址即可；
	//（3）SO_REUSEADDR允许单个进程捆绑同一个端口到多个套接字，只要每次捆绑指定不同的本地IP地址即可；
	//（4）SO_REUSEADDR允许完全重复的绑定：当一个IP地址和端口已经绑定到某个套接字上时，如果传输协议支持，
	  //同样的IP地址和端口还可以绑定到另一个套接字上；一般来说本特性仅支持UDP套接字[TCP不行]；
	//***************
	//所有TCP服务器都应该指定本套接字选项，以防止当套接字处于TIME_WAIT时bind()失败的情形出现；
	//试验程序nginx5_3_2_server.c
	//（3.1）两个进程，绑定同一个IP和端口：bind()失败[一个班级不能有两个人叫张三]
	//（3.2）TIME_WAIT状态时的bind绑定：bind()成功

	//SO_REUSEADDR：主要解决TIME_WAIT状态导致bind()失败的问题；

	  
	   	    	  
 	  

}


