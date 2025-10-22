/**
 * demux a flv file example
 * encode yuv to h264 video stream
 * encode pcm to aac audio stream
 * finally demultiplexing this two stream to a flv container
 * 从.yuv、.pcm文件读取音视频数据保存到AVFrame -> 编码，编码数据保存到AVPacket -> 将AVPacket写入flv文件 
 * 
*/
#include<iostream>
#include<fstream>
extern "C"{
#include<libavformat/avformat.h>
#include<libavcodec/avcodec.h>
#include<libavutil/avutil.h>
#include<libavutil/rational.h>
#include<libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include<libavcodec/bsf.h>
}

static char errbuf[1024];
static int ret = 0;

//输出流的上下文内容，包括编码器、编码器上下文、编码码流、编码帧......
typedef struct OutputContext
{
    const AVCodec* codec{nullptr};
    AVCodecContext* codec_ctx{nullptr};
    AVStream* av_stream{nullptr};
    AVFrame* av_frame{nullptr};
    AVFrame* av_frame_tmp{nullptr};
    AVPacket* av_pkt{nullptr};

    int per_frame_bytes;    //音视频每帧的数据量（字节为单位）
    uint8_t* data_buf{nullptr};  //保存原始音视频数据帧的buf，从文件里读取的yuv、pcm数据保存在此，再拷贝到AVFrame中
    int64_t next_pts{0};
    int frames_cnt{0};      //视频帧的数量
    int samples_cnt{0};    //音频的采样数量累计
}OutputContext;

static int add_stream(AVFormatContext* fmt_ctx, OutputContext* out_stream, AVCodecID codec_id);
static int open_codec(OutputContext* out_ctx);
static int encode(OutputContext* out_ctx, AVFormatContext* fmt_ctx);
static int encode_one_yuv_frame(OutputContext* video_ctx, AVFormatContext* fmt_ctx, std::ifstream& ifs);
static int encode_one_pcm_frame(OutputContext* video_ctx, AVFormatContext* fmt_ctx, std::ifstream& ifs);

int main(int argc, char** argv)
{
    if( argc != 4){
        printf("Usage: %s <output_file>\n", argv[0]);
        return 0;
    }
    AVFormatContext* fmt_ctx{nullptr};
    //这个函数与avformat_alloc_context的区别在于它会根据输出文件名（参数4）专门分配一个专门用于输入输出的【媒体封装上下文】
    //avformat_alloc_context仅仅只是分配一个空的AVFromatContext，内部的所有字段需要自己指定
    if( (ret = avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, argv[3])) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avformat_alloc_output_context2 error: %s\n", errbuf);
        return 0;
    }
    if( fmt_ctx == nullptr ){
        printf("AVFormatContext alloc failed!");
        return 0;
    }
    //如果AVOutputFormat中的flags包含了AVFMT_NOFILE，表示无需打开文件，比如写入到rtsp网络流中。当前示例场景为写入文件，需要打开文件
    if( !(fmt_ctx->oformat->flags & AVFMT_NOFILE) ){
        if( (ret = avio_open(&fmt_ctx->pb, argv[3], AVIO_FLAG_WRITE)) < 0 ){
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("avio_open error: %s\n", errbuf);
            return 0;
        }
    }

    OutputContext video_ctx;
    OutputContext audio_ctx;

    //添加视频流
    if( add_stream(fmt_ctx, &video_ctx, AV_CODEC_ID_H264) < 0) {
        printf("add video stream failed!\n");
        return 0;
    }
    //添加音频流
    if( add_stream(fmt_ctx, &audio_ctx, AV_CODEC_ID_AAC) < 0){
        printf("add audio stream failed!\n");
        return 0;
    }

    av_dump_format(fmt_ctx, 0, argv[3], 1);

    AVDictionary *opt = NULL;
    ret = avformat_write_header(fmt_ctx, &opt);// base_time audio = 1/1000 video = 1/1000
    if (ret < 0){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avformat_write_header error: %s\n", errbuf);
        return 0;
    }

    //从.yuv、.pcm文件中读取视频帧、音频帧，送入编码器编码，再写入flv文件
    std::ifstream ifs_yuv(argv[1], std::ios::in | std::ios::binary);
    std::ifstream ifs_pcm(argv[2], std::ios::in | std::ios::binary);
    if( !ifs_yuv.is_open() || !ifs_pcm.is_open()){
        printf("open .yuv or .pcm file failed!\n");
        return 0;
    }

    int enc_v = 1;
    int enc_a = 1;
    while( enc_v > 0 || enc_a > 0){
        if( enc_v > 0 && enc_a > 0 ){
            //比较音频帧和视频帧的时间基，video的next_pts是帧，audio的pts是每帧的采样点，会通过各自的time_base来转换到同一尺度上
            auto cmp = av_compare_ts(video_ctx.next_pts, video_ctx.codec_ctx->time_base,
                                     audio_ctx.next_pts, audio_ctx.codec_ctx->time_base);
            double video_ts = video_ctx.next_pts * 1000.0 * video_ctx.codec_ctx->time_base.num /  video_ctx.codec_ctx->time_base.den;
            double audio_ts = audio_ctx.next_pts * 1000.0 * audio_ctx.codec_ctx->time_base.num /  audio_ctx.codec_ctx->time_base.den;
            // printf("v_ts: %lf\t\ta_ts: %lf\n", video_ts, audio_ts);
            if( cmp < 0 ){
                enc_v = encode_one_yuv_frame(&video_ctx, fmt_ctx, ifs_yuv);
            } else {
                enc_a = encode_one_pcm_frame(&audio_ctx, fmt_ctx, ifs_pcm);
            }
        } else if( enc_v > 0){
            enc_v = encode_one_yuv_frame(&video_ctx, fmt_ctx, ifs_yuv);
        } else if( enc_a > 0){
            enc_a = encode_one_pcm_frame(&audio_ctx, fmt_ctx, ifs_pcm);
        }
    }

    av_write_trailer(fmt_ctx);

    avformat_free_context(fmt_ctx);
    return 0;
}

