#include "packet_queue.h"
#include "log.h"
#include "util_time.h"

PacketQueue *packet_queue_init(void)
{
    PacketQueue *q = (PacketQueue *)av_mallocz(sizeof(PacketQueue));
    if(!q)
    {
        return NULL;
    }
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
    return q;
}
static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    AVPacketList  *pkt1;

    if (q->abort_request)
       return -1;

    pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;// + sizeof(*pkt1);

    /* XXX: should duplicate packet data in DV case */
    SDL_CondSignal(q->cond);
    return 0;
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    int ret;

    SDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q, pkt);
    SDL_UnlockMutex(q->mutex);

    if (ret < 0)
        av_packet_unref(pkt);       // 插入失败时释放内存

    return ret;
}

/**
 * @brief packet_queue_get
 * @param q
 * @param pkt
 * @param block
 * @return 队列状态
 *  @retval -1  播放退出请求
 *  @retval 0   没有数据包可获取
 *  @retval 1   获取到数据包
 */
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;)
    {
        if (q->abort_request)
        {
            ret = -1;
            LOG_DEBUG(DEBUG_PLAYER_QUIT | DBG_TRACE, "%s(%d) quit t = %lld",
                      __FUNCTION__, __LINE__, get_current_time_msec());
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1)
        {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
            {
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

void packet_queue_flush(PacketQueue *q)
{
    AVPacketList *pkt, *pkt1;
    int npackets = q->nb_packets;

    SDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt != NULL; pkt = pkt1)
    {
        pkt1 = pkt->next;
//        printf("pos = %d\n", pkt->pkt.pos);
        if(pkt->pkt.buf)
            av_packet_unref(&pkt->pkt);
//        printf("av_packet_unref\n");
        av_freep(&pkt);
//        printf("av_freep %d\n", --npackets);
        if(npackets == 1)
        {
            printf("av_freep 只剩%d\n", npackets);
        }
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    SDL_UnlockMutex(q->mutex);
}

int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}

void packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);

    q->abort_request = 1;

    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
}

void packet_queue_destroy(PacketQueue **q)
{
    packet_queue_flush(*q);
    av_free(*q);
    *q = NULL;
}
