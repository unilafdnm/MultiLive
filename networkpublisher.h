#ifndef NetworkPublisher_H
#define NetworkPublisher_H

#include <QObject>
#include <string>
#include <mutex>
#include<thread>
#include<atomic>
#include<deque>
#include<memory>
#include<condition_variable>
#include<cstdint>
#include<cstddef>
#include"outputtypes.h"
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}



class NetworkPublisher : public QObject
{
    Q_OBJECT
public:
    explicit NetworkPublisher(QObject *parent = nullptr);
    ~NetworkPublisher();
    //准备RTMP地址
    bool prepare(const OutputTarget& target);
    //H264/ACC 编码器初始化完成后调用
    void setVideoEncoder(AVCodecContext* condec_ctx);
    void setAudioEncoder(AVCodecContext* codec_ctx);

    //编码得到Packet后调用
    void writeVideoPacket(const AVPacket* packet,AVRational encoderTimeBase);
    void writeAudioPacket(const AVPacket* packet,AVRational encoderTimeBase);

    void close();

    PublisherStats stats();

private:
    enum class PacketType{
        Video,
        Audio
    };

    struct PacketItem{
        AVPacket* packet=nullptr;
        AVRational timebase={0,1};
        PacketType type;
    };


private:
    void tryStartWorker();
    void workerLoop();
    bool connectServer();
    void disconnection(bool writeTrailer);
    void enqueuePacket(const AVPacket* packet,AVRational timeBase,PacketType type);
    bool sendPacket(PacketItem& item);
    void clearQueue();

    static int InterruptCallback(void* opaque);
private:
    AVFormatContext* _fmt_ctx;
    AVStream* _video_stream;
    AVStream* _audio_stream;
    OutputTarget _target;
    bool _headerWritten;
    bool _videoReady;
    bool _audioReady;
    AVRational _videoTimeBase;
    AVRational _audioTimeBase;
    AVCodecParameters* _videoCodecPar;
    AVCodecParameters* _audioCodecPar;

    std::atomic<bool> _running;
    std::thread _worker;

    std::deque<PacketItem> _queue;
    std::condition_variable _condition;

    std::atomic<bool> _waitingKeyFrame;

    std::mutex _configMutex;
    std::mutex _queueMutex;

    std::atomic<PublisherConnectionState> _connectionState{PublisherConnectionState::Idle};
    std::atomic<uint64_t> _totalByteSent{0};
    std::atomic<uint64_t> _videoPacketSent{0};
    std::atomic<uint64_t> _audioPacketSent{0};
    std::atomic<uint64_t> _droppedPackets{0};

    std::atomic<int64_t> _lastVideoPtsUs{AV_NOPTS_VALUE};
    std::atomic<int64_t> _lastAudioPtsUs{AV_NOPTS_VALUE};



signals:
    void connected();
    void connectionFailed();

};

#endif // NetworkPublisher_H
