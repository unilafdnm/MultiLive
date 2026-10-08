#ifndef REALTIMESESSION_H
#define REALTIMESESSION_H

#include"encodepacket.h"
#include"rtpsender.h"
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



private:
    MultiPublisher* _publisher;
    rtpsender _rtpSender;

};

#endif // REALTIMESESSION_H
