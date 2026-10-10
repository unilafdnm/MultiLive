#ifndef RTPSENDER_H
#define RTPSENDER_H

#include <cstdint>
#include <string>
#include <vector>

#include <QUdpSocket>
#include <QHostAddress>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

class rtpsender
{
public:
    rtpsender();
    ~rtpsender();
    bool open(const std::string& ip,uint16_t port);
    void close();
    bool isopen()const;
    bool sendH264(const AVPacket*packet,AVRational timeBase);
    uint32_t timestampBase()const;
    uint32_t ssrc()const;


private:
    struct Nalu{
        const uint8_t* data=nullptr;
        size_t size=0;
    };

private:
    std::vector<Nalu> splitNalus(const uint8_t* data,size_t size);
    bool sendNalu(const uint8_t* data,size_t size,uint32_t timestamp,bool market);
    bool sendSingleNalu(const uint8_t* data,size_t size,uint32_t timestamp,bool market);
    bool sendFuA(const uint8_t* data,size_t size,uint32_t timestamp,bool market);
    bool sendRtpPacket(const uint8_t* data,size_t size,uint32_t timestamp,bool market);

private:
    QUdpSocket _socket;
    QHostAddress _remoteAddress;
    quint16 _remoteport=0;
    bool _opened=false;
    uint16_t _sequence=0;
    uint32_t _ssrc=0;
    uint32_t _timestampBase=0;

    static constexpr uint8_t RTP_PAYLOAD_TYPE=96;
    static constexpr size_t MAX_RTP_PAYLOAD=1200;

};

#endif // RTPSENDER_H
