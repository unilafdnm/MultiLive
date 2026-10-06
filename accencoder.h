#ifndef ACCENCODER_H
#define ACCENCODER_H

#include <QObject>
#include<string>
#include"StreamConfig.h"
extern "C"{
#include<libavcodec/avcodec.h>
#include<libavformat/avformat.h>
#include<libswresample/swresample.h>
#include<libavutil/audio_fifo.h>
#include<libavutil/channel_layout.h>
#include<libavutil/samplefmt.h>
}

class MultiPublisher;

class accencoder : public QObject
{
    Q_OBJECT
public:
    explicit accencoder(QObject *parent = nullptr);
    ~accencoder();
    bool init(int inputSampleRate,const AVChannelLayout& inputChannelLayout,AVSampleFormat inputFormat,const StreamConfig& config);
    void encode(AVFrame* frame,int64_t timestampUs);
    void close();
    void setPublisher(MultiPublisher* publisher);

private:
    void encodeFrame();
    void writePackers();
private:
    AVCodecContext* _codec_ctx;
    SwrContext* _swr_ctx;
    AVAudioFifo* _fifo;
    AVFrame* _frame;
    AVPacket* _packet;
    AVFormatContext* _output_ctx;
    AVStream* _output_stream;
    MultiPublisher* _publisher;
    int64_t _pts;
    int64_t _nextpts;
    bool _ptsInitialized;
    bool _initialized;
signals:

};

#endif // ACCENCODER_H
