#include "audio_output.h"
#include "log.h"
#include "packet_queue.h"
#include "video_state.h"
#include "sonic.h"
#include "util_time.h"

//#define CONVERT_AUDIO_FMT_BY_USER   1   //自己转换采样格式
#define CHANGE_AUDIO_SPEED          1   //使用SONIC库做变速播放

static SDL_AudioDeviceID audio_dev;

// 初始化部分：先创建一个流
static sonicStream audio_speed_convert;

FILE *pcm_file = NULL;
FILE *pcm_file2 = NULL;
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    VideoState *is = (VideoState *)opaque;
    int64_t dec_channel_layout;
    int     wanted_nb_samples;
    int data_size = 0;
    int resampled_data_size;
    AVPacket        *packet;
    AVFrame         *frame;
    int             ret;
    int actual_out_samples;
    int copy_size = 0;
    int16_t samples_buf[1024*2];            // 只是用来自己转换格式时用
    int16_t *pcm = (int16_t *)samples_buf;
    uint8_t *p_buf = stream;
    int temp_len = len;

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE, "\ninto len = %d",len);
    while (len > 0)
    {
        if (is->audio_buf_index >= is->audio_buf_size)
        {   // 帧缓存的数据已经被拷贝完毕，所以要解码才有数据
            // 从队列中获取数据包并进行解码
            int64_t cur_time = get_current_time_msec();


            ret = packet_queue_get(is->audioq, packet, 1);
//            LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE, "packet_queue_get t = %d",
//                      get_current_time_msec() - cur_time);
            if(ret  < 0)                // 正在请求退出
            {
                memset(stream, 0, len); // 静音
                break;
            }
            else if(ret == 0)           // 没有数据可输出
            {
                memset(stream, 0, len); // 静音
                break;
            }
            else if(ret == 1)           // 有数据
            {
                // 发送要解码的数据
                ret = avcodec_send_packet(is->aud_codec_ctx, packet);
                if( ret != 0 )
                {
                    av_packet_unref(packet);
                    memset(stream, 0, len); // 静音
                    break;
                }
            }

            // 获取解码后的帧
            do
            {
//                cur_time = get_current_time_msec();
                ret = avcodec_receive_frame(is->aud_codec_ctx, frame);
//                LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE, "avcodec_receive_frame t = %d",
//                          get_current_time_msec() - cur_time);
                if(ret == 0)    // 成功获取到解码后的帧
                {
                    is->audio_buf_index = 0;            // 新的一帧数据
                    // 计算每一帧的数据长度  AV_SAMPLE_FMT_FLTP
                    data_size = av_samples_get_buffer_size(NULL, frame->channels,
                                                           frame->nb_samples,
                                                           (AVSampleFormat)frame->format, 1);
                    actual_out_samples = frame->nb_samples; // 重采样后实际输出的单通道采样点数
                    dec_channel_layout =
                            (frame->channel_layout
                             && frame->channels == av_get_channel_layout_nb_channels(frame->channel_layout)) ?
                                frame->channel_layout : av_get_default_channel_layout(frame->channels);
                    wanted_nb_samples = frame->nb_samples;      // 仅audio master的时候不用调整

                    // 对比解码后的帧格式和已保存的帧格式是否一致，如果不一致则重新设置swr_ctx
                    if ((frame->format != is->audio_src->fmt)
                            || (dec_channel_layout != is->audio_src->channel_layout)
                            || (frame->sample_rate != is->audio_src->freq)
                            || (wanted_nb_samples  != frame->nb_samples && !is->swr_ctx))
                    {
                        LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE, "swr_free");

                        // 当音频参数发生变化时，重新初始化
                        swr_free(&is->swr_ctx);
                        is->swr_ctx = swr_alloc_set_opts(NULL,
                                                         is->audio_tgt->channel_layout,
                                                         is->audio_tgt->fmt,
                                                         is->audio_tgt->freq,
                                                         dec_channel_layout,
                                                         (AVSampleFormat)frame->format,
                                                         frame->sample_rate,
                                                         0,
                                                         NULL);
                        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0)
                        {
                            av_log(NULL, AV_LOG_ERROR,
                                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                                   frame->sample_rate,
                                   av_get_sample_fmt_name((AVSampleFormat)frame->format),
                                   frame->channels,
                                   is->audio_tgt->freq,
                                   av_get_sample_fmt_name((AVSampleFormat)is->audio_tgt->fmt),
                                   is->audio_tgt->channels);
                            swr_free(&is->swr_ctx);
                            return ;
                        }
                        is->audio_src->channel_layout = dec_channel_layout;
                        is->audio_src->channels       = frame->channels;
                        is->audio_src->freq = (AVSampleFormat)frame->sample_rate;
                        is->audio_src->fmt = (AVSampleFormat)frame->format;
                    }

                    if (is->swr_ctx)
                    {
                        const uint8_t **in = (const uint8_t **)frame->extended_data;
                        uint8_t **out = &is->audio_buf1;
                        // 计算输出单个通道的采样点数，+256是为了避免溢出
                        int out_count = (int64_t)wanted_nb_samples *
                                is->audio_tgt->freq / frame->sample_rate + 256;
                        // 根据out_count个采样点*channels，以及采样格式，计算输出需要的buffer空间
                        int out_size  = av_samples_get_buffer_size(NULL,
                                                                   is->audio_tgt->channels,
                                                                   out_count,
                                                                   is->audio_tgt->fmt, 0);

                        if (out_size < 0)
                        {
                            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
                            return ;
                        }

                        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
                        if (!is->audio_buf1)
                            return ;//AVERROR(ENOMEM);
                        LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE, "swr_convert");
                        actual_out_samples = swr_convert(is->swr_ctx, out, out_count, in, frame->nb_samples);
                        if (actual_out_samples < 0) {
                            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
                            return;
                        }
                        if (actual_out_samples == out_count) {
                            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
                            if (swr_init(is->swr_ctx) < 0)
                                swr_free(&is->swr_ctx);
                        }
                        // 指向缓存区
                        resampled_data_size = actual_out_samples * is->audio_tgt->channels
                                * av_get_bytes_per_sample(is->audio_tgt->fmt);
                        is->audio_buf = is->audio_buf1; //  is->audio_buf1_size
                        is->audio_buf_size = resampled_data_size;   // 为什么不是out_size
                    } else
                    {
                        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, data_size);
                        memcpy(is->audio_buf1, frame->data[0], data_size);
                        is->audio_buf = is->audio_buf1;
                        is->audio_buf_size = data_size;
                    }

