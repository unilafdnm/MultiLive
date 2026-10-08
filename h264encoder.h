#ifndef H264ENCODER_H
#define H264ENCODER_H

#include <QObject>
#include<string>
#include<cstdio>
#include"StreamConfig.h"
#include"realtimesession.h"
extern "C"{
#include<libavcodec/avcodec.h>
#include<libswscale/swscale.h>

}


class H264Encoder : public QObject
{
    Q_OBJECT
public:
    explicit H264Encoder(QObject *parent = nullptr);
    ~H264Encoder();
    bool init(int inputwidth,int inputheight,AVPixelFormat inputformat,const StreamConfig& config);
    void encode(AVFrame* frame,int64_t timestampUs);
    void close();
    void setPacketSink(EncodePacketSink* sink);


private:
    AVCodecContext* _codec_ctx;
    SwsContext* _sws_ctx;
    AVFrame* _frame;
    AVPacket* _packet;
    FILE* _file;
    int64_t _lastPts;
    EncodePacketSink* _sink;


    bool _initialized;

signals:

};

#endif // H264ENCODER_H
