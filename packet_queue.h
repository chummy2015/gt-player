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
    int nb_packets;             // ������
    int size;                   // ���д洢��ý�������ܴ�С
    SDL_mutex *mutex;
    SDL_cond *cond;
    int abort_request;          // �Ƿ��˳��ȴ�
} PacketQueue;

PacketQueue *packet_queue_init(void);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);
/**
 * @brief ��ȡ���ݰ�
 * @param q
 * @param pkt
 * @param block �Ƿ�����ʽ��ȡ���ݰ�
 * @return ����״̬
 *  @retval -1  �����˳�����
 *  @retval 0   û�����ݰ��ɻ�ȡ
 *  @retval 1   ��ȡ�����ݰ�
 */
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
void packet_queue_flush(PacketQueue *q);
int packet_queue_put_nullpacket(PacketQueue *q, int stream_index);
void packet_queue_abort(PacketQueue *q);
void packet_queue_destroy(PacketQueue **q);
#endif // PACKET_QUEUE_H
