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
    int freq;                   // 帧率
    int channels;               // 通道数
    int64_t channel_layout;     // 通道布局 例如2.0立体声/2.1立体声+低音炮/5.1家庭影院环绕立体声等等
    enum AVSampleFormat fmt;    // 采样格式
    int frame_size;             // 一个采样单元占的字节数量（一个采样单元包括所有通道的采样点）
    int bytes_per_sec;          // 一秒钟消耗的字节数量
} AudioParams;

int audio_open(void *opaque, int64_t wanted_channel_layout,
               int wanted_nb_channels, int wanted_sample_rate,
               struct AudioParams *audio_hw_params);
void audio_stop();
#endif // AUDIO_OUTPUT_H
