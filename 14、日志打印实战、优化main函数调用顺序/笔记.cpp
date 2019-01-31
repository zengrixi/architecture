// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include <stdarg.h>

using namespace std;
#pragma warning(disable : 4996)

int main()
{
	//一：基础设施之日志打印实战代码一
	//1-3万行代码，想收获多少就要付出多少，平衡
	//注意代码的保护，私密性
	//日志的重要性：供日后运行维护人员去查看、定位和解决问题；
	//新文件：ngx_printf.cxx以及ngx_log.cxx。
	//ngx_printf.cxx：放和打印格式相关的函数；
	//ngx_log.cxx：放和日志相关的函数；

	//ngx_log_stderr()  :三个特殊文件描述符【三章七节】，谈到了标准错误 STDERR_FILENO，代表屏幕
	//ngx_log_stderr()：往屏幕上打印一条错误信息；功能类似于printf
	//printf("mystring=%s,myint=%d，%d","mytest",15,20);
	//(1)根据可变的参数，组合出一个字符串:mystring=mytest,myint=15，20
	//(2)往屏幕上显示出这个组合出来的字符串；
	//讲解ngx_log_stderr()函数的理由：
	//(1)提高大家编码能力；
	//(2)ngx_log_stderr()：可以支持任意我想支持的格式化字符 %d， %f,对于扩展原有功能非常有帮助
	//(i)void ngx_log_stderr(int err, const char *fmt, ...)
	//(i)    p = ngx_vslprintf(p,last,fmt,args); //实现了自我可定制的printf类似的功能
	//(i)        buf = ngx_sprintf_num(buf, last, ui64, zero, hex, width);
	//(i)    p = ngx_log_errno(p, last, err);

	//二：设置时区
	//我们要设置成CST时区,以保证日期，时间显示的都正确
	//我们常看到的时区，有如下几个：
	//a)PST【PST美国太平洋标准时间】 = GMT - 8;
	//b)GMT【格林尼治平均时间Greenwich Mean Time】等同于英国伦敦本地时间
	//c)UTC【通用协调时Universal Time Coordinated】 = GMT
	//d)CST【北京时间：北京时区是东八区，领先UTC八个小时】

	//三：基础设施之日志打印实战代码二
	//（3.1）日志等级划分
	//划分日志等级，一共分8级，分级的目的是方便管理，显示，过滤等等；
	//日志级别从高到低，数字最小的级别最高，数字最大的级别最低；

	//（3.2）配置文件中和日志有关的选项
	//继续介绍void ngx_log_init();打开/创建日志文件
	//介绍ngx_log_error_core()函数：写日志文件的核心函数
	//ngx_slprintf
	//    ngx_vslprintf

	//四：捋顺main函数中代码执行顺序











}


