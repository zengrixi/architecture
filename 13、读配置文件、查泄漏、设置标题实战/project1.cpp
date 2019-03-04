// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>

using namespace std;
#pragma warning(disable : 4996)

int main()
{
	//一：基础设施之配置文件读取
	//（1.1）前提内容和修改
	//使用配置文件，使我们的服务器程序有了极大的灵活性,是我们作为服务器程序开发者，必须要首先搞定的问题；

	//配置文件：文本文件，里边除了注释行之外不要用中文，只在配置文件中使用字母，数字下划线
	  //以#号开头的行作为注释行(注释行可以有中文)
	//我们这个框架（项目），第一个要解决的问题是读取配置文件中的配置项（读到内存中来）；

	//（1.2）配置文件读取功能实战代码
	//写代码要多顾及别人感受，让别人更容易读懂和理解，不要刻意去炫技；这种炫技的人特别讨厌；
	//该缩进的必须要缩进，该对齐的要对齐，该注释的要注释，这些切记

	//二：内存泄漏的检查工具
	//Valgrind：帮助程序员寻找程序里的bug和改进程序性能的工具集。擅长是发现内存的管理问题；
	 //里边有若干工具，其中最重要的是Memcheck(内存检查）工具，用于检查内存的泄漏；
	//（2.1）memcheck的基本功能，能发现如下的问题；
	//a)使用未初始化的内存
	//b)使用已经释放了的内存
	//c)使用超过malloc()分配的内存
	//d)对堆栈的非法访问
	//e)申请的内存是否有释放*****
	//f)malloc/free,new/delete申请和释放内存的匹配
	//g)memcpy()内存拷贝函数中源指针和目标指针重叠；

	//（2.2）内存泄漏检查示范
	//所有应该释放的内存，都要释放掉，作为服务器程序开发者，要绝对的严谨和认真
	//格式：
	//valgrind --tool=memcheck  一些开关      可执行文件名
	//--tool=memcheck ：使用valgrind工具集中的memcheck工具
	//--leak-check=full ： 指的是完全full检查内存泄漏
	//--show-reachable=yes ：是显示内存泄漏的地点
	//--trace-children = yes ：是否跟入子进程
	//--log-file=log.txt：讲调试信息输出到log.txt，不输出到屏幕
	//最终用的命令：
	//valgrind --tool=memcheck --leak-check=full --show-reachable=yes ./nginx
	//查看内存泄漏的三个地方：
	//(1) 9 allocs, 8 frees  差值是1，就没泄漏，超过1就有泄漏
	//(2)中间诸如： by 0x401363: CConfig::Load(char const*) (ngx_c_conf.cxx:77)和我们自己的源代码有关的提示，就要注意；
	//(3)LEAK SUMMARY:definitely lost: 1,100 bytes in 2 blocks

	//三：设置可执行程序的标题（名称）
	//（3.1）原理和实现思路分析
	//argc:命令行参数的个数
	//argv:是个数组，每个数组元素都是指向一个字符串的char *，里边存储的内容是所有命令行参数；
	   //./nginx -v -s 5
	   //argc = 4
	   //argv[0] = ./nginx    ----指向的就是可执行程序名： ./nginx
	   //argv[1] = -v
	   //argv[2] = -s
	   //argv[3] = 5
	//比如你输入 ./nginx -12 -v 568 -q gess

	//argv内存之后，接着连续的就是环境变量参数信息内存【是咱们这个可执行程序执行时有关的所有环境变量参数信息】
	  //可以通过一个全局的environ[char **]就可以访问
	//environ内存和argv内存紧紧的挨着
	//修改可执行程序的实现思路：
	//(1)重新分配一块内存，用来保存environ中的内容；
	//(2)修改argv[0]所指向的内存；
	
	//（3.2）设置可执行程序的标题实战代码


	    





	
	
}


