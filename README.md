## Qt+ffmpeg+sdl简单播放器

## 2019-2-15
音视频同时播放，暂未进行音视频同步  
sdl2声音播放原理见雷神: 
https://blog.csdn.net/leixiaohua1020/article/details/40544521

## 2019-2-13
简单视频播放器（直接解码sdl渲染播放）  
https://blog.csdn.net/FlayHigherGT/article/details/85690386

## 基本流程解析：
pFormatCtx = avformat_alloc_context();                                             
首先给上下文分配内存  

ret = avformat_open_input(&pFormatCtx, filePath, NULL, NULL);   
之后打开媒体文件，这里面应该会将媒体格式信息存储在上下文中  

av_dump_format(pFormatCtx, 0, filePath, 0);                                      
这里可以打印出媒体格式的详细信息  

ret = avformat_find_stream_info(pFormatCtx, NULL);                      
在上下文中寻找视频流信息  

videoindex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);   
这里是找到视频流的index标志号
 
avStream = pFormatCtx->streams[videoindex];                              
设置avstream这个东西
 
g_frame_rate = avStream->avg_frame_rate.num / avStream->avg_frame_rate.den; 
这里可以设置一下帧率
 
pCodecCtx = avcodec_alloc_context3(NULL);                                   
这里打开解码器上下文
 
ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar);   
将媒体信息的参数传入解码器上下文
 
pCodec = avcodec_find_decoder(pCodecCtx->codec_id);               
在解码器上下文中找到解码器
 
ret = avcodec_open2(pCodecCtx, pCodec, NULL);                           
打开解码器
 
screen_w = pCodecCtx->width;  
这里可以从解码器上下文中获取到解码视频的宽
 
screen_h = pCodecCtx->height; 
这里可以从解码器上下文中获取到解码视频的高可以给渲染窗口用
 
packet = av_packet_alloc();
av_init_packet(packet);          
分配一下视频一帧的压缩包数据结构，并初始化
 
pFrame = av_frame_alloc(); 
分配解码后的一帧数据的数据结构
 
while(1)
 
{  
    //这里是不断读取需要解码的包送到解码器里面  
    ret = av_read_frame(pFormatCtx, packet); 这里while进行循环读取
 
    1、ret < 0 没有包可以读，可以结束循环  
    2、ret == 0 && packet->stream_index != videoindex 这里表示没有读取到视频流  
    3、ret == 0 {//这里表示正常的视频包  
    avcodec_send_packet(pCodecCtx, packet);//这里发送要解码的数据到解码器上下文中  
         }else  
         {  
            av_packet_unref(packet);// 如果还占用内存则释放  
            ret = avcodec_send_packet(pCodecCtx, packet);这里如果不是0的话需要刷一个空包进去  
         }
 
    //这里是读取解码器里面需要解码的包进行解码  
    do{  
        ret = avcodec_receive_frame(pCodecCtx, pFrame);  
        1、ret == 0 表示成功，可以从pFrame中读取到解码帧数据送到渲染器渲染  
        2、ret == AVERROR(EAGAIN)    说明没有可读的解完的帧数据  
        3、ret == AVERROR_EOF    说明没有帧了
 
        avcodec_flush_buffers(pCodecCtx);// YUV 1920*1080*1.5*5个buffer 这里需要做一个刷新
 
    }while(ret != AVERROR(EAGAIN))一直读取知道没有可渲染的帧  
}
