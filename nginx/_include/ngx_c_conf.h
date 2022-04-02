
#ifndef __NGX_CONF_H__
#define __NGX_CONF_H__

#include <vector>

#include "ngx_global.h" //一些全局/通用定义

class CConfig
{
    //---------------------------------------------------
private:
    CConfig();

public:
    ~CConfig();

private:
    static CConfig* m_instance;

public:
    static CConfig* GetInstance()
    {
        if (m_instance == NULL)
        {
            if (m_instance == NULL)
            {
                m_instance = new CConfig();
                static CGarhuishou cl;
            }
        }
        return m_instance;
    }
    class CGarhuishou
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
    bool        Load(const char* pconfName);
    const char* GetString(const char* p_itemname);
    int         GetIntDefault(const char* p_itemname, const int def);

public:
    std::vector<LPCConfItem> m_ConfigItemList; //存储配置信息的列表
};

#endif
