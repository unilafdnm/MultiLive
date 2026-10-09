#ifndef AUDIORTPSENDER_H
#define AUDIORTPSENDER_H

#include <atomic>
#include <cstdint>
#include <string>

#include <QUdpSocket>
#include <QHostAddress>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

class AudioRtpSender
{
public:
    AudioRtpSender();
    ~AudioRtpSender();
    bool open(const std::string& ip,uint16_t port,uint32_t sampleRate=48000);
    void close();
    bool isOpen()const;
    bool sendAAC(const uint8_t* data,size_t size,uint32_t timestamp);

private:
    bool sendRtpPacket(const uint8_t* data,size_t size,uint32_t timestamp,bool marker);



private:
    QUdpSocket* _socket=nullptr;
    QHostAddress _remoteAddress;
    quint16 _remotePort=0;

    std::atomic_bool _opened{false};

    uint16_t _sequence=0;
    uint32_t _ssrc=0;
    uint32_t _timestampBase=0;

    uint32_t _sampleRate=48000;

    static constexpr uint8_t RTP_PAYLOAD_TYPE=97;
    static constexpr int MAX_RTP_PAYLOAD=1200;
    static constexpr int AU_HEADER_SIZE=0;

};

#endif // AUDIORTPSENDER_H
