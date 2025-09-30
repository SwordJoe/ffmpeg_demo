#include<iostream>
#include<fstream>
#include<string>
#include<cstring>

extern "C"{
#include<libavutil/frame.h>
#include<libavcodec/avcodec.h>
#include<libavformat/avformat.h>
}

#define VIDEO_INNBUF_SIZE 20480
#define VIDEO_REFILL_THRESH 4096
#define PRINT_AV_ERROR(ret) do { \
            char errbuf[256]; \
            memset(errbuf, 0, sizeof(errbuf)); \
            av_strerror((ret), errbuf, sizeof(errbuf)); \
            printf("%s\n", errbuf); \
} while (0)



static void decode(AVCodecContext* decode_ctx, AVPacket* pkt, AVFrame* decode_frame, std::ofstream& ofs);
static void dump_video_paramets(const AVFrame* frame);
static void probeInputFormat(std::string& infile, AVCodecID& codec_id, std::ifstream& ofs);
static void dumpYUV2file(AVFrame* decode_frame, std::ofstream& ofs);
static void dumpFrameTypeCnt();
static int g_dump_flag = 0;
static int I_frame_cnt = 0;
static int framt_type_cnt[7] = {0};

int main(int argc, char**argv)
{
    if( argc != 3){
        printf("Usage: %s <input file> <output file>", argv[0]);
        return 0;
    }
    std::string input_filename = std::string(argv[1]);
    std::string output_filename = std::string(argv[2]);
    std::ifstream ifs(input_filename, std::ios::in | std::ios::binary);
    std::ofstream ofs(output_filename, std::ios::out | std::ios::binary);
    if( !ifs.is_open() || !ofs.is_open() ){
        printf("open input_file or out_put file faile!");   return 0;
    }

    AVCodecID video_codec_id = AV_CODEC_ID_H264;
    // probeInputFormat(input_filename, video_codec_id, ifs);

    //根据解码器id获取对应的解码器信息
    //特别注意：AVCodec类型只是一个只读的全局对象，它只是用来描述（定义）编解码器信息，真正处理编码解码的类是AVCodecContext
    const AVCodec* codec = avcodec_find_decoder(video_codec_id);        
    if( !codec ){
        printf("codec not found");  return 0;
    }

    //分配解码器codec上下文
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if( !codec_ctx){
        printf("codec_ctx alloc failed!");  return 0;
    }

    //用解码器信息codec来初始化解码器上下文codec_ctx
    int ret = avcodec_open2(codec_ctx, codec, nullptr);
    if( ret != 0){
        printf("avcodec_open failed!");     return 0;
    }

    //获取解析裸流的解析器
    AVCodecParserContext* parser_ctx = av_parser_init(video_codec_id);
    if( !parser_ctx ){
        printf("init AVCodecParserContext failed");   return 0;
    }

    //存放h264编码数据，从h264文件中读取的裸流就放在这里面
    AVPacket* pkt = av_packet_alloc();
    if( !pkt ){
        printf("alloc AVPacket failed!\n"); return 0;
    }

    //存放解码后的视频帧，在本示例中AVFrame中存放yuv类型的数据
    AVFrame* decode_frame = av_frame_alloc(); 
    if( !decode_frame ){
        printf("alloc AVFrame failed!\n");   return 0;
    }

    //从文件读取的h264裸码流保存在inbuf中
    uint8_t inbuf[VIDEO_INNBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    const uint8_t* data = inbuf;
    ifs.read((char*)inbuf, VIDEO_INNBUF_SIZE);
    int data_size = ifs.gcount();

    while( true ){
        if( data_size == 0){
            break;
        }

        //解析器的作用从连续的码流中解析出完整的一帧帧编码数据单元（packet 边界）
        int parsed_len = av_parser_parse2(parser_ctx, codec_ctx, 
                                &pkt->data, &pkt->size, 
                                data, data_size, 
                                AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if( parsed_len < 0 ){
            printf("parse failed!\n");  return 0;
        }

        data += parsed_len;
        data_size -= parsed_len;
        
        //有解析出的裸数据
        if( pkt->size ){
            decode(codec_ctx, pkt, decode_frame, ofs);
        }

        if( data_size < VIDEO_REFILL_THRESH ){
            memmove(inbuf, data, data_size);
            data = inbuf;
            ifs.read((char*)inbuf, VIDEO_INNBUF_SIZE - data_size);
            data_size += ifs.gcount();
        }

    }

    /* 冲刷解码器 */
    pkt->data = NULL;   // 让其进入drain mode
    pkt->size = 0;
    decode(codec_ctx, pkt, decode_frame, ofs);

    dumpFrameTypeCnt();

    av_packet_free(&pkt);
    av_frame_free(&decode_frame);
    av_packet_free(&pkt);
    av_parser_close(parser_ctx);
    return 0;
}

static void decode(AVCodecContext* decode_ctx, AVPacket* avpkt, AVFrame* decode_frame, std::ofstream& ofs)
{
    //将编码包pkt送入解码器
    auto ret = avcodec_send_packet(decode_ctx, avpkt);
    char errbuf[256];
    if( ret != 0) {
        PRINT_AV_ERROR(ret);
        return;
    }

    while(ret >= 0 ){
        //从解码器接收解码后的数据
        ret = avcodec_receive_frame(decode_ctx, decode_frame);
        if( ret != 0){
            if( ret != AVERROR(EAGAIN) && ret != AVERROR_EOF){
                PRINT_AV_ERROR(ret);
                exit(0);
            }
            break;
        }
        //打印一些视频参数
        dump_video_paramets(decode_frame);

        //将解析出来的yuv数据写入yuv文件
        dumpYUV2file(decode_frame, ofs);

        auto idx = (int)decode_frame->pict_type;
        framt_type_cnt[idx]++;
    }
}

static void dump_video_paramets(const AVFrame* frame)
{
    if( g_dump_flag == 0){
        printf("width: %d\n", frame->width);
        printf("height: %d\n", frame->height);
        printf("format: %d\n", frame->format);
        g_dump_flag = 1;
    }
}

static void probeInputFormat(std::string& infile, AVCodecID& codec_id, std::ifstream& ifs)
{
    AVProbeData pd;
    uint8_t probe_buf[16384];
    ifs.read((char*)probe_buf, sizeof(probe_buf));
    pd.buf = (unsigned char*)probe_buf;
    pd.buf_size = ifs.gcount();
    pd.filename = infile.c_str();

    //清除 eof/error 标志，不改变 ifstream 的位置
    ifs.clear();
    ifs.seekg(0, std::ios::beg);
    
    const AVInputFormat* inputformat = av_probe_input_format(&pd, 0);
    printf("inputformat name:\t\t%s\n", inputformat->name);
    printf("inputformat long_name: \t\t%s\n", inputformat->long_name);

    if( strcmp(inputformat->name, "h264") == 0 ){
        codec_id = AV_CODEC_ID_H264;
    } else if( strcmp(inputformat->name, "mpeg2") == 0 ){
        codec_id = AV_CODEC_ID_MPEG2VIDEO;
        printf("unsupported video codec id\n");
    } else {
        exit(0);
    }
}

static void dumpYUV2file(AVFrame* decode_frame, std::ofstream& ofs)
{
    //Tips: 将解码yuv数据写入文件时要特别注意!!! AVFrame里的视频解码数据是按照平面来排列的
    //data[0]保存Y平面数据，data[1]保存U平面数据，data[2]...
    //data[0]保存的数据又可以切分为frame->height个小单元，也就是N行Y平面数据
    //但是每行的数据量并不完全等于frame->width，每行的数据量其实为frame->linesize[0]指定的。因为ffmpeg可能会在每行后面多加一些字节，内存对齐等原因
    //所以写入文件的时候，并不是每行写入frame->linesize[0]个字节，而是每行写入frame->width个字节数据
    for(int i = 0; i < decode_frame->height; ++i ){
        ofs.write((const char*)decode_frame->data[0] + i * decode_frame->linesize[0], decode_frame->width);
    }
    for(int i = 0; i < decode_frame->height / 2; ++i ){
        ofs.write((const char*)decode_frame->data[1] + i * decode_frame->linesize[1], decode_frame->width / 2);
    }
    for(int i = 0; i < decode_frame->height / 2; ++i ){
        ofs.write((const char*)decode_frame->data[2] + i * decode_frame->linesize[2], decode_frame->width / 2);
    }
}

static void dumpFrameTypeCnt()
{
    printf("AV_PICTURE_TYPE_I: %d\n", framt_type_cnt[(int)AV_PICTURE_TYPE_I]);
    printf("AV_PICTURE_TYPE_P: %d\n", framt_type_cnt[(int)AV_PICTURE_TYPE_P]);
    printf("AV_PICTURE_TYPE_B: %d\n", framt_type_cnt[(int)AV_PICTURE_TYPE_B]);
    printf("AV_PICTURE_TYPE_S: %d\n", framt_type_cnt[(int)AV_PICTURE_TYPE_S]);
    printf("AV_PICTURE_TYPE_SI: %d\n", framt_type_cnt[(int)AV_PICTURE_TYPE_SI]);
    printf("AV_PICTURE_TYPE_SP: %d\n", framt_type_cnt[(int)AV_PICTURE_TYPE_SP]);
    printf("AV_PICTURE_TYPE_BI: %d\n", framt_type_cnt[(int)AV_PICTURE_TYPE_BI]);
}