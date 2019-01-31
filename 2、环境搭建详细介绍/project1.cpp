// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>

int main()
{
	/*一：windows下的vs2017安装
		c盘空闲空间尽量不要少于30G。
		a)安装路径能设置到其他盘符（非c盘符），尽量安装到其他盘；
		b)安装时只勾选和c++有关的选项，切不可多勾选；
		c)vs2017社区版（community）；
	*/

	//二：准备一个word文档“linux c++通讯架构实战课程-重要内容记录.doc"
	//三：windows下的虚拟机安装
	 //装虚拟机软件（虚拟出一台电脑），然后我们就可以在这个虚拟出来的电脑上来安装一个linux操作系统；
	 //装虚拟机软件采用的WMware-workstation；

	//每位同学必须能够熟练的用百度搜索来解决问题；

	//四：虚拟机中安装linux 操作系统
	//a)红帽子Red hat，收费；
	//b)CentOS：被红帽子收购，免费的；
	//c)Ubuntu（乌班图）,免费；

	//用户名：kuangxiang   密码：123456
	//ctrl+alt一起按，就能把鼠标显示出来；

	//五：配置固定IP地址 
	//要修改配置文件  需要vim编辑器，乌班图要安装这个编辑器：  sudo apt-get install vim-gtk
	//两台主机（windows,乌班图）
	//ip地址不能相同，但是要在同一个网段中
	//主动发送数据包这一端  叫 “客户端”，另一端叫 “服务器端”
	//windows电脑的网络信息用ipconfig来查看；
	//IPv4 地址 . . . . . . . . . . . . : 192.168.1.100
	//子网掩码  . . . . . . . . . . . . : 255.255.255.0
	//默认网关. . . . . . . . . . . . . : 192.168.1.1
	//所以这个乌班图linux的ip地址：192.168.1.126
	//linux上查看网络信息是用ifconfig,网卡叫ens33
	//vim编辑器分 文本输入状态，命令状态，从命令状态切换到文本输入状态，需要按字母i;
	//从文本输入状态切换回命令状态，按键盘左上边的esc键盘
	//在命令状态下输入 :wq!（存盘退出）   ，而输入:q!（不存盘退出）

	//修改一下dns   8.8.8.8

	//六：配置远程连接
	//(1)需要在linux上安装ssh服务；
	//(2)远程连接工具，推荐 xshell;

	//七：安装编译工具gcc（编译c程序.c）,g++(编译c++程序，就是.cpp程序) 等
	//sudo apt-get install build-essential
	//sudo apt-get install gcc
	//sudo apt-get install g++

	//八：	共享一个操作目录
	//vim使用的不习惯；
	//二：
	//samba服务；不采用
	//通过虚拟机，把一个windows下的目录共享。让linux可以访问这个目录；

	   	  


    std::cout << "Hello World!\n"; 
}