#ifdef CONVERT_AUDIO_FMT_BY_USER
                    //1. 自己转换格式
                    float *lch = (float *)frame->data[0];
                    float *rch = (float *)frame->data[1];
                    pcm = samples_buf;
                    for(int i = 0; i < frame->nb_samples; i++)
                    {
                        *(pcm++) = (int16_t)(lch[i] * 32767);
                        *(pcm++) = (int16_t)(rch[i] * 32767);
                    }
                    LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE, "%f %f %f %f", lch[0], lch[1], lch[2], lch[3]);
                    is->audio_buf = (uint8_t *)samples_buf;
                    is->audio_buf_size = 4096;
#endif
#ifdef CHANGE_AUDIO_SPEED
                    // 变速播放
                    static int s_in_cout = 0;
                    static int s_out_cout = 0;
                    float speed = 1.0;
                    int sonic_samples;
                    // 计算处理后的点数
                    int numSamples = actual_out_samples / speed;
                    data_size = av_samples_get_buffer_size(NULL,  is->audio_tgt->channels,
                                                           (numSamples+1),
                                                           (AVSampleFormat)is->audio_tgt->fmt, 1);
                    // 处理变速
                    av_fast_malloc(&is->audio_buf2, &is->audio_buf2_size,
                                   data_size);
                    //LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE, "%p, %d, %d", is->audio_buf2, is->audio_buf2_size, data_size);
                    // 设置变速系数
                    sonicSetSpeed(audio_speed_convert, speed);
                    // 写入原始数据
                    int out_ret = sonicWriteShortToStream(audio_speed_convert,
                                                          (short *)is->audio_buf,
                                                          actual_out_samples);
                    LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE, "sonicWriteShortToStream");
                    s_in_cout += actual_out_samples;
                    if(out_ret)
                    {
                        // 从流中读取处理好的数据
                        sonic_samples = sonicReadShortFromStream(audio_speed_convert,
                                                                 (int16_t *)is->audio_buf2,
                                                                 numSamples);
                    }
                    s_out_cout += sonic_samples;
                    //                    LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE, "sonic_samples = %d, in=%d, out=%d, aq_size = %d",
                    //                           sonic_samples, s_in_cout, (int)(s_out_cout*speed),
                    //                           is->audioq->size);
                    is->audio_buf = is->audio_buf2;
                    is->audio_buf_size = sonic_samples*2*2;
