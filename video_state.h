#ifndef VIDEO_STATE_H
#define VIDEO_STATE_H

#ifdef __cplusplus
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL2/include/SDL.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
#include "libswresample/swresample.h"
}
#endif

typedef struct PacketQueue PacketQueue;
typedef struct AudioParams AudioParams;
typedef struct VideoState
{
    AVFormatContext *ic;
    int		abort_request;      // =1时请求退出播放

    // 视频相关
    AVStream    *video_st;
    PacketQueue *videoq;
    int         videoindex;
    AVCodecContext  *vid_codec_ctx;
    AVStream    *vstream;
    int         frame_rate;     // 帧率

    // 音频相关
    AVStream    *audio_st;
    PacketQueue *audioq;
    int         audioindex;
    AVCodecContext  *aud_codec_ctx;
    AVStream    *astream;
    int         sample_rate;        // 采样率

    AudioParams *audio_tgt;         // SDL播放音频需要的格式
    AudioParams *audio_src;         // 解出来音频帧的格式  当audio_tgt和audio_src不同则需要重采样
    struct SwrContext *swr_ctx;     // 音频重采样

    uint8_t *audio_buf;             // 指向解码后的数据，它只是一个指针，实际存储解码后的数据在audio_buf1
    uint32_t audio_buf_size;        // audio_buf指向数据帧的数据长度，以字节为单位
    uint32_t audio_buf_index;       // audio_buf_index当前读取的位置，不能超过audio_buf_size

    uint8_t *audio_buf1;            // 存储解码后的音频数据帧，动态申请，当不能满足长度时则重新释放再分配
    uint32_t audio_buf1_size;       // 存储的数据长度，以字节为单位

    uint8_t *audio_buf2;            // 存储变速后的音频数据帧
    uint32_t audio_buf2_size;       // 存储的数据长度，以字节为单位

    // 显示相关
    int	width, height, xleft, ytop;
    bool is_display_open;

    // 文件相关
    char filename[1024];
    bool eof;        // 是否已经读取结束

    SDL_Thread   *refresh_tid;
    SDL_Thread   *read_tid;

    // 控制相关
    bool quit;       // = ture时程序退出
    bool pause;      // = ture暂停
} VideoState;
#endif // VIDEO_STATE_H
