#ifndef MultiPublisher_H
#define MultiPublisher_H

#include<QDebug>
#include<string>
#include<vector>
#include<memory>
#include"networkpublisher.h"
#include"outputtypes.h"
extern "C"{
#include<libavcodec/avcodec.h>
}

class MultiPublisher:public QObject
{
    Q_OBJECT;
public:
    MultiPublisher(QObject* parent=nullptr);
    ~MultiPublisher();
    bool addOutput(const OutputTarget& target);

    void setVideoEncoder(AVCodecContext* codec_ctx);
    void setAudioEncoder(AVCodecContext* codec_ctx);

    void writeVideoPacket(const AVPacket* packet,AVRational encoderTimeBase);
    void writeAudioPacket(const AVPacket* packet,AVRational encoderTimeBase);

    void close();
    std::vector<PublisherStats> stats();

private:
    std::vector<std::unique_ptr<NetworkPublisher>> _publishers;
signals:
    void connected();
    void connectionFailed();
};

#endif // MultiPublisher_H
