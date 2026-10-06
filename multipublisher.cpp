#include "multipublisher.h"

MultiPublisher::MultiPublisher(QObject* parent)
    :QObject(parent)
{

}

MultiPublisher::~MultiPublisher()
{
    close();
}

bool MultiPublisher::addOutput(const OutputTarget& target)
{

    auto publisher=std::make_unique<NetworkPublisher>();
    if(!publisher->prepare(target)){
        qDebug()<<"prepare RTMP failed";
        return false;
    }

    connect(publisher.get(),&NetworkPublisher::connectionFailed,this,[this](){
        emit connectionFailed();
    });

    connect(publisher.get(),&NetworkPublisher::connected,this,[this](){
        emit connected();
    });

    _publishers.push_back(std::move(publisher));
    return true;

}

void MultiPublisher::setVideoEncoder(AVCodecContext *codec_ctx)
{
    for(auto& publisher:_publishers){
        publisher->setVideoEncoder(codec_ctx);
    }

}

void MultiPublisher::setAudioEncoder(AVCodecContext *codec_ctx)
{

    for(auto& publisher:_publishers){
        publisher->setAudioEncoder(codec_ctx);
    }

}

void MultiPublisher::writeVideoPacket(const AVPacket *packet, AVRational encoderTimeBase)
{
    for(auto& publisher:_publishers){
        publisher->writeVideoPacket(packet,encoderTimeBase);
    }

}

void MultiPublisher::writeAudioPacket(const AVPacket *packet, AVRational encoderTimeBase)
{
    for(auto& publisher:_publishers){
        publisher->writeAudioPacket(packet,encoderTimeBase);
    }

}

void MultiPublisher::close()
{
    for(auto& publisher:_publishers){
        publisher->close();
    }
    _publishers.clear();

}

std::vector<PublisherStats> MultiPublisher::stats()
{
    std::vector<PublisherStats> result;
    result.reserve(_publishers.size());

    for(auto& publisher:_publishers){
        result.push_back(publisher->stats());
    }
    return result;
}
