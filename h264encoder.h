#ifndef H264ENCODER_H
#define H264ENCODER_H

#include <QObject>
#include<string>
#include<cstdio>
#include"StreamConfig.h"
extern "C"{
#include<libavcodec/avcodec.h>
#include<libswscale/swscale.h>

}

class MultiPublisher;

class H264Encoder : public QObject
{
    Q_OBJECT
public:
    explicit H264Encoder(QObject *parent = nullptr);
    ~H264Encoder();
    bool init(int inputwidth,int inputheight,AVPixelFormat inputformat,const StreamConfig& config);
    void encode(AVFrame* frame,int64_t timestampUs);
    void close();
    void setPublisher(MultiPublisher* publisher);


private:
    AVCodecContext* _codec_ctx;
    SwsContext* _sws_ctx;
    AVFrame* _frame;
    AVPacket* _packet;
    MultiPublisher* _publisher;
    FILE* _file;
    int64_t _lastPts;
    bool _initialized;

signals:

};

#endif // H264ENCODER_H
