#include <iostream>
#include <fstream>
extern "C"{
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavcodec/defs.h>
#include <libswresample/swresample.h>
}

static char errbuf[1024];
static void f32le_cvt_to_fltp(uint8_t* pcm_f32le, AVFrame* av_frame);
static int encode(AVCodecContext* codec_ctx, AVFrame* av_frame, AVPacket* av_pkt, std::ofstream& ofs);
static void fill_adts_header(AVCodecContext* codec_ctx, uint8_t* adts_header, int aac_len);
int main(int argc, char** argv)
{
    if(argc < 3){
        printf("Usage: %s <input file> <out file> [codec name]\n", argv[0]);
        return 0;
    }
    std::string input_pcm_file = std::string(argv[1]);
    std::string output_aac_file = std::string(argv[2]);
    std::ifstream ifs(input_pcm_file, std::ios::in | std::ios::binary); 
    std::ofstream ofs(output_aac_file, std::ios::out | std::ios::binary);
    if( !ifs.is_open() || !ofs.is_open() ){
        printf("open file failed!\n");
        return 0;
    }

    const AVCodec* codec{nullptr};
    AVCodecContext* codec_ctx{nullptr};
    AVPacket* av_pkt{nullptr};
    AVFrame* av_frame{nullptr};
    int ret{0};

    enum AVCodecID codec_id = AV_CODEC_ID_AAC;
    
    //根据编码器名字查找编码器
    if( !(codec = avcodec_find_encoder_by_name("aac")) ){
        printf("avcodec_find_encoder_by_name failed!\n");
        return 0;
    }

    //分配编码器上下文
    if( !(codec_ctx = avcodec_alloc_context3(codec)) ){
        printf("avcodec_alloc_context3 failed!\n");
        return 0;
    }

    codec_ctx->codec_id = AV_CODEC_ID_AAC;          //编解码id，aac
    codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;     //编解码类型，音频
    codec_ctx->bit_rate = 128 * 1024;               //编码后的bit率，bit/s，即128kbit/s，一秒钟128kb的数据据
    codec_ctx->sample_rate = 48000;                 //采样频率，每秒采样多少次
    codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;     //采样格式，32位float，平面布局
    av_channel_layout_default(&codec_ctx->ch_layout, 2);
    codec_ctx->profile = AV_PROFILE_AAC_LOW;
    codec_ctx->flags = AV_CODEC_FLAG_GLOBAL_HEADER;
    // // 查询编码器某个参数所支持的配置，下面的示例是查询codec中支持的采样格式有哪些
    // AVCodecConfig codec_config{AV_CODEC_CONFIG_SAMPLE_FORMAT};
    // int out_num_configs;
    // const AVSampleFormat* sample_fmts{nullptr};
    // avcodec_get_supported_config(codec_ctx, codec, codec_config, 0, (const void**)&sample_fmts, &out_num_configs);
    // printf("supported samples formats num: %d\n", out_num_configs);
    // for (int i = 0; i < out_num_configs; i++) {
    //     AVSampleFormat fmt = sample_fmts[i];
    //     // 在新版 FFmpeg 中，用 AV_SAMPLE_FMT_NONE 作为终止标志
    //     if (fmt == AV_SAMPLE_FMT_NONE)
    //         break;
    //     std::cout << "  " << av_get_sample_fmt_name(fmt) << "\n";
    // }

    printf("before avcodec_open2, codec_ctx->frame_size: %d\n", codec_ctx->frame_size);
    if( (ret = avcodec_open2(codec_ctx, codec, nullptr) ) < 0){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avcodec_open2 error: %s\n", errbuf);
        goto END;
    }
    printf("after avcodec_open2, codec_ctx->frame_size: %d\n", codec_ctx->frame_size);

    if( !(av_pkt = av_packet_alloc()) ){
        printf("av_packet_alloc failed!\n");
        goto END;
    }
    if( !(av_frame = av_frame_alloc() )){
        printf("av_frame_alloc failed!\n");
        goto END;
    }


    /**
     * 每次送多少数据给编码器由三个要素决定
     *  （1）音频通道数。比如双声道有2个通道
     *  （2）每个通道的采样数。比如每帧1024个采样点
     *  （3）采样格式。比如fltp，32位浮点型，planar平面格式
    */
    av_frame->nb_samples = codec_ctx->frame_size;       //每个通道的采样点个数
    av_frame->format = codec_ctx->sample_fmt;           //每个采样点的格式
    av_frame->ch_layout = codec_ctx->ch_layout;         //通道格式，重要的是通道数
    av_frame->sample_rate = codec_ctx->sample_rate;     //采样率，每秒采集多少次（即得到多少采样点）
    printf("av_frame nb_samples: %d\n", av_frame->nb_samples);
    printf("av_frame format: %d, %s\n", av_frame->format, av_get_sample_fmt_name(AVSampleFormat(av_frame->format)));
    printf("av_frame channels: %d\n", av_frame->ch_layout.nb_channels);

    //开辟AVFrame的空间
    if( (ret = av_frame_get_buffer(av_frame, 0)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("av_frame_get_buffer error: %s\n", errbuf);
        goto END;
    }

    //计算每帧数据量，这个数据量是我们每次需要从PCM文件里读取的数据，拷贝给AVFrame，再交给编码器编码
    int nb_channels = av_frame->ch_layout.nb_channels;
    int nb_samples = av_frame->nb_samples;
    int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)av_frame->format);
    int frame_bytes = nb_channels * nb_samples * bytes_per_sample;
    printf("frame bytes: %d\n", frame_bytes);

    uint8_t* pcm_buf = new uint8_t[frame_bytes];
    uint8_t* pcm_cvt_buf = new uint8_t[frame_bytes];

    printf("start encode....\n");

    while(1){
        memset(pcm_buf, 0, sizeof(frame_bytes));
        ifs.read((char*)pcm_buf, std::streamsize(frame_bytes));
        if( ifs.gcount() != frame_bytes && !ifs.eof() ){
            printf("read file failed!\n");
            break;
        } else if( ifs.eof() || ifs.gcount() == 0 ){
            break;
        }
        if( (ret = av_frame_make_writable(av_frame)) < 0 ){
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("av_frame_make_writable error: %s\n", errbuf);
            goto END;
        }

        memset(pcm_cvt_buf, 0, frame_bytes);
        f32le_cvt_to_fltp(pcm_buf, av_frame);

        ret = encode(codec_ctx, av_frame, av_pkt, ofs);
        if( ret != 0 ){
            printf("encode failed!\n");
            goto END;
        }
    }

    encode(codec_ctx, nullptr, av_pkt, ofs);

