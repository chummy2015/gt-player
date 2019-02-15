#ifndef LOG_H
#define LOG_H

#define LOG_DEBUG_EN 1

//��־���������
#define LOG_LEVEL_ALL                   0x00
#define LOG_LEVEL_INIT                  0x01    //��ʼ���ɹ���Ϣ
#define LOG_LEVEL_PROMPT                0x02    //��ʾ��Ϣ
#define LOG_LEVEL_WARNING               0x03    //������Ϣ
#define LOG_LEVEL_ERROR                 0x04    //������Ϣ
#define LOG_LEVEL_SERIOUS               0x05    //���ش���
#define LOG_LEVEL_MALLOC                0x06    //�ڴ����ʧ��
//��С��־�������(��־����ֻ��>=��ֵ�Ż����)
#define DBG_MIN_LEVEL                LOG_LEVEL_ALL

//��־�����������
#define DBG_MASK_LEVEL               0x07

//��־���Ա�־
#define DBG_ON                       0x80U   //ʹ�ܵ������
#define DBG_OFF                      0x00U   //��ֹ�������
//��־�ȼ�
#define DBG_TRACE                    0x40U   //���̸�����Ϣ
#define DBG_STATE                    0x20U   //ģ��״̬��Ϣ
#define DBG_ERROR                    0x10U   //ģ�������Ϣ
#define DBG_HALT                     0x08U   //ģ�����(���ش���)��Ϣ

//��־�����ܿ���
#define DBG_TYPES_ON   (DBG_TRACE | DBG_STATE | DBG_ERROR | DBG_HALT)

//ʹ��/��ֹ���ģ��������
#define DEBUG_PLAYER                    DBG_ON      //������
#define DEBUG_AVSYNC                    DBG_ON      //����Ƶͬ��ģ��
#define DEBUG_VIDEO_DECODE              DBG_ON      //��Ƶ����ģ��
#define DEBUG_AUDIO_DECODE              DBG_OFF      //��Ƶ����ģ��
#define DEBUG_PLAYER_QUIT               DBG_ON      //�������˳�ʱ��

//��������LOG_DEBUG()���������������DBG_ON/DBG_OFFӰ��
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
//���������
#ifdef LOG_DEBUG_EN
// ���ú���
#define LOG_DEBUG(debug, message...)  log_debug(__FUNCTION__, __LINE__, debug, message)
// ֱ�Ӻ��滻
//#define LOG_DEBUG(debug, message...)  _LOG_DEBUG(__FUNCTION__, __LINE__, debug, message)
#else
#define LOG_DEBUG(debug, message, ...)
#endif
#endif // LOG_H