int add_stream(AVFormatContext* fmt_ctx, OutputContext* out_ctx, AVCodecID codec_id)
{
    int ret = 0;
    auto& codec = out_ctx->codec;
    auto& codec_ctx = out_ctx->codec_ctx;
    auto& av_stream = out_ctx->av_stream;

    //根据编码器id查找编码器
    if( !(codec = avcodec_find_encoder(codec_id)) ){
        printf("avcodec_find_decoder_by_name failed!\n");
        return -1;
    }

    //根据编码器添加一个对应类型的stream到媒体文件中
    av_stream = avformat_new_stream(fmt_ctx, codec);
    if( !av_stream ){
        printf("avformat_new_stream failed!\n");
        return -1;
    }
    av_stream->id = fmt_ctx->nb_streams - 1;

    //设置编码器参数，并且打开编码器，设置音视频编码参数是很关键的一步
    open_codec(out_ctx);
    
    if (codec_ctx->codec_id == AV_CODEC_ID_AAC && av_stream->codecpar->extradata == nullptr) {
        printf("AAC extradata missing!\n");
    }

    if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER){
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; 
    }


    return 0;
}

int open_codec(OutputContext* out_ctx)
{
    auto& codec = out_ctx->codec;
    auto& codec_ctx = out_ctx->codec_ctx;
    auto& av_stream = out_ctx->av_stream;
     
    //根据编码器AVCodec分配分配并初始化一个解码器上下文AVCodecContext结构体
    //一路流对应一个解码器上下文
    if( !(codec_ctx = avcodec_alloc_context3(codec)) ){
        printf("avcodec_alloc_context3 failed\n");
        return -1;
    }
    
    //设置音频编码器参数
    if( codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO ){
        av_channel_layout_default(&codec_ctx->ch_layout, 2);
        codec_ctx->sample_rate = 48000;
        codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
        codec_ctx->bit_rate = 191000;
        codec_ctx->time_base = {1, codec_ctx->sample_rate};
        av_stream->time_base = codec_ctx->time_base;        //这里其实可以不进行设置，后面avformat_write_header的时候，封装器可能会改变流的time_base
    }

    //设置视频编码器参数
    if( codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ){
        codec_ctx->width = 3840;
        codec_ctx->height = 2160;
        codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        codec_ctx->framerate = av_make_q(60,1);
        codec_ctx->bit_rate = 68 * 1024 * 1024; //CBR模式下设置平均码率为20Mbps
        codec_ctx->max_b_frames = 100;
        codec_ctx->gop_size = 60;
        av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(codec_ctx->priv_data, "profile", "high", 0);
        av_opt_set(codec_ctx->priv_data, "tune", "film", 0);
        av_opt_set_double(codec_ctx->priv_data, "crf", 23, 0);
        codec_ctx->time_base = {1, 60};
        av_stream->time_base = codec_ctx->time_base;     //这里其实可以不进行设置，后面avformat_write_header的时候，封装器可能会改变流的time_base
    }

    //用给定的编码器codec真正打开并初始化编码器AVCodecContext上下文
    if( (ret = avcodec_open2(codec_ctx, codec, nullptr)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avcodec_open2 error: %s\n", errbuf);
        return ret;
    }

    //将编码器上下文中的参数拷贝到AVStream中
    if( (ret = avcodec_parameters_from_context(av_stream->codecpar, codec_ctx)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avcodec_parameters_from_context error: %s\n", errbuf);
        return ret;
    }

    //AVFrame的内部buffer需要分别根据音频参数和视频参数分别开辟
    if( codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ){
        //分配一个视频帧AVFrame结构体，内部的真正数据空间还未分配
        out_ctx->av_frame = av_frame_alloc();
        if( out_ctx->av_frame == nullptr ){
            printf("av_frame_alloc failed!\n");
            return -1;
        }
        out_ctx->av_frame->format = AV_PIX_FMT_YUV420P;
        out_ctx->av_frame->width = 3840;
        out_ctx->av_frame->height = 2160;
        //这里根据色彩空间格式、宽、高 获取真正的AVFrame数据空间
        if( (ret = av_frame_get_buffer(out_ctx->av_frame, 0)) < 0 ){
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("av_frame_get_buffer error: %s\n", errbuf);
            return -1;
        }
        //从文件读取的每帧yuv图像的大小
        auto frame_bytes = av_image_get_buffer_size((AVPixelFormat)out_ctx->av_frame->format, out_ctx->av_frame->width, out_ctx->av_frame->height, 1); 
        out_ctx->data_buf = new uint8_t[frame_bytes];
        out_ctx->per_frame_bytes = frame_bytes;

    } else if( codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO ){
        //分配一个音频帧AVFrame结构体，内部的真正数据空间还未分配
        out_ctx->av_frame = av_frame_alloc();
        if( out_ctx->av_frame == nullptr ){
            printf("av_frame_alloc failed!\n");
            return -1;
        }
        out_ctx->av_frame->format = (AVSampleFormat)codec_ctx->sample_fmt;
        out_ctx->av_frame->ch_layout = codec_ctx->ch_layout;
        //重点：音频每个原始PCM帧的采样点数量，需要等打开AVCodecContext编码器上下后才能知道
        //nb_samples是针对每个通道而言的样本数量
        out_ctx->av_frame->nb_samples = codec_ctx->frame_size;
        out_ctx->av_frame->sample_rate = codec_ctx->sample_rate;
        //这里根据采样数据格式、通道数、每帧采样点 获取音频帧真正的AVFrame数据空间
        if( (ret = av_frame_get_buffer(out_ctx->av_frame, 0)) < 0 ){
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("av_frame_get_buffer error: %s\n", errbuf);
            return -1;
        }
        //从文件读取的每帧音频的数据量，这个数据量是我们每次需要从PCM文件里读取的数据，拷贝给AVFrame，再交给编码器编码
        int nb_channels = out_ctx->av_frame->ch_layout.nb_channels;
        int nb_samples = out_ctx->av_frame->nb_samples;
        int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)out_ctx->av_frame->format);
        int frame_bytes = nb_channels * nb_samples * bytes_per_sample;
        out_ctx->data_buf = new uint8_t[frame_bytes];
        out_ctx->per_frame_bytes = frame_bytes;
    }

    //分配一个AVPacket结构体，AVPacket等待接收编码数据，这里由编码器开辟空间，无需考虑参数设置问题
    out_ctx->av_pkt = av_packet_alloc();
    if( out_ctx->av_pkt == nullptr){
        printf("av_packet_alloc failed!\n");
        return -1;
    }
    return 0;
}

