#ifndef AUDIORTPRECEIVER_H
#define AUDIORTPRECEIVER_H

#include<QUdpSocket>
#include<cstddef>
#include<cstdint>
#include<functional>
#include<vector>


class AudioRtpReceiver
{

public:
    using AacCallback=std::function<void(std::vector<uint8_t>data,uint32_t timestamp)>;

public:
    AudioRtpReceiver();
    ~AudioRtpReceiver();

    bool open(uint16_t port);
    void close();

    void setAacCallback(AacCallback callback);

private:
    void onReadyRead();
    bool parseRtpPacket(const uint8_t* data,size_t size);
    bool handleAacPayload(const uint8_t* payload,size_t payloadSize,uint16_t sequence,uint32_t timestamp,bool marker);

private:
    QUdpSocket* _socket=nullptr;
    uint16_t _port=0;
    bool _opened=false;
    AacCallback _aacCallback;
    static constexpr uint8_t RTP_PAYLOAD_TYPE=97;

};

#endif // AUDIORTPRECEIVER_H
