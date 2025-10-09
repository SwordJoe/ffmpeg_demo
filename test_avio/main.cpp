#include <iostream>
extern "C"{
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/file.h>
#include <libavutil/mem.h>
}

static char errbuf[1024];
//用户自定义结构体
//将读取到的MP4文件数据整个都保存在ptr中
//size表示ptr指向的内存还剩多少没有被ffmpeg读取（还剩多少没有拷贝到ffmpeg的内存中）
struct opaque_data{
    uint8_t* start_ptr; //文件起始
    uint8_t* cur_ptr;     //当前读取位置
    size_t total_size;  //文件总大小
    size_t left_size;   //剩余字节
};

static int read_packet(void* opaque, uint8_t* buf, int buf_size){
    auto p = (struct opaque_data*)opaque;
    buf_size = FFMIN(buf_size, p->left_size);
    if( buf_size == 0 ){
        return AVERROR_EOF;
    }

    memcpy(buf, p->cur_ptr, buf_size);
    p->cur_ptr += buf_size;
    p->left_size -= buf_size;

    return buf_size;
}

static int64_t seek_packet(void* opaque, int64_t offset, int whence){
    auto p = (struct opaque_data*)opaque;
    switch( whence ){
        case SEEK_SET: p->cur_ptr = p->start_ptr + offset; break;       //从开始出偏移offset
        case SEEK_CUR: p->cur_ptr += offset; break;                     //从当前位置偏移ofsset
        case SEEK_END: p->cur_ptr = p->start_ptr + p->total_size + offset; break;   //从末尾偏移offset
        case AVSEEK_SIZE: return p->total_size; break;
        default: return -1;
    }
    p->left_size = p->start_ptr + p->total_size - p->cur_ptr;
    return p->cur_ptr - p->start_ptr;
}

int main(int argc, char** argv)
{
    if( argc != 2 ){
        printf("Usage: %s <input file>\n", argv[0]);
        return 0;
    }

    AVFormatContext* av_fmt_ctx{nullptr};
    AVIOContext* av_io_ctx{nullptr};
    const AVCodec* av_decoder{nullptr};
    AVCodecContext* av_decoder_ctx{nullptr};
    AVPacket* av_pkt{nullptr};

    uint8_t* buf{nullptr};
    size_t buf_size{0};
    uint8_t* avio_ctx_buf{nullptr};
    size_t avio_ctx_buf_size{4096};
    struct opaque_data opaque{0};
    int ret = 0;


    const char* input_file = argv[1];
    ret = av_file_map(input_file, &buf, &buf_size, 0, nullptr);
    if( ret < 0){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("av_file_map error, err msg: %s\n", errbuf);
        return 0;
    }
    //将从MP4文件读取到的数据保存到opaque_data结构体中
    //后面AVFormatContext实际就是调用read_packet从这里读取数据
    opaque.start_ptr = buf;
    opaque.cur_ptr = buf;
    opaque.total_size = buf_size;
    opaque.left_size = buf_size;

    if( !(av_fmt_ctx = avformat_alloc_context()) ){
        ret = AVERROR(ENOMEM);
        goto END;
    }

    if( !(avio_ctx_buf = (uint8_t*)(av_malloc(avio_ctx_buf_size))) ){
        ret = AVERROR(ENOMEM);
        goto END;
    }

    //将AVIOContext上下文和读回调函数进行关联
    if( !(av_io_ctx = avio_alloc_context(avio_ctx_buf, avio_ctx_buf_size, 0, &opaque, &read_packet, nullptr, &seek_packet))){
        ret = AVERROR(ENOMEM);
        goto END;
    }
    //对AVFormatContext上下文中的AVIOContext*变量进行赋值
    //不使用avio的话，AVFormatContext里的这个变量就是nullptr，AVFormatContext会自己分配
    av_fmt_ctx->pb = av_io_ctx;

    if( (ret = avformat_open_input(&av_fmt_ctx, nullptr, nullptr, nullptr)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avformat_open_input error: %s\n", errbuf);
        goto END;
    }

    //寻找视频流
    int videoStreamIndex = av_find_best_stream(av_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if( videoStreamIndex < 0 ){
        av_strerror(videoStreamIndex, errbuf, sizeof(errbuf));
        goto END;
    }
    AVStream* videoStream = av_fmt_ctx->streams[videoStreamIndex];
    
    //查找解码器信息
    auto codecId = videoStream->codecpar->codec_id;
    if( !(av_decoder = avcodec_find_decoder(codecId)) ){
        printf("can't find decoder!\n");
        goto END;
    }

    //根据解码器信息创建解码器上下文
    if( !(av_decoder_ctx = avcodec_alloc_context3(av_decoder)) ){
        printf("avcodec_alloc_context3 failed!\n");
        goto END;
    }

    //将从mp4中获取的解码参数传递给解码器上下文AVCodecContext
    if( (ret = avcodec_parameters_to_context(av_decoder_ctx, av_fmt_ctx->streams[videoStreamIndex]->codecpar)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avcodec_parameters_to_context error: %s\n",  errbuf);
        goto END;
    }

    //打开解码器
    if( (ret = avcodec_open2(av_decoder_ctx, av_decoder, nullptr)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avcodec_open2 error: %s\n", errbuf);
        goto END;
    }

    if( !(av_pkt = av_packet_alloc()) ){
        printf("av_packet_alloc failed!\n");
        goto END;
    }

    size_t frame_cnt{0};
    while( true ){
        ret = av_read_frame(av_fmt_ctx, av_pkt);
        if( ret == AVERROR_EOF ){
            break;
        } else if( ret < 0 ){
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("av_read_frame error: %s\n", errbuf);
            goto END;
        }
        if( av_pkt->stream_index == videoStreamIndex ){
            frame_cnt++;
        }
        av_packet_unref(av_pkt);
    }

    printf("Frames Cnt: %d\n", frame_cnt);

END:
    avformat_close_input(&av_fmt_ctx);
    if( av_io_ctx ){
        if( av_io_ctx->buffer ){
            av_freep(&av_io_ctx->buffer);
        }
        avio_context_free(&av_io_ctx);
    }
    av_packet_free(&av_pkt);
    avcodec_free_context(&av_decoder_ctx);
    avformat_free_context(av_fmt_ctx);
    av_file_unmap(buf, buf_size);

    return 0;
}