static int encode_one_yuv_frame(OutputContext* video_ctx, AVFormatContext* fmt_ctx, std::ifstream& ifs)
{
    static int frame_index{0};
    auto yuv_buf = video_ctx->data_buf;
    auto per_frame_bytes = video_ctx->per_frame_bytes; 
    
    memset(yuv_buf, 0, per_frame_bytes);
    //从yuv文件读取一帧yuv数据
    ifs.read((char*)yuv_buf, per_frame_bytes);
    if( ifs.eof() ){
        printf("read yuv file finished\n");
        return 0;       //return 0表示yuv文件读取完毕
    }
    av_frame_make_writable(video_ctx->av_frame);
    ret = av_image_fill_arrays(video_ctx->av_frame->data, video_ctx->av_frame->linesize, yuv_buf, 
            (AVPixelFormat)video_ctx->av_frame->format, video_ctx->av_frame->width, video_ctx->av_frame->height, 1);
    if( ret < 0 || ret != per_frame_bytes){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("av_image_fill_arrays error: %s\n", errbuf);
        return 0;
    }
    video_ctx->av_frame->pts = frame_index;
    video_ctx->next_pts = frame_index + 1;
    frame_index++;

    return encode(video_ctx, fmt_ctx);
}

static int encode_one_pcm_frame(OutputContext* audio_ctx, AVFormatContext* fmt_ctx, std::ifstream& ifs)
{
    auto pcm_buf = audio_ctx->data_buf;
    auto per_frame_bytes = audio_ctx->per_frame_bytes;
    memset(pcm_buf, 0, per_frame_bytes);

    auto& av_frame = audio_ctx->av_frame;

    ifs.read((char*)pcm_buf, per_frame_bytes);
    if( ifs.eof() ){
        printf("read PCM file finished\n");
        return 0;
    }
    av_frame_make_writable(av_frame);

    //pcm是我用ffmpeg从mp4文件中提取的，ffmpeg提取的pcm文件只能是packed
    //现在要将读取到的一帧packed排列的pcm数据转换为planer排列格式
    //f32le -> fltp
    SwrContext* swr{nullptr};
    //输出参数
    AVChannelLayout out_ch_layout = audio_ctx->codec_ctx->ch_layout;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_FLTP;
    int out_sample_rate = audio_ctx->codec_ctx->sample_rate;
    //输入参数
    AVChannelLayout in_ch_layout = audio_ctx->codec_ctx->ch_layout;
    AVSampleFormat in_sample_fmt = AV_SAMPLE_FMT_FLT;
    int in_sample_rate = audio_ctx->codec_ctx->sample_rate;

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

    const uint8_t* in_data[1] = {pcm_buf};
    int in_samples = per_frame_bytes / ( in_ch_layout.nb_channels * sizeof(float));
    int nb_samples = swr_convert(swr, av_frame->data, av_frame->nb_samples, in_data, in_samples);
    if( nb_samples < 0 ){
        av_strerror(nb_samples, errbuf, sizeof(errbuf));
        printf("swr_convert error: %s\n", errbuf);
        exit(0);
    }
    av_frame->pts = audio_ctx->samples_cnt;
    // audio_ctx->samples_cnt += nb_samples;
    audio_ctx->samples_cnt += audio_ctx->codec_ctx->frame_size;
    audio_ctx->next_pts = audio_ctx->samples_cnt;
    swr_free(&swr);

    return encode(audio_ctx, fmt_ctx);
}

