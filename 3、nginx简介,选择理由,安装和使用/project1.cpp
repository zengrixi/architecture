// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>

int main()
{
	//一：nginx简介
	//nginx(2002年开发,2004年10才出现第一个版本0.1.0):web服务器，市场份额，排在第二位，Apache(1995)第一位； 
	//web服务器，反向代理，负载均衡，邮件代理；运行时需要的系统资源比较少，所以经常被称呼为轻量级服务器；
	//是一个俄罗斯人（Igor Sysoev)，C语言（不是c++）开发的，并且开源了；
	//nginx号称并发处理百万级别的TCP连接，非常稳定，热部署（运行的时候能升级），高度模块化设计，自由许可证。
	//很多人开发自己的模块来增强nginx，第三方业务模块（c++开发）； OpenResty；
	//linux epoll技术；   windows IOCP


	//二：为什么选择nginx 
	//单机10万并发，而且同时能够保持高效的服务，epoll这种高并发技术好处就是：高并发只是占用更多内存就能 做到；
	//内存池，进程池，线程池，事件驱动等等；
	//学习研究大师级的人写的代码，是一个程序开发人员能够急速进步的最佳途径；

	//三：安装nginx，搭建web服务器
	//（3.1）安装前提
	//a)epoll,linux 内核版本为2.6或者以上；
	//b)gcc编译器，g++编译器
	//c)pcre库：函数库；支持解析正则表达式；
	//d)zlib库：压缩解压缩功能
	//e)openssl库：ssl功能相关库，用于网站加密通讯

	//（3.2）nginx源码下载以及目录结构简单认识
	//nginx官网 http://www.nginx.org
	//nginx的几种版本
	//(1)mainline版本：版本号中间数字一般为奇数。更新快，一个月内就会发布一个新版本，最新功能，bug修复等，稳定性差一点；
	//(2)stable版本：稳定版，版本号中间数字一般为偶数。经过了长时间的测试，比较稳定，商业化环境中用这种版本；这种版本发布周期比较长，几个月；
	//(3)Legacy版本：遗产，遗留版本，以往的老版本；
	//安装，现在有这种二进制版本：通过命令行直接安装；
	//灵活：要通过编译 nginx源码手段才能把第三方模块弄进来；
/*
	auto / :编译相关的脚本，可执行文件configure一会会用到这些脚本
		cc / : 检查编译器的脚本
		lib / : 检查依赖库的脚本
		os / : 检查操作系统类型的脚本
		type / : 检查平台类型的脚本
	CHANGES : 修复的bug，新增加的功能说明
	CHANGES.ru : 俄语版CHANGES
	conf / : 默认的配置文件
	configure : 编译nginx之前必须先执行本脚本以生成一些必要的中间文件
	contrib / : 脚本和工具，典型的是vim高亮工具
		vim / : vim高亮工具
	html / : 欢迎界面和错误界面相关的html文件
	man / : nginx帮助文件目录
	src / : nginx源码目录
		core : 核心代码
		event : event(事件)模块相关代码
		http : http(web服务)模块相关代码
		mail : 邮件模块相关代码
		os : 操作系统相关代码
		stream : 流处理相关代码
	objs/:执行了configure生成的中间文件目录
		ngx_modules.c：内容决定了我们一会编译nginx的时候有哪些模块会被编译到nginx里边来。
		Makefile:执行了configure脚本产生的编译规则文件，执行make命令时用到		
		*/
	
	//（3.3）nginx的编译和安装
	//a)编译的第一步：用configure来进行编译之前的配置工作
	//./configure
	//--prefix：指定最终安装到的目录：默认值 /usr/local/nginx
	//--sbin-path：用来指定可执行文件目录：默认的是   sbin/ nginx
	//--conf-path：用来指定配置文件目录：默认的是  conf/nginx.conf 
	//b)用make来编译,生成了可执行文件   make
	//c)用make命令开始安装   sudo make install

	//四：nginx的启动和简单使用
	//启动:   sudo ./nginx
	//乌班图：192.168.1.128
	//百度：“服务器程序端口号”；
	//百度：“监听端口号”；
	//（4.1）通讯程序基础概念
	//a)找个人： 这个人住哪（IP地址），第二个事情是知道它叫什么（端口号）；
	//b)



	  

	  	  


   
}


