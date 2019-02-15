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
    int		abort_request;      // =1ʱ�����˳�����

    // ��Ƶ���
    AVStream    *video_st;
    PacketQueue *videoq;
    int         videoindex;
    AVCodecContext  *vid_codec_ctx;
    AVStream    *vstream;
    int         frame_rate;     // ֡��

    // ��Ƶ���
    AVStream    *audio_st;
    PacketQueue *audioq;
    int         audioindex;
    AVCodecContext  *aud_codec_ctx;
    AVStream    *astream;
    int         sample_rate;        // ������

    AudioParams *audio_tgt;         // SDL������Ƶ��Ҫ�ĸ�ʽ
    AudioParams *audio_src;         // �������Ƶ֡�ĸ�ʽ  ��audio_tgt��audio_src��ͬ����Ҫ�ز���
    struct SwrContext *swr_ctx;     // ��Ƶ�ز���

    uint8_t *audio_buf;             // ָ����������ݣ���ֻ��һ��ָ�룬ʵ�ʴ洢������������audio_buf1
    uint32_t audio_buf_size;        // audio_bufָ������֡�����ݳ��ȣ����ֽ�Ϊ��λ
    uint32_t audio_buf_index;       // audio_buf_index��ǰ��ȡ��λ�ã����ܳ���audio_buf_size

    uint8_t *audio_buf1;            // �洢��������Ƶ����֡����̬���룬���������㳤��ʱ�������ͷ��ٷ���
    uint32_t audio_buf1_size;       // �洢�����ݳ��ȣ����ֽ�Ϊ��λ

    uint8_t *audio_buf2;            // �洢���ٺ����Ƶ����֡
    uint32_t audio_buf2_size;       // �洢�����ݳ��ȣ����ֽ�Ϊ��λ

    // ��ʾ���
    int	width, height, xleft, ytop;
    bool is_display_open;

    // �ļ����
    char filename[1024];
    bool eof;        // �Ƿ��Ѿ���ȡ����

    SDL_Thread   *refresh_tid;
    SDL_Thread   *read_tid;

    // �������
    bool quit;       // = tureʱ�����˳�
    bool pause;      // = ture��ͣ
} VideoState;
#endif // VIDEO_STATE_H
