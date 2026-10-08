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
    if(!_publisher || !encoded.packet){
        return;
    }
    if(encoded.type == MediaType::Video){
        _publisher->writeVideoPacket(encoded.packet,encoded.timeBase);
    }else if(encoded.type == MediaType::Audio){
        _publisher->writeAudioPacket(encoded.packet,encoded.timeBase);
    }

    if(encoded.type == MediaType::Video && _rtpSender.isopen()){
        _rtpSender.sendH264(encoded.packet,encoded.timeBase);
    }

}

void realtimesession::onEncoderReady(MediaType type, AVCodecContext *codecCtx)
{
    if(!_publisher || !codecCtx){
        return;
    }

    if(type == MediaType::Video){
        _publisher->setVideoEncoder(codecCtx);
    }else if(type == MediaType::Audio){
        _publisher->setAudioEncoder(codecCtx);
    }

}

void realtimesession::stopRtp()
{
    _rtpSender.close();
}

bool realtimesession::startVideoRtp(const std::string &ip, uint16_t port)
{
    return _rtpSender.open(ip,port);
}