END:
    ifs.close();
    ofs.close();
    if( pcm_buf ){
        delete[] pcm_buf;
    } 
    av_frame_free(&av_frame);
    av_packet_free(&av_pkt);
    avcodec_free_context(&codec_ctx);
    
    printf("encode pcm to aac end!\n");
    return 0;
}

static int encode(AVCodecContext* codec_ctx, AVFrame* av_frame, AVPacket* av_pkt, std::ofstream& ofs){
    int ret{0};
    if( (ret = avcodec_send_frame(codec_ctx, av_frame)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avcodec_send_frame error: %s\n", errbuf);
        return -1;
    }

    while( ret >= 0 ){
        ret = avcodec_receive_packet(codec_ctx, av_pkt);
        if( ret == AVERROR_EOF || ret == AVERROR(EAGAIN) ){
            return 0;
        } else if( ret < 0 ){
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("avcodec_receive_packet error: %s\n", errbuf);
            return ret;
        }
        
        if(codec_ctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER){
            uint8_t adts_header[7];
            fill_adts_header(codec_ctx, adts_header, av_pkt->size);
            ofs.write((char*)adts_header, 7);
            ofs.write((char*)av_pkt->data, av_pkt->size);
            if( !ofs ){
                printf("ofstream write to file failed!\n");
                return -1;
            }
        }
    }

    return ret;
}


static void f32le_cvt_to_fltp(uint8_t* pcm_f32le, AVFrame* av_frame){
    SwrContext* swr{nullptr};

    //输出参数
    AVChannelLayout out_ch_layout = av_frame->ch_layout;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_FLTP;
    int out_sample_rate = av_frame->sample_rate;

    //输入参数
    AVChannelLayout in_ch_layout = av_frame->ch_layout;
    AVSampleFormat in_sample_fmt = AV_SAMPLE_FMT_FLT;
    int in_sample_rate = av_frame->sample_rate;

    int ret = swr_alloc_set_opts2(&swr, &out_ch_layout, out_sample_fmt, out_sample_rate,
                                &in_ch_layout, in_sample_fmt, in_sample_rate,
                                0, nullptr);
    if( ret < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("swr_alloc_set_opts2 error: %s\n", errbuf);
        exit(0);
    }
    ret = swr_init(swr);
    if( ret < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("swr_init error: %s\n", errbuf);
        exit(0);
    }

    const uint8_t* in_data[1] = {pcm_f32le};
    int nb_samples = swr_convert(swr, av_frame->data, av_frame->nb_samples, in_data, av_frame->nb_samples);
    if( nb_samples < 0 ){
        av_strerror(nb_samples, errbuf, sizeof(errbuf));
        printf("swr_convert error: %s\n", errbuf);
        exit(0);
    }

    swr_free(&swr);
}


static void fill_adts_header(AVCodecContext* codec_ctx, uint8_t* adts_header, int aac_len){
    uint8_t freq_idx = 0;    //0: 96000 Hz  3: 48000 Hz 4: 44100 Hz
    switch (codec_ctx->sample_rate) {
        case 96000: freq_idx = 0; break;
        case 88200: freq_idx = 1; break;
        case 64000: freq_idx = 2; break;
        case 48000: freq_idx = 3; break;
        case 44100: freq_idx = 4; break;
        case 32000: freq_idx = 5; break;
        case 24000: freq_idx = 6; break;
        case 22050: freq_idx = 7; break;
        case 16000: freq_idx = 8; break;
        case 12000: freq_idx = 9; break;
        case 11025: freq_idx = 10; break;
        case 8000: freq_idx = 11; break;
        case 7350: freq_idx = 12; break;
        default: freq_idx = 4; break;
    }
    uint8_t nb_channels = codec_ctx->ch_layout.nb_channels;
    uint32_t frame_len = aac_len + 7;
    adts_header[0] = 0xff;
    adts_header[1] = 0xf1;
    adts_header[2] = ((codec_ctx->profile + 1) << 6) + (freq_idx << 2) + (nb_channels >> 2);
    adts_header[3] = (((nb_channels & 3) << 6) + (frame_len  >> 11));
    adts_header[4] = ((frame_len & 0x7FF) >> 3);
    adts_header[5] = (((frame_len & 7) << 5) + 0x1F);
    adts_header[6] = 0xFC;

    
/** 11111111 11110001 01000300 10000010 00000000 00111111 11111100
 *  
 *  ===========28位固定头部=====================
 *  11111111 1111 -> syncword：占12个bit，同步位，总是0xfff
 *  0 -> id：占1个bit，0表示MPEG-4，1表示MPEG-2
 *  00 -> layer：占2个bit，总是0
 *  1 -> 校验字段：占1个bit，0表示有校验字段，1表示没有
 *  01 -> profile：占2个bit，01表示AAC LC
 *  0003 ->  采样率索引：占4个bit
 *  0 -> private_bit：占1个bit
 *  010 -> 声道数：占3个bit
 *  0 -> original_copy： 占1个bit
 *  0 -> home：占1个bit
 * 
 * 
 *  ===========28位可变头部=======================
 *  0 ->    占1个bit
 *  0 ->    占1个bit
 *  10 00000000 001 -> aac_frame_length：占13个bit，aac帧的长度，头部+aac数据
 *  11111 111111 -> aac_buffer_fullness：占11个bit，示例里全部填1
 *  00 -> number_of_raw_data_blocks_in_frame：占2个bit。表示有多少个AAC原始帧，需要+1。比如为0时表示后面的数据部分有1个AAC原始帧。因为只占2个bit，所以最大值顶多为3，也就是最多包含4帧AAC编码数据，但是一般情况下都只有1个AAC编码数据包
*/  
}