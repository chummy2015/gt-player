#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H




#ifdef __cplusplus
extern "C"
{
#include "libavformat/avformat.h"
#include "SDL2/include/SDL.h"
}
#endif

typedef struct PacketQueue
{
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;             // 包数量
    int size;                   // 队列存储的媒体数据总大小
    SDL_mutex *mutex;
    SDL_cond *cond;
    int abort_request;          // 是否退出等待
} PacketQueue;

PacketQueue *packet_queue_init(void);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);
/**
 * @brief 获取数据包
 * @param q
 * @param pkt
 * @param block 是否阻塞式获取数据包
 * @return 队列状态
 *  @retval -1  播放退出请求
 *  @retval 0   没有数据包可获取
 *  @retval 1   获取到数据包
 */
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
void packet_queue_flush(PacketQueue *q);
int packet_queue_put_nullpacket(PacketQueue *q, int stream_index);
void packet_queue_abort(PacketQueue *q);
void packet_queue_destroy(PacketQueue **q);
#endif // PACKET_QUEUE_H
