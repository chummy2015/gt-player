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
#include "audio_output.h"
#include "log.h"
#include "packet_queue.h"
#include "util_time.h"
#include "video_state.h"

// Since we only have one decoding thread, the Big Struct can be global in case we need it.
VideoState *global_video_state = NULL;

// SDL 这部分主要是显示相关，
static int default_width  = 640;
static int default_height = 480;
int screen_width = 0;
int screen_height = 0;
static SDL_Window   *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture  *vid_texture = NULL;
static SDL_Rect     sdlRect;

// 包队列数据缓存控制
#define MAX_QUEUE_SIZE (512 * 1024)

const char *s_picture_type[] =
{
    "AV_PICTURE_TYPE_NONE", ///< Undefined
    "AV_PICTURE_TYPE_I",     ///< Intra
    "AV_PICTURE_TYPE_P",     ///< Predicted
    "AV_PICTURE_TYPE_B",     ///< Bi-dir predicted
    "AV_PICTURE_TYPE_S",     ///< S(GMC)-VOP MPEG-4
    "AV_PICTURE_TYPE_SI",    ///< Switching Intra
    "AV_PICTURE_TYPE_SP",    ///< Switching Predicted
    "AV_PICTURE_TYPE_BI"   ///< BI type
};

// 自定义SDL事件
#define FRAME_REFRESH_EVENT (SDL_USEREVENT+1)
static int frame_refresh_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    while(!is->quit)
    {
        if(!is->pause)
        {
            SDL_Event event;
            event.type = FRAME_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        if(is->frame_rate > 0)
            SDL_Delay(1000/is->frame_rate);     // 这里控制播放速度，有时候因为调试所以使用了倍速。
        else
            SDL_Delay(40);
    }

    LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "quit");
    return 0;
}

static char err_buf[128] = {0};
static char* av_get_err(int errnum)
{
    av_strerror(errnum, err_buf, 128);
    return err_buf;
}

static int stream_component_open(VideoState *is, int stream_index)
{
    int ret;
    AVCodecContext *avctx;
    AVCodec *codec;
    int sample_rate, nb_channels;
    int64_t channel_layout;

    // 分配解码器上下文内存，使用avcodec_free_context来释放
    avctx = avcodec_alloc_context3(NULL);
    if(!avctx)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] avcodec_alloc_context3: %s", av_get_err(ret));
        goto failed;
    }
    // 将码流中的编解码器信息AVCodecParameters拷贝到AVCodecContex
    ret = avcodec_parameters_to_context(avctx, is->ic->streams[stream_index]->codecpar);
    if(ret < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] avcodec_parameters_to_context: %s", av_get_err(ret));
        goto failed;
    }
    // 查找解码器
    codec = avcodec_find_decoder(avctx->codec_id);
    if(codec == NULL)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] Video Codec not found");
        goto failed;
    }
    // 打开解码器
    ret = avcodec_open2(avctx, codec, NULL);
    if(ret < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error]avcodec_open2 failed: %s", av_get_err(ret));
        goto failed;
    }
    switch (avctx->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        sample_rate    = avctx->sample_rate;//采样率
        nb_channels    = avctx->channels;//通道数
        channel_layout = avctx->channel_layout;//通道布局
        is->aud_codec_ctx = avctx;
        is->audio_tgt = (AudioParams *)av_mallocz(sizeof(AudioParams));
        is->audio_src = (AudioParams *)av_mallocz(sizeof(AudioParams));
        is->audio_buf_index = 0;
        if ((ret = audio_open(is, channel_layout, nb_channels, sample_rate, is->audio_tgt)) < 0)
            goto failed;
        *(is->audio_src) = *(is->audio_tgt);
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->vid_codec_ctx = avctx;
        is->frame_rate = is->vstream->avg_frame_rate.num / is->vstream->avg_frame_rate.den;
        screen_width = is->vid_codec_ctx->width;
        screen_height = is->vid_codec_ctx->height;
        break;
    }
    return 0;
failed:
    return -1;
}

