#ifndef _MUXER_H_
#define _MUXER_H_

extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class Muxer
{
public:
    Muxer();
    ~Muxer();

    int Init(const char* filename);
    void DeInit();

    int AddStream(AVCodecContext* ctx);

private:
}


#endif // _MUXER_H_