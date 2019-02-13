/**********************************************
 * Date:  2018��10��12��
 * Description: ����Ƶ������ v0.1
 *
 **********************************************/
/************************************************
 * @file main.c
 * @brief ����Ƶ������ v0.1
 * @details v0.1 ����֧����Ƶ���ţ���Ƶˢ��������������
 * @mainpage ���̸���
 * @author Liao Qingfu
 * @email 592407834@qq.com
 * @version 0.1
 * @date 2018-10-12
 ***********************************************/


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

#include "log.h"
#include "packet_queue.h"
#include "util_time.h"


typedef struct VideoState
{
    AVFormatContext *ic;
    int		abort_request;      // =1ʱ�����˳�����

    // ��Ƶ���
    AVStream    *video_st;
    PacketQueue videoq;
    int         videoindex;
    AVCodecContext  *vid_codec_ctx;
    AVStream    *vstream;
    int         frame_rate;     // ֡��

    // ��Ƶ���
    AVStream    *audio_st;
    PacketQueue audioq;
    int         audioindex;
    AVCodecContext  *aud_codec_ctx;
    AVStream    *astream;
    int         sample_rate;     // ������

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

// Since we only have one decoding thread, the Big Struct can be global in case we need it.
VideoState *global_video_state = NULL;


// SDL �ⲿ����Ҫ����ʾ��أ�
static int default_width  = 640;
static int default_height = 480;
int screen_width = 0;
int screen_height = 0;
static SDL_Window   *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture  *vid_texture = NULL;
static SDL_Rect     sdlRect;

// ���������ݻ������
#define MAX_QUEUE_SIZE (1 * 1024 * 1024)


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

// �Զ���SDL�¼�
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
            SDL_Delay(1000/is->frame_rate);     // ������Ʋ����ٶȣ���ʱ����Ϊ��������ʹ���˱��١�
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

static int read_thread(void *arg)
{
    VideoState		*is	= (VideoState *) arg;
    AVCodec         *pcodec;
    AVPacket        *packet;
    int             ret;

    // ����⸴�������ڴ棬ʹ��avformat_close_input�ͷ�
    is->ic = avformat_alloc_context();
    if (!is->ic)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] Could not allocate context");
        goto failed;
    }

    // ����url����������ѡ��ƥ��Ľ⸴����
    ret = avformat_open_input(&is->ic, is->filename, NULL, NULL);
    if(ret != 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] avformat_open_input: %s", av_get_err(ret));
        goto failed;
    }

    // ��ȡý���ļ��Ĳ������ݰ��Ի�ȡ������Ϣ
    ret = avformat_find_stream_info(is->ic, NULL);
    if(ret < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] avformat_find_stream_info: %s", av_get_err(ret));
        goto failed;
    }

    // ���ҳ��ĸ�������video/audio/subtitles
    is->audioindex = -1;
    is->videoindex = -1;
    is->videoindex = av_find_best_stream(is->ic, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if(is->videoindex == -1)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "Didn't find a video stream");
        goto failed;
    }
    is->vstream =  is->ic->streams[is->videoindex];
    is->frame_rate = is->vstream->avg_frame_rate.num / is->vstream->avg_frame_rate.den;

    // ����������������ڴ棬ʹ��avcodec_free_context���ͷ�
    is->vid_codec_ctx = avcodec_alloc_context3(NULL);
    if(!is->vid_codec_ctx)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] avcodec_alloc_context3: %s", av_get_err(ret));
        goto failed;
    }
    // �������еı��������ϢAVCodecParameters������AVCodecContex
    ret = avcodec_parameters_to_context(is->vid_codec_ctx, is->ic->streams[is->videoindex]->codecpar);
    if(ret < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] avcodec_parameters_to_context: %s", av_get_err(ret));
        goto failed;
    }
    // ���ҽ�����
    pcodec = avcodec_find_decoder(is->vid_codec_ctx->codec_id);
    if(pcodec == NULL)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error] Video Codec not found");
        goto failed;
    }
    // �򿪽�����
    ret = avcodec_open2(is->vid_codec_ctx, pcodec, NULL);
    if(ret < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_ERROR, "[error]avcodec_open2 failed: %s", av_get_err(ret));
        goto failed;
    }
    screen_width = is->vid_codec_ctx->width;
    screen_height = is->vid_codec_ctx->height;

    // ����ļ���Ϣ
    LOG_DEBUG(DEBUG_PLAYER | DBG_TRACE,"-------------File Information-------------\n");
    av_dump_format(is->ic, 0, is->filename, 0);
    LOG_DEBUG(DEBUG_PLAYER | DBG_TRACE,"------------------------------------------\n");

    // �������ݰ�
    packet = av_packet_alloc();
    av_init_packet(packet);
    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "��ʼ��ȡ����...");
    for (;;)
    {
        if (is->abort_request)
        {
            LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "quit, size = %d",
                      is->audioq.size + is->videoq.size);
            break;
        }
        if(is->audioq.size + is->videoq.size > MAX_QUEUE_SIZE)
        {
            av_usleep(10*1000);
            continue;
        }
        if((ret = av_read_frame(is->ic, packet)) < 0)
        {
            // û�и�����ɶ�
            if ((ret == AVERROR_EOF || avio_feof(is->ic->pb)) && !is->eof)
            {
                LOG_DEBUG(DEBUG_VIDEO_DECODE | DBG_STATE, "push null packet");
                packet_queue_put_nullpacket(&is->videoq, is->videoindex);
                packet_queue_put_nullpacket(&is->audioq, is->audioindex);
                is->eof = true;            // �ļ���ȡ����
            }
            av_usleep(10*1000);
            continue;
        }

        // Is this a packet from the video stream?.
        if (packet->stream_index == is->videoindex)
        {
            packet_queue_put(&is->videoq, packet);
        }
        //        else if (packet->stream_index == is->audioindex)
        //        {
        //            packet_queue_put(&is->audioq, packet);
        //        }
        else
        {
            av_packet_unref(packet);
        }
    }
    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "av_packet_free");
    av_packet_free(&packet);

    goto release;