static int read_thread(void *arg)
{
    VideoState		*is	= (VideoState *) arg;
    AVCodec         *pcodec;
    AVPacket        *packet;
    int             ret;

    // 分配解复用器的内存，使用avformat_close_input释放
    is->ic = avformat_alloc_context();
    if (!is->ic)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] Could not allocate context");
        goto failed;
    }

    // 根据url打开码流，并选择匹配的解复用器
    ret = avformat_open_input(&is->ic, is->filename, NULL, NULL);
    if(ret != 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] avformat_open_input: %s", av_get_err(ret));
        goto failed;
    }

    // 读取媒体文件的部分数据包以获取码流信息
    ret = avformat_find_stream_info(is->ic, NULL);
    if(ret < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] avformat_find_stream_info: %s", av_get_err(ret));
        goto failed;
    }

    // 查找出哪个码流是video/audio/subtitles
    is->audioindex = -1;
    is->videoindex = -1;
    is->videoindex = av_find_best_stream(is->ic, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if(is->videoindex == -1)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "Didn't find a video stream");
        goto failed;
    }
    is->vstream =  is->ic->streams[is->videoindex];


    is->audioindex = av_find_best_stream(is->ic, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if(is->audioindex == -1)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "Didn't find a audio stream");
        goto failed;
    }

    // 视频解码器
    if(stream_component_open(is, is->videoindex) < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "stream_component_open video failed");
        goto failed;
    }

    // 音频解码器
    if(stream_component_open(is, is->audioindex) < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "stream_component_open video failed");
        goto failed;
    }


    // 输出文件信息
    LOG_DEBUG(DEBUG_PLAYER | DBG_TRACE,"-------------File Information-------------\n");
    av_dump_format(is->ic, 0, is->filename, 0);
    LOG_DEBUG(DEBUG_PLAYER | DBG_TRACE,"------------------------------------------\n");

    // 分配数据包
    packet = av_packet_alloc();
    av_init_packet(packet);
    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "开始读取数据...");
    for (;;)
    {
        if (is->abort_request)
        {
            LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "quit, size = %d",
                      is->audioq->size + is->videoq->size);
            break;
        }
        if(is->audioq->size + is->videoq->size > MAX_QUEUE_SIZE)
        {
            av_usleep(10*1000);
            continue;
        }
        if((ret = av_read_frame(is->ic, packet)) < 0)
        {
            // 没有更多包可读
            if ((ret == AVERROR_EOF || avio_feof(is->ic->pb)) && !is->eof)
            {
                LOG_DEBUG(DEBUG_VIDEO_DECODE | DBG_STATE, "push null packet");
                packet_queue_put_nullpacket(is->videoq, is->videoindex);
                packet_queue_put_nullpacket(is->audioq, is->audioindex);
                is->eof = true;            // 文件读取结束
            }
            av_usleep(10*1000);
            continue;
        }

        // Is this a packet from the video stream?.
        if (packet->stream_index == is->videoindex)
        {
            packet_queue_put(is->videoq, packet);
        }
        else if (packet->stream_index == is->audioindex)
        {
            packet_queue_put(is->audioq, packet);
        }
        else
        {
            av_packet_unref(packet);
        }
    }
    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "av_packet_free");
    av_packet_free(&packet);

    goto release;
failed:
    // 请求关闭程序
    is->quit = true;
release:
    avformat_close_input(&is->ic);

    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "read_thread finish");
    return 0;
}

/**
 * @brief 初始化显示
 * @return
 */
int display_init(void)
{

    window = SDL_CreateWindow("Simple Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              default_width, default_height, SDL_WINDOW_RESIZABLE);  //SDL_WINDOW_OPENGL
    if(!window)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "SDL: could not create window - %s\n",
                  SDL_GetError());
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, 0);
    if(!renderer)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "SDL_CreateRenderer failed");
        return -1;
    }
    return 0;
}

/**
 * @brief 打开显示，在解出第一帧后再调用，只被调用一次
 * @param is
 * @return
 */
int display_open(VideoState *is)
{
    int w,h;

    if (screen_width)
    {
        w = screen_width;
        h = screen_height;
    }
    else
    {
        w = default_width;
        h = default_height;
    }

    SDL_SetWindowTitle(window, is->filename);
    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    vid_texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV,
                                     SDL_TEXTUREACCESS_STREAMING, w, h);
    if(!vid_texture)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "SDL_CreateTexture failed");
        return -1;
    }

    is->width  = w;
    is->height = h;

    sdlRect.x = 0;
    sdlRect.y = 0;
    sdlRect.w = screen_width;
    sdlRect.h = screen_height;

    return 0;
}

void display_frame(AVFrame *frame)
{
    SDL_UpdateYUVTexture(vid_texture, &sdlRect,
                         frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1],
            frame->data[2], frame->linesize[2]);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, vid_texture, NULL, &sdlRect);
    SDL_RenderPresent(renderer);
}

