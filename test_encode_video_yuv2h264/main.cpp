#include<iostream>
#include<fstream>
#include<chrono>
#include<filesystem>
extern "C"{
#include<libavcodec/avcodec.h>
#include<libavutil/avutil.h>
#include<libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#define LOG_INFO(msg){ \
    std::filesystem::path path(__FILE__); \
    std::cout << "[" << path.filename() << ":" << __LINE__ << " " << __func__ << "] " << msg << std::endl;\
}

char errbuf[1024];
static int encode(AVCodecContext* codec_ctx, AVFrame* av_frame, AVPacket* pkt, std::ofstream& ofs);

//提取yuv数据: ffmpeg -i a.mp4 -t 10 -r 25 -pix_fmt yuv420p source_3840x2160_10s.yuv
int main(int argc, char** argv)
{
    if( argc != 3){
        printf("Usage: %s <input file> <output file>\n", argv[0]);
        return 0;
    }

    std::string input_yuv_file(argv[1]);
    std::string output_h264_file(argv[2]);
    std::ifstream ifs(input_yuv_file, std::ios::in | std::ios::binary);
    std::ofstream ofs(output_h264_file, std::ios::out | std::ios::binary);
    if( !ifs.is_open() || !ofs.is_open() ){
        printf("open ifs or ofs failed!\n");
        return 0;
    }

    AVCodecContext* codec_ctx{nullptr};    
    const AVCodec* encoder{nullptr};            
    AVPacket* av_pkt{nullptr};            
    AVFrame* av_frame{nullptr};
    int ret = 0;

    if( !(encoder = avcodec_find_encoder_by_name("libx264")) ){
        printf("encoder not found\n");
        return 0;
    }

    if( !(codec_ctx = avcodec_alloc_context3(encoder)) ){
        printf("avcodec_alloc_context3 failed!\n");
        return 0;
    }

    //设置编码器上下文的编码参数
    codec_ctx->width = 2560;
    codec_ctx->height = 1600;
    codec_ctx->time_base = av_make_q(1,1000);
    codec_ctx->framerate = av_make_q(60,1);
    codec_ctx->gop_size = 60;   //I帧间隔
    codec_ctx->max_b_frames = 0;
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codec_ctx->priv_data, "profile", "main", 0);
    av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
    av_opt_set_double(codec_ctx->priv_data, "crf", 5, 0);


    //根据编码器encoder初始化编码器上下文codec_ctx
    if( (ret = avcodec_open2(codec_ctx, encoder, nullptr)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avcodec_open2 error: %s\n", errbuf);
        return 0;
    }

    if( !(av_pkt = av_packet_alloc()) ){
        printf("av_packet_alloc failed!\n");
        return 0;
    }

    if( !(av_frame = av_frame_alloc()) ){
        printf("av_frame_alloc failed!\n");
        return 0;
    }

    // //注意：比特率只有在CBR恒定码率下才有效，且设置了CBR必须设置bitrate
    // //CBR恒定码率编码情况下，编码器会尽量让每秒输出码率接近这个值, 所以一般这两个参数一起设置
    // av_opt_set(codec_ctx->priv_data, "rc", "cbr", 0);
    // codec_ctx->bit_rate = 30000;

    av_frame->width = codec_ctx->width;
    av_frame->height = codec_ctx->height;
    av_frame->format = codec_ctx->pix_fmt;
    if( (ret = av_frame_get_buffer(av_frame, 0)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("av_frame_get_buffer error: %s\n", errbuf);
        return 0;
    }

    int frame_bytes = av_image_get_buffer_size((AVPixelFormat)av_frame->format, av_frame->width, av_frame->height, 1);
    printf("one yuv AVFrame need bytes: %d\n", frame_bytes);

    uint8_t* yuv_buf = new uint8_t[frame_bytes];
    auto begin_time = std::chrono::high_resolution_clock::now();

    int frame_index = 1;
    while(1){
        memset(yuv_buf, 0, frame_bytes);
        ifs.read((char*)yuv_buf, frame_bytes);
        if( ifs.eof() ){
            printf("read yuv file finished!\n");
            break;
        } else if ( ifs.gcount() < frame_bytes ){
            printf("read yuv file failed\n");
            return 0;
        }

        if( (ret = av_frame_make_writable(av_frame)) < 0 ){
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("av_frame_make_writbale error: %s\n", errbuf);
            return 0;
        }

        ret = av_image_fill_arrays(av_frame->data, av_frame->linesize, yuv_buf, 
                            (AVPixelFormat)av_frame->format, av_frame->width, av_frame->height, 1);
        if( ret < 0 || ret != frame_bytes ){
            printf("av_image_fill_arrays failed!\n");
            continue;
        }
        av_frame->pts = frame_index++;      //用帧数来作为pts
        auto a_time = std::chrono::high_resolution_clock::now();
        if( (ret = encode(codec_ctx, av_frame, av_pkt, ofs)) < 0 ){
            printf("encode failed!\n");
            return 0;
        }
        auto b_time = std::chrono::high_resolution_clock::now();
        printf("encode time: %d ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(b_time - a_time));
    }
    //冲刷编码器
    if( (ret = encode(codec_ctx, nullptr, av_pkt, ofs)) < 0 ){
        printf("encode failed!\n");
        return 0;
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - begin_time);
    printf("TOTAL DURATION: %d ms\n", duration_ms);

    ifs.close();
    ofs.close();
    av_frame_free(&av_frame);
    av_packet_free(&av_pkt);
    avcodec_free_context(&codec_ctx);
    if( yuv_buf ){
        delete[] yuv_buf;
    }
 
    return 0;
}

static int encode(AVCodecContext* codec_ctx, AVFrame* av_frame, AVPacket* pkt, std::ofstream& ofs)
{
    if( av_frame ){
        // printf("%s_%s_%d\tsend frame %d\n", __FILE__, __func__, __LINE__, av_frame->pts);
        char msg[1024];
        sprintf(msg, "send frame: %d", av_frame->pts);
        LOG_INFO(msg);
    }
    int ret = 0;
    if( !(ret = avcodec_send_frame(codec_ctx, av_frame)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avcodec_send_frame error: %s\n", errbuf);
        return ret;
    }

    while(true){
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if( ret == AVERROR(EAGAIN) || ret == AVERROR_EOF ){
            return 0;
        } else if( ret < 0 ){
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("avcodec_receive_packet error: %s\n", errbuf);
            return ret;
        }

        if( pkt->flags & AV_PKT_FLAG_KEY ){
            printf("encoded pkt falgs: %d, pts: %lld, dts: %lld, size: %d\n",
                    pkt->flags, pkt->pts, pkt->dts, pkt->size);
        }

        ofs.write((char*)pkt->data, pkt->size);
    }

    return 0;
}