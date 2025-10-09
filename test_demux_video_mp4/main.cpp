#include<iostream>
#include<fstream>
#include<string>
extern "C"{
#include<libavformat/avformat.h>
#include<libavutil/error.h>
#include<libavcodec/bsf.h>
#include<libavcodec/codec.h>
}

char av_err_buf[1024];
const int av_err_buf_size = 1024;

#define CHECK_AV_RET(ret, api_name) do{\
        if( ret < 0){\
            printf("%s execute failed!\n", api_name);\
            av_strerror(ret, av_err_buf, av_err_buf_size);\
            return -1;\
        }\
} while(0)

#define CHECK_AV_ALLOC(ret, api_name) do{\
        if( ret == nullptr ){\
            printf("%s alloc failed!\n", api_name);\
            return -1;\
        }\
}while(0)

int main(int argc, char** argv)
{
    if( argc != 4){
        printf("Usage: %s <input.mp4> out.h264 out.aac", argv[0]);
        return 0;
    }

    std::string inputfile = std::string(argv[1]);
    std::string output_h264_file = std::string(argv[2]);
    std::string output_aac_file = std::string(argv[3]);

    std::ofstream ofs_h264(output_h264_file, std::ios::out | std::ios::binary);
    std::ofstream ofs_aac(output_aac_file, std::ios::out | std::ios::binary);
    if( !ofs_h264.is_open() || !ofs_aac.is_open() ){
        printf("open h264 aac output file failed!");
        return -1;
    }

    AVFormatContext* av_fmt_ctx = nullptr;
    AVPacket* av_pkt = nullptr;
    int video_index = -1;
    int audio_index = -1;
    int ret = -1;
    char av_error_msg_buf[1024];

    av_fmt_ctx = avformat_alloc_context();
    CHECK_AV_ALLOC(av_fmt_ctx, "avformat_alloc_context");

    ret = avformat_open_input(&av_fmt_ctx, inputfile.c_str(), nullptr, nullptr);
    CHECK_AV_RET(ret, "avformat_open_input");

    video_index = av_find_best_stream(av_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    CHECK_AV_RET(video_index, "av_find_best_stream");

    audio_index = av_find_best_stream(av_fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    CHECK_AV_RET(audio_index, "av_find_best_stream");
    
    //根据名称获取比特流过滤器，mp4容器中NALU的存储格式为[NALU长度+NALU]，需要通过过滤器转换为annexb格式
    const AVBitStreamFilter *bs_filter = av_bsf_get_by_name("h264_mp4toannexb");
    CHECK_AV_ALLOC(bs_filter, "av_bsf_get_by_name");

    //分配过滤器上下文，AVBSFcontext变量是具体工作的地方
    AVBSFContext *bsf_ctx = nullptr;
    ret = av_bsf_alloc(bs_filter, &bsf_ctx);
    CHECK_AV_RET(ret, "av_bsf_alloc");

    //从解复用上下文结构体中拷贝参数到比特流过滤器上下文中
    ret = avcodec_parameters_copy(bsf_ctx->par_in, av_fmt_ctx->streams[video_index]->codecpar);
    CHECK_AV_RET(ret, "avcodec_parameters_copy");

    //初始化过滤器上下文
    ret = av_bsf_init(bsf_ctx);
    CHECK_AV_RET(ret, "av_bsf_init");

    av_pkt = av_packet_alloc();
    CHECK_AV_ALLOC(av_pkt, "av_packet_alloc");
    av_init_packet(av_pkt);

    while(1){
        ret = av_read_frame(av_fmt_ctx, av_pkt);        //av_read_frame将编码包裹读取到av_pkt的buf中，用完后需要我们自己去释放av_pkt里的内存
        if (ret == AVERROR_EOF) {
            printf("READ END OF FILE\n");
            break;
        }
        CHECK_AV_RET(ret, "av_read_frame");

        if( av_pkt->stream_index == video_index ){
            ret = av_bsf_send_packet(bsf_ctx, av_pkt);
            if ( ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF ){
                av_strerror(ret, av_err_buf, av_err_buf_size);
                printf("av_bsf_send_packet failed, av error msg: %s\n", av_err_buf);
                break;
            }
            ret = av_bsf_receive_packet(bsf_ctx, av_pkt);
            if( ret == AVERROR(EAGAIN) ){
                av_packet_unref(av_pkt);
                continue;
            } else if( ret == 0){
                ofs_h264.write((const char*)av_pkt->data, av_pkt->size);
                if( !ofs_h264 ){
                    printf("write to h264 file failed!\n");
                    exit(0);
                }
            }
        } else if( av_pkt->stream_index == audio_index ){
            
        }

        //每轮正常结束后，统一在while循环末尾释放AVPacket
        av_packet_unref(av_pkt);

    }

    //冲刷一次过滤器
    av_bsf_send_packet(bsf_ctx, nullptr);
    while( av_bsf_receive_packet(bsf_ctx, av_pkt) == 0 ){
        ofs_h264.write((const char*)av_pkt->data, av_pkt->size);
        av_packet_unref(av_pkt);
    }

    ofs_h264.close();

    printf("parse finish!\n");
    return 0;
}
