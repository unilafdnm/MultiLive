#ifndef ENCODEPACKET_H
#define ENCODEPACKET_H

extern "C"{
#include<libavcodec/avcodec.h>
#include<libavutil/avutil.h>
}

enum class MediaType{
    Video,
    Audio
};

struct EncodePacket{
    MediaType type;
    const AVPacket* packet=nullptr;
    AVRational timeBase{0,1};

    bool isKeyFrame()const{
        return packet && (packet->flags&AV_PKT_FLAG_KEY);
    }

    int64_t ptsUs()const{
        if(!packet || packet->pts == AV_NOPTS_VALUE){
            return AV_NOPTS_VALUE;
        }

        return av_rescale_q(packet->pts,timeBase,AV_TIME_BASE_Q);

    }

};


class EncodePacketSink{

public:
    virtual ~EncodePacketSink()=default;

    //编码器初始化完成
    virtual void onEncoderReady(MediaType type,AVCodecContext* codecCtx)=0;
    //编码完成一个AVPacket
    virtual void onEncoderPacket(const EncodePacket& packet)=0;

};




#endif // ENCODEPACKET_H
