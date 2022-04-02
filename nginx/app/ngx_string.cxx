
//一些和字符串处理相关的函数，放这里
/*
公众号：程序员速成     q群：716480601
王健伟老师 《Linux C++通讯架构实战》
商业级质量的代码，完整的项目，帮你提薪至少10K
*/
#include <stdio.h>
#include <string.h>

//截取字符串尾部空格
void Rtrim(char *string)   
{   
	size_t len = 0;   
	if(string == NULL)   
		return;   

	len = strlen(string);   
	while(len > 0 && string[len-1] == ' ')   //位置换一下   
		string[--len] = 0;   
	return;   
}

//截取字符串首部空格
void Ltrim(char *string)
{
	size_t len = 0;
	len = strlen(string);   
	char *p_tmp = string;
	if( (*p_tmp) != ' ') //不是以空格开头
		return;
	//找第一个不为空格的
	while((*p_tmp) != '\0')
	{
		if( (*p_tmp) == ' ')
			p_tmp++;
		else
			break;
	}
	if((*p_tmp) == '\0') //全是空格
	{
		*string = '\0';
		return;
	}
	char *p_tmp2 = string; 
	while((*p_tmp) != '\0')
	{
		(*p_tmp2) = (*p_tmp);
		p_tmp++;
		p_tmp2++;
	}
	(*p_tmp2) = '\0';
    return;
}