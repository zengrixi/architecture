
#ifndef __NGX_MACRO_H__
#define __NGX_MACRO_H__

//各种#define宏定义相关的定义放这里

#define NGX_MAX_ERROR_STR   2048   //显示的错误信息最大数组长度

//简单功能函数--------------------
//类似memcpy，但常规memcpy返回的是指向目标dst的指针，而这个ngx_cpymem返回的是目标【拷贝数据后】的终点位置，连续复制多段数据时方便
#define ngx_cpymem(dst, src, n)   (((u_char *) memcpy(dst, src, n)) + (n))  //注意#define写法，n这里用()包着，防止出现什么错误
#define ngx_min(val1, val2)  ((val1 > val2) ? (val2) : (val1))              //比较大小，返回小值，注意，参数都用()包着

//数字相关--------------------
#define NGX_MAX_UINT32_VALUE   (uint32_t) 0xffffffff              //最大的32位无符号数：十进制是‭4294967295‬
#define NGX_INT64_LEN          (sizeof("-9223372036854775808") - 1)     

//日志相关--------------------
//我们把日志一共分成八个等级【级别从高到低，数字最小的级别最高，数字大的级别最低】，以方便管理、显示、过滤等等
#define NGX_LOG_STDERR            0    //控制台错误【stderr】：最高级别日志，日志的内容写入log参数指定的文件，同时也尝试直接将日志输出到标准错误设备比如控制台屏幕
#define NGX_LOG_EMERG             1    //紧急 【emerg】
#define NGX_LOG_ALERT             2    //警戒 【alert】
#define NGX_LOG_CRIT              3    //严重 【crit】
#define NGX_LOG_ERR               4    //错误 【error】：属于常用级别
#define NGX_LOG_WARN              5    //警告 【warn】：属于常用级别
#define NGX_LOG_NOTICE            6    //注意 【notice】
#define NGX_LOG_INFO              7    //信息 【info】
#define NGX_LOG_DEBUG             8    //调试 【debug】：最低级别

//#define NGX_ERROR_LOG_PATH       "logs/error1.log"   //定义日志存放的路径和文件名 
#define NGX_ERROR_LOG_PATH       "error.log"   //定义日志存放的路径和文件名 

//进程相关----------------------
//标记当前进程类型
#define NGX_PROCESS_MASTER     0  //master进程，管理进程
#define NGX_PROCESS_WORKER     1  //worker进程，工作进程
//.......其他待扩展


#endif