#undef main
int main(int argc, char *argv[])
{
    LOG_DEBUG(DEBUG_PLAYER | DBG_ALWAYS_OUTPUT, "探索播放器的世界...");
    VideoState  *is = NULL;
    SDL_Event   event;
    AVPacket    *packet = NULL;
    AVFrame     *frame = NULL;
    int ret = 0;

    is = (VideoState *) av_mallocz( sizeof(VideoState) );   /* 分配结构体并初始化 */
    if ( !is )
    {
        return -1;
    }
    global_video_state = is;

    // SDL初始化
    if(SDL_Init(SDL_INIT_VIDEO  | SDL_INIT_TIMER) < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_HALT, "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    // 初始化显示界面
    if(display_init() < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_HALT, "display_init failed");
        return -1;
    }
    is->videoq = packet_queue_init();
    is->audioq = packet_queue_init();
    if(!is->videoq || !is->audioq)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_HALT, "packet_queue_init failed");
        return -1;
    }

    if(argc != 2)
    {
        strcpy(is->filename, "source.200kbps.768x320.flv"); // 默认打开文件
    }
    else
        strcpy(is->filename, argv[1]);
    // 创建一个刷新线程，刷新线程定义发送刷新信号给主线程，触发主线程解码和显示画面
    is->read_tid = SDL_CreateThread(read_thread, "read_thread", is );
    is->refresh_tid = SDL_CreateThread(frame_refresh_thread, "refresh_thread", is);

    frame   = av_frame_alloc();
    packet  = av_packet_alloc();

    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "进入主循环....\n");
    while(!is->quit)      // 主循环
    {
        if(SDL_WaitEvent(&event) != 1)
            continue;

        switch(event.type)
        {
        case SDL_KEYDOWN:
            if(event.key.keysym.sym == SDLK_ESCAPE)
                is->quit = true;
            if(event.key.keysym.sym == SDLK_SPACE)
                is->pause = !is->pause;
            break;

        case SDL_QUIT:      // 窗口关闭命令
            LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "收到窗口关闭命令，请求退出程序");
            is->quit = true;
            break;

        case FRAME_REFRESH_EVENT:
            ret = packet_queue_get(is->videoq, packet, 1);
            if(ret  < 0)
            {
                is->quit = true;
                break;
            }
            else if(ret == 0)           // 没有数据输出
            {
                break;
            }
            else if(ret == 1)           // 有数据
            {
                // 发送要解码的数据
                ret = avcodec_send_packet(is->vid_codec_ctx, packet);
                if( ret != 0 )
                {
                    av_packet_unref(packet);
                    break;
                }
            }

            // 获取解码后的帧
            do
            {
                ret = avcodec_receive_frame(is->vid_codec_ctx, frame);
                if(ret == 0)
                {
                    // 成功获取到解码后的帧
                    if(!is->is_display_open)
                    {
                        display_open(is);
                        is->is_display_open = true;
                    }
//                    static int s_frame_count = 0;
//                    LOG_DEBUG(DEBUG_VIDEO_DECODE | DBG_STATE, "frame[%d] type = %s\n",
//                              ++s_frame_count, s_picture_type[frame->pict_type]);
                    display_frame(frame);      // 显示
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
                    avcodec_flush_buffers(is->vid_codec_ctx);// YUV 1920*1080*1.5*5个buffer
                    LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "avcodec_flush_buffers");
                    is->quit = true;          // 退出播放
                    break;
                }
                else if(ret < 0)
                {
                    LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "if(ret < 0)\n");
                    break;
                }
            } while(ret != AVERROR(EAGAIN));

            av_packet_unref(packet);        // 释放内存

            break;
        default:
            //printf("unknow sdl event.......... event.type = %x\n", event.type);
            break;
        }
    }
    audio_stop();
    // 请求结束 read_thread
    packet_queue_abort(is->videoq);
    packet_queue_abort(is->audioq);
    is->abort_request = 1;
    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "SDL_WaitThread(is->refresh_tid, NULL);");
    SDL_WaitThread(is->refresh_tid, NULL);
    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "SDL_WaitThread(is->read_tid, NULL);");
    SDL_WaitThread(is->read_tid, NULL);

    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "packet_queue_destroy");
    if(is->audioq)
    packet_queue_destroy(&is->audioq);  // 注意音频队列 回调函数
    if(is->videoq)
    packet_queue_destroy(&is->videoq);

    if(vid_texture)
        SDL_DestroyTexture(vid_texture);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    SDL_Quit();

    if(is->vid_codec_ctx)
        avcodec_free_context(&is->vid_codec_ctx);
    if(is->aud_codec_ctx)
        avcodec_free_context(&is->aud_codec_ctx);

    av_packet_free(&packet);
    av_frame_free(&frame);

    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "程序结束\n");

    return 0;
}