#endif


                    if(len >= (is->audio_buf_size - is->audio_buf_index))
                    {
                        copy_size = is->audio_buf_size - is->audio_buf_index;
                        // 把整帧数据拷贝
                        memcpy(stream, is->audio_buf + is->audio_buf_index, copy_size);
                        is->audio_buf_index += copy_size;
                        len -= copy_size;
                        fwrite(stream,1, copy_size, pcm_file);
                        stream += copy_size;

                    }
                    else
                    {
                        memcpy(stream, is->audio_buf + is->audio_buf_index, len);
                        is->audio_buf_index += len;
                        fwrite(stream,1, len, pcm_file);
                        stream += len;
                        len = 0;
                    }

                    LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE,
                              "audio_buf_index = %d, len = %d", is->audio_buf_index, len);
                    if(len<=0)
                        break;
                }
                else if(ret == AVERROR(EAGAIN))
                {
                    // 没有帧可读，等待下一次输入后再读取
                    //                    LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "EAGAIN");
                    break;
                }
                else if(ret == AVERROR_EOF)
                {
                    // 解码器所有帧都已经被读取
                    avcodec_flush_buffers(is->aud_codec_ctx);
                    LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "avcodec_flush_buffers");
                    break;
                }
                else if(ret < 0)
                {
                    LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "if(ret < 0)\n");
                    break;
                }
            } while(ret != AVERROR(EAGAIN));

            av_packet_unref(packet);        // 释放内存
        }
        else        // 上一次拷贝还剩有数据
        {
            if(len >= (is->audio_buf_size - is->audio_buf_index))
            {
                // 把整帧数据拷贝
                copy_size = is->audio_buf_size - is->audio_buf_index;
                // 把整帧数据拷贝
                memcpy(stream, is->audio_buf + is->audio_buf_index, copy_size);
                is->audio_buf_index += copy_size;
                len -= copy_size;
                fwrite(stream,1, copy_size, pcm_file);
                stream += copy_size;
            }
            else
            {
                memcpy(stream, is->audio_buf + is->audio_buf_index, len);
                is->audio_buf_index += len;
                fwrite(stream,1, len, pcm_file);
                stream += len;
                len = 0;
            }
        }
    }

    av_packet_free(&packet);

    fwrite(p_buf, 1, temp_len, pcm_file2);
    //    LOG_DEBUG(DEBUG_AUDIO_DECODE | DBG_TRACE, "reamain = %d, %d, %d\n",
    //              is->audio_buf_size - is->audio_buf_index, is->audio_buf_size, is->audio_buf_index);
}



int audio_open(void *opaque, int64_t wanted_channel_layout,
               int wanted_nb_channels, int wanted_sample_rate,
               struct AudioParams *audio_hw_params)
{
    SDL_AudioSpec wanted_spec; // 我想要的设置
    SDL_AudioSpec spec;        //实际返回来的设置
    const char *env;
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;
    if(SDL_Init( SDL_INIT_AUDIO) < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_HALT, "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env)
    {
        wanted_nb_channels = atoi(env);
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout))
    {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE,
                                2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = opaque;
    while (!(audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec,
                                             SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE)))
    {
        av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, SDL_GetError());
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels)
        {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq)
            {
                av_log(NULL, AV_LOG_ERROR,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }
    if (spec.format != AUDIO_S16SYS)
    {
        av_log(NULL, AV_LOG_ERROR,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels)
    {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout)
        {
            av_log(NULL, AV_LOG_ERROR,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;           // AV_SAMPLE_FMT_S16只是SDL建议的格式
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels =  spec.channels;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0)
    {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }

    SDL_PauseAudioDevice(audio_dev, 0);     // 开启声音输出

    // 参数为采样率和声道数
    audio_speed_convert = sonicCreateStream(audio_hw_params->freq, audio_hw_params->channels);
    pcm_file = fopen("pcm_dump.pcm", "wb+");
    pcm_file2 = fopen("pcm2_dump.pcm", "wb+");

    return spec.size;
}

void audio_stop()
{
    SDL_PauseAudioDevice(audio_dev, 1);     // 暂停声音输出
    SDL_CloseAudioDevice(audio_dev);        // 关闭音频设备
    fclose(pcm_file);
    fclose(pcm_file2);
}
