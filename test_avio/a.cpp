#include<iostream>

extern "C"{
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/file.h>
#include <libavutil/mem.h>
}

#define INPUT_FILE "1.mp4"

struct buffer_data {
    uint8_t* ptr;
    size_t size; ///< size left in the buffer
};

static int read_packet(void* opaque, uint8_t* buf, int buf_size)
{
    struct buffer_data* bd = (struct buffer_data*)opaque;
    buf_size = FFMIN(buf_size, bd->size);

    if (!buf_size)
        return AVERROR_EOF;
    printf("ptr:%p size:%zu\n", bd->ptr, bd->size);

    /* copy internal buffer data to buf */
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr += buf_size;
    bd->size -= buf_size;

    return buf_size;
}

int main(int argc, char* argv[])
{
    AVFormatContext* fmt_ctx = NULL;
    AVIOContext* avio_ctx = NULL;
    uint8_t* buffer = NULL, * avio_ctx_buffer = NULL;
    size_t buffer_size, avio_ctx_buffer_size = 4096;
    char* input_filename = NULL;
    int ret = 0;
    struct buffer_data bd = { 0 };
    int  videoStreamIndex = -1;
    AVCodecParameters* avCodecPara = NULL;
    const AVCodec* codec = NULL;
    AVCodecContext* codecCtx = NULL;
    AVPacket* pkt = NULL;

    input_filename = INPUT_FILE;

    /* slurp file content into buffer */
    ret = av_file_map(input_filename, &buffer, &buffer_size, 0, NULL);
    if (ret < 0)
        goto end;

    /* fill opaque structure used by the AVIOContext read callback */
    bd.ptr = buffer;
    bd.size = buffer_size;

    if (!(fmt_ctx = avformat_alloc_context())) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    avio_ctx_buffer = (uint8_t*)av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
        0, &bd, &read_packet, NULL, NULL);
    if (!avio_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    fmt_ctx->pb = avio_ctx;

    ret = avformat_open_input(&fmt_ctx, NULL, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open input\n");
        goto end;
    }

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not find stream information\n");
        goto end;
    }

    //av_dump_format(fmt_ctx, 0, input_filename, 0);
    printf("完成\n");

    //循环查找视频中包含的流信息，直到找到视频类型的流
    //便将其记录下来 保存到videoStreamIndex变量中
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;//找到视频流就退出
        }
    }

    //如果videoStream为-1 说明没有找到视频流
    if (videoStreamIndex == -1) {
        printf("cannot find video stream\n");
        goto end;
    }

    //=================================  查找解码器 ===================================//
    avCodecPara = fmt_ctx->streams[videoStreamIndex]->codecpar;
    codec = avcodec_find_decoder(avCodecPara->codec_id);
    if (codec == NULL) {
        printf("cannot find decoder\n");
        goto end;
    }
    //根据解码器参数来创建解码器内容
    codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, avCodecPara);
    if (codecCtx == NULL) {
        printf("Cannot alloc context.");
        goto end;
    }

    //================================  打开解码器 ===================================//
    if ((ret = avcodec_open2(codecCtx, codec, NULL)) < 0) { // 具体采用什么解码器ffmpeg经过封装 我们无须知道
        printf("cannot open decoder\n");
        goto end;
    }

    //=========================== 分配AVPacket结构体 ===============================//
    int       i = 0;//用于帧计数
    pkt = av_packet_alloc();                      //分配一个packet
    av_new_packet(pkt, codecCtx->width * codecCtx->height); //调整packet的数据

    //===========================  读取视频信息 ===============================//
    while (av_read_frame(fmt_ctx, pkt) >= 0) { //读取的是一帧视频  数据存入一个AVPacket的结构中
        if (pkt->stream_index == videoStreamIndex) {
            i++;//只计算视频帧
        }
        av_packet_unref(pkt);//重置pkt的内容
    }
    printf("There are %d frames int total.\n", i);

end:
    avformat_close_input(&fmt_ctx);

    /* note: the internal buffer could have changed, and be != avio_ctx_buffer */
    if (avio_ctx)
        av_freep(&avio_ctx->buffer);
    avio_context_free(&avio_ctx);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    av_file_unmap(buffer, buffer_size);
    avformat_free_context(fmt_ctx);

    if (ret < 0) {
        // fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        char av_err_buf[1024];
        av_strerror(ret, av_err_buf, sizeof(av_err_buf));
        fprintf(stderr, "Error occurred: %s\n", av_err_buf);
        return 1;
    }

    return 0;
}