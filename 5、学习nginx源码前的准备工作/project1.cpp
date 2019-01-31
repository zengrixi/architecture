// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>

int main()
{
	//一：nginx源码总述
	//winrar

	//二：nginx源码查看工具
	//visual studio,source Insight,visual stuido Code.
	//采用 Visual Studio Code来阅读nginx源码
	  //Visual Studio Code:微软公司开发的一个跨平台的轻量级的编辑器（不要混淆vs2017:IDE集成开发环境，以编译器）；
	   //Visual Studio Code在其中可以安装很多扩展模块；
	   //1.30.0版本，免费的,多平台；
	//官方地址：https://code.visualstudio.com
	//https://code.visualstudio.com/download
	 //为支持语法高亮，跳转到函数等等，可能需要安装扩展包；

	//三：nginx源码入口函数定位
	  	  
	//四：创建一个自己的linux下的c语言程序
	//共享目录不见了，一般可能是虚拟机自带的工具 VMWare tools可能有问题；
	//VMWare-tools是VMware虚拟机自带的一系列的增强工具，文件共享功能就是WMWare-tools工具里边的
	//a)虚拟机->重新安装VMware tools
	//b)sudo mkdir /mnt/cdrom
	//c)sudo mount /dev/cdrom /mnt/cdrom
	//d)cd /mnt/cdrom
	//e)sudo cp WMwareTool....tar.gz  ../
	//f)cd ..
	//g)sudo tar -zxvf VMwareToo......tar.gz
	//h)cd wmware-tools-distrib
	//j)sudo ./vmware-install.pl
	//一路回车。

	//gcc编译.c，g++编译 c++
	//.c文件若很多，都需要编译，那么咱们就要写专门的MakeFile来编译了；
	//gcc -o:用于指定最终的可执行文件名

	//五：nginx源码怎么讲
	//(1)讲与不讲，是主观的；
	//(2)以讲解通讯代码为主。 其他的也会涉及，创建进程，处理信号；
	//(3)有必要的老师带着大家看源码，解释源码；
	//(4)把这些nginx中的精华的源码提取出来；带着大家往新工程中增加新代码，编译，运行，讲解；入到自己的知识库，这些是加薪的筹码





   
}


