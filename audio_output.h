#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#ifdef __cplusplus
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL2/include/SDL.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"

}
#endif
/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30
typedef struct AudioParams
{
    int freq;                   // ֡��
    int channels;               // ͨ����
    int64_t channel_layout;     // ͨ������ ����2.0������/2.1������+������/5.1��ͥӰԺ�����������ȵ�
    enum AVSampleFormat fmt;    // ������ʽ
    int frame_size;             // һ��������Ԫռ���ֽ�������һ��������Ԫ��������ͨ���Ĳ����㣩
    int bytes_per_sec;          // һ�������ĵ��ֽ�����
} AudioParams;

int audio_open(void *opaque, int64_t wanted_channel_layout,
               int wanted_nb_channels, int wanted_sample_rate,
               struct AudioParams *audio_hw_params);
void audio_stop();
#endif // AUDIO_OUTPUT_H
