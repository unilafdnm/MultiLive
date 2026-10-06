#ifndef OUTPUTTYPES_H
#define OUTPUTTYPES_H
#include<string>
#include<cstdint>
#include<cstddef>

extern "C"{
#include<libavutil/avutil.h>
}

enum class OutputProtocol{
    RTMP,
    SRT
};

struct OutputTarget{
    OutputProtocol protocol=OutputProtocol::RTMP;
    std::string url;
};

enum class PublisherConnectionState{
    Idle,
    Connecting,
    Connected,
    Reconnecting
};

struct PublisherStats{
    OutputProtocol protocol=OutputProtocol::RTMP;
    std::string url;
    PublisherConnectionState state=PublisherConnectionState::Idle;
    uint64_t totalBytesSent=0;
    uint64_t videoPacketSent=0;
    uint64_t audioPacketSent=0;
    uint64_t droppedPackets=0;

    std::size_t queueSize=0;

    int64_t lastVideoPtsUs=AV_NOPTS_VALUE;
    int64_t lastAudioPtsUs=AV_NOPTS_VALUE;

};



#endif // OUTPUTTYPES_H


