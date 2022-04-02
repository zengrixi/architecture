﻿
//和 内存分配 有关的函数放这里
/*
公众号：程序员速成     q群：716480601
王健伟老师 《Linux C++通讯架构实战》
商业级质量的代码，完整的项目，帮你提薪至少10K
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ngx_c_memory.h"

//类静态成员赋值
CMemory *CMemory::m_instance = NULL;

//分配内存
//memCount：分配的字节大小
//ifmemset：是否要把分配的内存初始化为0；
void *CMemory::AllocMemory(int memCount,bool ifmemset)
{	    
	void *tmpData = (void *)new char[memCount]; //我并不会判断new是否成功，如果new失败，程序根本不应该继续运行，就让它崩溃以方便我们排错吧
    if(ifmemset) //要求内存清0
    {
	    memset(tmpData,0,memCount);
    }
	return tmpData;
}

//内存释放函数
void CMemory::FreeMemory(void *point)
{		
	//delete [] point;  //这么删除编译会出现警告：warning: deleting ‘void*’ is undefined [-Wdelete-incomplete]
    delete [] ((char *)point); //new的时候是char *，这里弄回char *，以免出警告
}

