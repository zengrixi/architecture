
#ifndef __NGX_CONF_H__
#define __NGX_CONF_H__

#include <vector>

#include "ngx_global.h"  //一些全局/通用定义

//类名可以遵照一定的命名规则规范，比如老师这里，第一个字母是C，后续的单词首字母大写
class CConfig
{
//---------------------------------------------------
//这段代码老师在《c++从入门到精通》 多线程这章老师提过 单例设计模式，就是如下这些代码，大家即便没学过，也可以现在学
private:
	CConfig();
public:
	~CConfig();
private:
	static CConfig *m_instance;

public:	
	static CConfig* GetInstance() 
	{	
		if(m_instance == NULL)
		{
			//锁
			if(m_instance == NULL)
			{					
				m_instance = new CConfig();
				static CGarhuishou cl; 
			}
			//放锁		
		}
		return m_instance;
	}	
	class CGarhuishou  //类中套类，用于释放对象
	{
	public:				
		~CGarhuishou()
		{
			if (CConfig::m_instance)
			{						
				delete CConfig::m_instance;				
				CConfig::m_instance = NULL;				
			}
		}
	};
//---------------------------------------------------
public:
    bool Load(const char *pconfName); //装载配置文件
	const char *GetString(const char *p_itemname);
	int  GetIntDefault(const char *p_itemname,const int def);

public:
	std::vector<LPCConfItem> m_ConfigItemList; //存储配置信息的列表

};

#endif
