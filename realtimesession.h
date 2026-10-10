#ifndef REALTIMESESSION_H
#define REALTIMESESSION_H

#include"encodepacket.h"
#include"rtpsender.h"
#include"audiortpsender.h"
class MultiPublisher;


class realtimesession:public EncodePacketSink
{
public:
    explicit realtimesession(MultiPublisher* publisher=nullptr);
    void setPublisher(MultiPublisher* publisher);
    void onEncoderPacket(const EncodePacket &packet) override;
    void onEncoderReady(MediaType type, AVCodecContext *codecCtx) override;

    void stopRtp();
    bool startVideoRtp(const std::string& ip,uint16_t port);
    bool startAudioRtp(const std::string& ip,uint16_t port);
    uint32_t videoRtpTimestampBase()const;
    uint32_t audioRtpTimestampBase()const;
    uint32_t videoRtpSsrc()const;
    uint32_t audioRtpSsrc()const;


private:
    MultiPublisher* _publisher=nullptr;
    rtpsender _videoRtpSender;
    AudioRtpSender _audioRtpSender;

};

#endif // REALTIMESESSION_H