static int encode(OutputContext* out_ctx, AVFormatContext* fmt_ctx)
{   
    int ret = 0;
    auto codec_ctx = out_ctx->codec_ctx;
    auto av_frame = out_ctx->av_frame;
    auto pkt = out_ctx->av_pkt;
    // if( av_frame ){
    //     printf("send frame: %d\n", av_frame->pts);
    // }
    if( (ret = avcodec_send_frame(codec_ctx, av_frame)) < 0 ){
        av_strerror(ret, errbuf, sizeof(errbuf));
        printf("avcodec_send_frame error: %s\n", errbuf);
        return ret;
    }

    while(true){
        av_packet_unref(pkt);
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if( ret == AVERROR(EAGAIN) || ret == AVERROR_EOF ){
            break;
        } else if( ret < 0 ){
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("avcodec_receive_packet error: %s\n", errbuf);
            return ret;
        }

        if( pkt->flags & AV_PKT_FLAG_KEY ){
            printf("encoded pkt falgs: %d, pts: %lld, dts: %lld, size: %d\n",
                    pkt->flags, pkt->pts, pkt->dts, pkt->size);
        }

        //这里很重要!!!编码后的AVPacket的流的index需要我们自己来指定
        pkt->stream_index = out_ctx->av_stream->id;
        av_packet_rescale_ts(pkt, codec_ctx->time_base, out_ctx->av_stream->time_base);
        if( (ret = av_interleaved_write_frame(fmt_ctx, pkt)) < 0 ){
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("av_interleaved_write_frame error: %s\n", errbuf);
            return ret;
        }
    }

    return 1;
}

// printf("frame pts=%ld, stream pts=%ld (%.3f ms)\n",
//        frame->pts,
//        av_rescale_q(frame->pts, codec_ctx->time_base, stream->time_base),
//        av_rescale_q(frame->pts, codec_ctx->time_base, (AVRational){1,1000}) * 1.0);


// struct A{
//     int* ptr{nullptr};
// };

// void func(A* a){
//     auto p = a->ptr;
//     p = new int(100);
// }

// int main()
// {
//     A a;
//     func(&a);

//     if(a.ptr){
//         printf("%d\n", *a.ptr);
//     }
// }


// struct Lane{
//     int lane_id;
//     int max_veh_num;
//     int length;
// }Lane;