#include "realtimesession.h"
#include"multipublisher.h"
realtimesession::realtimesession(MultiPublisher *publisher)
    :_publisher(publisher)
{

}

void realtimesession::setPublisher(MultiPublisher *publisher)
{
    _publisher=publisher;
}

void realtimesession::onEncoderPacket(const EncodePacket &encoded)
{
    if(!encoded.packet){
        return;
    }

    if(_publisher){
        if(encoded.type == MediaType::Video){
            _publisher->writeVideoPacket(encoded.packet,encoded.timeBase);
        }else if(encoded.type == MediaType::Audio){
            _publisher->writeAudioPacket(encoded.packet,encoded.timeBase);
        }
    }
    //实时视频RTP
    if(encoded.type == MediaType::Video && _videoRtpSender.isopen()){
        _videoRtpSender.sendH264(encoded.packet,encoded.timeBase);
    }
    //实时音频RTP
    if(encoded.type == MediaType::Audio && _audioRtpSender.isOpen()){
        _audioRtpSender.sendAAc(encoded.packet,encoded.timeBase);
    }


}

void realtimesession::onEncoderReady(MediaType type, AVCodecContext *codecCtx)
{
    if(!codecCtx){
        return;
    }

    if(_publisher){
        if(type == MediaType::Video){
            _publisher->setVideoEncoder(codecCtx);
        }else if(type == MediaType::Audio){
            _publisher->setAudioEncoder(codecCtx);
        }
    }

    if(type == MediaType::Audio){
        _audioRtpSender.setSampleRate(codecCtx->sample_rate);
    }

}

void realtimesession::stopRtp()
{
    _videoRtpSender.close();
    _audioRtpSender.close();
}

bool realtimesession::startVideoRtp(const std::string &ip, uint16_t port)
{
    return _videoRtpSender.open(ip,port);
}

bool realtimesession::startAudioRtp(const std::string &ip, uint16_t port)
{
    return _audioRtpSender.open(ip,port);
}