failed:
    // ����رճ���
    is->quit = true;
release:
    avformat_close_input(&is->ic);

    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "read_thread finish");
    return 0;
}

/**
 * @brief ��ʼ����ʾ
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
 * @brief ����ʾ���ڽ����һ֡���ٵ��ã�ֻ������һ��
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

// ֻ���������Խ����������Ƿ�����
void save_yuv(FILE *file, AVFrame *frame)
{
    int height = frame->height;
    int width = frame->width;

    char* buf = new char[width * height * 3 / 2];
    memset(buf, 0, height * width * 3 / 2);

    int a = 0, i;
    for (i = 0; i<height; i++)
    {
        memcpy(buf + a, frame->data[0] + i * frame->linesize[0], width);
        a += width;
    }
    for (i = 0; i<height / 2; i++)
    {
        memcpy(buf + a, frame->data[1] + i * frame->linesize[1], width / 2);
        a += width / 2;
    }
    for (i = 0; i<height / 2; i++)
    {
        memcpy(buf + a, frame->data[2] + i * frame->linesize[2], width / 2);
        a += width / 2;
    }
    fwrite(buf, 1, width * height * 3 / 2, file);
}

#undef main
int main(int argc, char *argv[])
{
    LOG_DEBUG(DEBUG_PLAYER | DBG_ALWAYS_OUTPUT, "̽��������������...");
    VideoState  *is = NULL;
    SDL_Event   event;
    AVPacket    *packet = NULL;
    AVFrame     *frame = NULL;
    int ret = 0;

    is = (VideoState *) av_mallocz( sizeof(VideoState) );   /* ����ṹ�岢��ʼ�� */
    if ( !is )
    {
        return -1;
    }
    global_video_state = is;

    FILE *yuv_file = fopen("yuv_file.yuv","wb+");
    if (!yuv_file)
    {
        return 0;
    }
    // SDL��ʼ��
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0)
    {
        LOG_DEBUG(DEBUG_PLAYER | DBG_HALT, "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    // ��ʼ����ʾ����
    if(display_init() < 0)
    {
        return -1;
    }
    packet_queue_init(&is->videoq);
    packet_queue_init(&is->audioq);

    if(argc != 2)
    {
        strcpy(is->filename, "source.200kbps.768x320.flv"); // Ĭ�ϴ��ļ�
    }
    else
        strcpy(is->filename, argv[1]);
    // ����һ��ˢ���̣߳�ˢ���̶߳��巢��ˢ���źŸ����̣߳��������߳̽������ʾ����
    is->read_tid = SDL_CreateThread(read_thread, "read_thread", is );
    is->refresh_tid = SDL_CreateThread(frame_refresh_thread, "refresh_thread", is);

    frame   = av_frame_alloc();
    packet  = av_packet_alloc();

    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "������ѭ��....\n");
    while(!is->quit)      // ��ѭ��
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

        case SDL_QUIT:      // ���ڹر�����
            LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "�յ����ڹر���������˳�����");
            is->quit = true;
            break;

        case FRAME_REFRESH_EVENT:
            ret = packet_queue_get(&is->videoq, packet, 1);
            if(ret  < 0)
            {
                is->quit = true;
                break;
            }
            else if(ret == 0)           // û���������
            {
                break;
            }
            else if(ret == 1)           // ������
            {
                // ����Ҫ���������
                if(!packet->buf)
                {
                    LOG_DEBUG(DEBUG_VIDEO_DECODE | DBG_STATE, "flush null packet");
                    LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, " size = %d",
                              is->videoq.size);

                }

                ret = avcodec_send_packet(is->vid_codec_ctx, packet);
                if( ret != 0 )
                {
                    av_packet_unref(packet);
                    break;
                }
            }

            // ��ȡ������֡
            do
            {
                ret = avcodec_receive_frame(is->vid_codec_ctx, frame);
                if(ret == 0)
                {
                    // �ɹ���ȡ��������֡
                    if(!is->is_display_open)
                    {
                        display_open(is);
                        is->is_display_open = true;
                    }
//                    static int s_frame_count = 0;
//                    LOG_DEBUG(DEBUG_VIDEO_DECODE | DBG_STATE, "frame[%d] type = %s\n",
//                              ++s_frame_count, s_picture_type[frame->pict_type]);
                    display_frame(frame);      // ��ʾ

                    //save_yuv(yuv_file, frame);
                }
                else if(ret == AVERROR(EAGAIN))
                {
                    // û��֡�ɶ����ȴ���һ��������ٶ�ȡ
                    //                    LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "EAGAIN");
                    break;
                }
                else if(ret == AVERROR_EOF)
                {
                    // ����������֡���Ѿ�����ȡ
                    avcodec_flush_buffers(is->vid_codec_ctx);// YUV 1920*1080*1.5*5��buffer
                    LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "avcodec_flush_buffers");
                    is->quit = true;          // �˳�����
                    break;
                }
                else if(ret < 0)
                {
                    LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_STATE, "if(ret < 0)\n");
                    break;
                }
            } while(ret != AVERROR(EAGAIN));

            av_packet_unref(packet);        // �ͷ��ڴ�

            break;
        default:
            //printf("unknow sdl event.......... event.type = %x\n", event.type);
            break;
        }
    }

    // ������� read_thread
    packet_queue_abort(&is->videoq);
    packet_queue_abort(&is->audioq);
    is->abort_request = 1;
    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "SDL_WaitThread(is->refresh_tid, NULL);");
    SDL_WaitThread(is->refresh_tid, NULL);
    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "SDL_WaitThread(is->read_tid, NULL);");
    SDL_WaitThread(is->read_tid, NULL);

    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "packet_queue_flush");
    packet_queue_flush(&is->videoq);
    packet_queue_flush(&is->audioq);

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

    fclose(yuv_file);

    LOG_DEBUG(DEBUG_PLAYER | DBG_STATE, "�������\n");

    return 0;
}
