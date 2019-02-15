#ifndef LOG_H
#define LOG_H

#define LOG_DEBUG_EN 1

//日志输出级别定义
#define LOG_LEVEL_ALL                   0x00
#define LOG_LEVEL_INIT                  0x01    //初始化成功信息
#define LOG_LEVEL_PROMPT                0x02    //提示信息
#define LOG_LEVEL_WARNING               0x03    //警告信息
#define LOG_LEVEL_ERROR                 0x04    //错误信息
#define LOG_LEVEL_SERIOUS               0x05    //严重错误
#define LOG_LEVEL_MALLOC                0x06    //内存分配失败
//最小日志输出级别(日志级别只有>=此值才会输出)
#define DBG_MIN_LEVEL                LOG_LEVEL_ALL

//日志输出级别掩码
#define DBG_MASK_LEVEL               0x07

//日志调试标志
#define DBG_ON                       0x80U   //使能调试输出
#define DBG_OFF                      0x00U   //禁止调试输出
//日志等级
#define DBG_TRACE                    0x40U   //流程跟踪消息
#define DBG_STATE                    0x20U   //模块状态消息
#define DBG_ERROR                    0x10U   //模块错误消息
#define DBG_HALT                     0x08U   //模块挂起(严重错误)消息

//日志调试总开关
#define DBG_TYPES_ON   (DBG_TRACE | DBG_STATE | DBG_ERROR | DBG_HALT)

//使能/禁止相关模块调试输出
#define DEBUG_PLAYER                    DBG_ON      //播放器
#define DEBUG_AVSYNC                    DBG_ON      //音视频同步模块
#define DEBUG_VIDEO_DECODE              DBG_ON      //视频解码模块
#define DEBUG_AUDIO_DECODE              DBG_OFF      //视频解码模块
#define DEBUG_PLAYER_QUIT               DBG_ON      //播放器退出时间

//总是满足LOG_DEBUG()的输出条件，不受DBG_ON/DBG_OFF影响
#define DBG_ALWAYS_OUTPUT            0xFF

#define _LOG_DEBUG(function, line, debug, message,...)                     \
    do                                                                      \
    {                                                                       \
        if(((debug) & DBG_ON)                                               \
            && ((debug) & DBG_TYPES_ON)                                     \
            && (((debug) & DBG_MASK_LEVEL) >= DBG_MIN_LEVEL))               \
        {                                                                   \
            char msg[256];                                                  \
            char ti[32];                                                    \
            time_t now = time(NULL);                                        \
            sprintf(msg, message, ##__VA_ARGS__);                           \
            strftime(ti, sizeof(ti), "%Y-%m-%d %H:%M:%S", localtime(&now)); \
            printf("[%s] [%s(%d)]  %s\n", ti, function, line, msg); \
        }                                                                   \
    }while(0)

void log_debug(const char *function, int line, int debug,const char *message,...);
//调试输出宏
#ifdef LOG_DEBUG_EN
// 调用函数
#define LOG_DEBUG(debug, message...)  log_debug(__FUNCTION__, __LINE__, debug, message)
// 直接宏替换
//#define LOG_DEBUG(debug, message...)  _LOG_DEBUG(__FUNCTION__, __LINE__, debug, message)
#else
#define LOG_DEBUG(debug, message, ...)
#endif
#endif // LOG_H
