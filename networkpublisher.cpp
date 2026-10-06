#include "networkpublisher.h"
#include<QDebug>

namespace  {

const char* muxerNameFroProtocol(OutputProtocol protocol){

    switch (protocol) {
    case OutputProtocol::RTMP:
        return "flv";
    case OutputProtocol::SRT:
        return "mpegts";

    }

    return nullptr;
}

}



NetworkPublisher::NetworkPublisher(QObject *parent) : QObject(parent)
  ,_fmt_ctx(nullptr),_video_stream(nullptr),_audio_stream(nullptr)
  ,_videoCodecPar(nullptr),_audioCodecPar(nullptr)
  ,_videoTimeBase{0,1},_audioTimeBase{0,1},_videoReady(false),_audioReady(false)
  ,_headerWritten(false),_running(false),_waitingKeyFrame(true)
{

}

NetworkPublisher::~NetworkPublisher()
{
    close();
}

bool NetworkPublisher::prepare(const OutputTarget& target)
{

    if(target.url.empty()){
        return false;
    }

    switch (target.protocol) {
    case OutputProtocol::RTMP:
    case OutputProtocol::SRT:
        break;

    default:
        return false;
    }


    std::lock_guard<std::mutex> _lock(_configMutex);

    _target=target;
    return true;
}

void NetworkPublisher::setVideoEncoder(AVCodecContext *codec_ctx)
{

    if(!codec_ctx) return;

    {
        std::lock_guard<std::mutex> lock(_configMutex);

        if(!_videoCodecPar){
            _videoCodecPar=avcodec_parameters_alloc();
        }
        if(!_videoCodecPar){
            return;
        }
        avcodec_parameters_from_context(_videoCodecPar,codec_ctx);
        _videoCodecPar->codec_tag=0;
        _videoTimeBase=codec_ctx->time_base;
        _videoReady=true;
    }

    tryStartWorker();






}

void NetworkPublisher::setAudioEncoder(AVCodecContext *codec_ctx)
{


    if(!codec_ctx) return;

    {
        std::lock_guard<std::mutex> lock(_configMutex);

        if(!_audioCodecPar){
            _audioCodecPar=avcodec_parameters_alloc();
        }
        if(!_audioCodecPar){
            return;
        }
        avcodec_parameters_from_context(_audioCodecPar,codec_ctx);
        _audioCodecPar->codec_tag=0;
        _audioTimeBase=codec_ctx->time_base;
        _audioReady=true;
    }

    tryStartWorker();

}

void NetworkPublisher::writeVideoPacket(const AVPacket *packet, AVRational encoderTimeBase)
{


   enqueuePacket(packet,encoderTimeBase,PacketType::Video);

}

void NetworkPublisher::writeAudioPacket(const AVPacket *packet, AVRational encoderTimeBase)
{
   enqueuePacket(packet,encoderTimeBase,PacketType::Audio);
}

void NetworkPublisher::close()
{
    _connectionState=PublisherConnectionState::Idle;

    _running=false;
    _condition.notify_all();

    if(_worker.joinable()){
        _worker.join();
    }

    clearQueue();
    disconnection(true);

    if(_videoCodecPar){
        avcodec_parameters_free(&_videoCodecPar);
    }
    if(_audioCodecPar){
        avcodec_parameters_free(&_audioCodecPar);
    }
    _videoReady=false;
    _audioReady=false;
    _waitingKeyFrame=true;

    return;
}

PublisherStats NetworkPublisher::stats()
{
    PublisherStats result;
    {
        std::lock_guard<std::mutex> lock(_configMutex);
        result.url=_target.url;
        result.protocol=_target.protocol;
    }
    result.state=_connectionState;
    result.totalBytesSent=_totalByteSent;
    result.videoPacketSent=_videoPacketSent;
    result.audioPacketSent=_audioPacketSent;
    result.droppedPackets=_droppedPackets;
    result.lastVideoPtsUs=_lastVideoPtsUs;
    result.lastAudioPtsUs=_lastAudioPtsUs;

    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        result.queueSize=_queue.size();
    }

    return result;
}

void NetworkPublisher::tryStartWorker()
{
    std::lock_guard<std::mutex> lock(_configMutex);

    if(_running || _target.url.empty() || !_videoReady || !_audioReady){
        return;
    }

    _running=true;
    _connectionState=PublisherConnectionState::Connecting;

    _worker=std::thread(&NetworkPublisher::workerLoop,this);


}

void NetworkPublisher::workerLoop()
{

    while(_running){

        if(!_fmt_ctx){

            if(!connectServer()){
                if(_running){
                    _connectionState=PublisherConnectionState::Reconnecting;
                    emit connectionFailed();
                }

                for(int i=0;i<20&&_running;i++){
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                continue;
            }

            if(_running){
                _connectionState=PublisherConnectionState::Connected;
                emit connected();
            }

        }

        PacketItem item;

        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _condition.wait_for(lock,std::chrono::milliseconds(200),[this](){
               return !_queue.empty() || !_running;
            });

            if(!_running){
                break;
            }
            if(_queue.empty()){
                continue;
            }
            item=_queue.front();
            _queue.pop_front();
        }

        bool success=sendPacket(item);
        av_packet_free(&item.packet);

        if(!success){
            _connectionState=PublisherConnectionState::Reconnecting;
            qDebug()<<"RTMP disconnected , reconnecting...";
            disconnection(false);
            clearQueue();
            _waitingKeyFrame=true;

            for(int i=0;i<20&&_running;i++){
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

        }



   }
    _connectionState=PublisherConnectionState::Idle;
    disconnection(true);

}

bool NetworkPublisher::connectServer()
{
    disconnection(false);

    const char* muxerName=muxerNameFroProtocol(_target.protocol);

    if(!muxerName){
        return false;
    }

    if(avformat_alloc_output_context2(&_fmt_ctx,nullptr,muxerName,_target.url.c_str()) < 0){
        return false;
    }

    _video_stream=avformat_new_stream(_fmt_ctx,nullptr);
    if(!_video_stream){
        disconnection(false);
        return false;
    }

    avcodec_parameters_copy(_video_stream->codecpar,_videoCodecPar);
    _video_stream->codecpar->codec_tag=0;
    _video_stream->time_base=_videoTimeBase;

    _audio_stream=avformat_new_stream(_fmt_ctx,nullptr);
    if(!_audio_stream){
        disconnection(false);
        return false;
    }
    _fmt_ctx->interrupt_callback.callback=&NetworkPublisher::InterruptCallback;
    _fmt_ctx->interrupt_callback.opaque=this;

    avcodec_parameters_copy(_audio_stream->codecpar,_audioCodecPar);
    _audio_stream->time_base=_audioTimeBase;

    _fmt_ctx->avoid_negative_ts=AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE;

    AVDictionary* options=nullptr;

    av_dict_set(&options,"rw_timeout","3000000",0);


    if(avio_open2(&_fmt_ctx->pb,_target.url.c_str(),AVIO_FLAG_WRITE,&_fmt_ctx->interrupt_callback,&options) <0){
        av_dict_free(&options);
        disconnection(false);
        return false;
    }
    av_dict_free(&options);

    int ret=avformat_write_header(_fmt_ctx,nullptr);
    if(ret < 0){
        disconnection(false);
        return false;
    }

    _headerWritten=true;

    _waitingKeyFrame=true;

    return true;

}

void NetworkPublisher::disconnection(bool writeTrailer)
{
    if(!_fmt_ctx){
        return;
    }

    if(writeTrailer && _headerWritten){
        av_write_trailer(_fmt_ctx);
    }

    if(_fmt_ctx->pb){
        avio_closep(&_fmt_ctx->pb);
    }

    avformat_free_context(_fmt_ctx);

    _fmt_ctx=nullptr;
    _video_stream=nullptr;
    _audio_stream=nullptr;
    _headerWritten=false;


}

void NetworkPublisher::enqueuePacket(const AVPacket *packet, AVRational timeBase, NetworkPublisher::PacketType type)
{
    if(!_running || !packet){
        return;
    }

    AVPacket* packet_copy=av_packet_clone(packet);

    if(!packet_copy){
        _droppedPackets+=1;
        return;
    }

    std::lock_guard<std::mutex> lock(_queueMutex);


    if(_queue.size() >= 300){
        _droppedPackets+=_queue.size();
        for(auto& item : _queue){
            av_packet_free(&item.packet);
        }

        _queue.clear();
        _waitingKeyFrame=true;
    }

    if(_waitingKeyFrame){

        if(type == PacketType::Audio){
            _droppedPackets+=1;
            av_packet_free(&packet_copy);
            return;
        }

        if(!(packet_copy->flags & AV_PKT_FLAG_KEY)){
            _droppedPackets+=1;
            av_packet_free(&packet_copy);
            return;
        }

    }



    PacketItem item;
    item.type=type;
    item.packet=packet_copy;
    item.timebase=timeBase;


    _queue.push_back(item);

    _condition.notify_one();

}

bool NetworkPublisher::sendPacket(NetworkPublisher::PacketItem &item)
{
    if(!_fmt_ctx || !item.packet){
        return false;
    }
    AVStream* stream=nullptr;

    if(item.type == PacketType::Audio){
        stream=_audio_stream;
    }
    else{
        stream=_video_stream;
    }

    if(_waitingKeyFrame){

        if(item.type == PacketType::Audio){
            _droppedPackets+=1;
            return true;
        }

        if(!(item.packet->flags & AV_PKT_FLAG_KEY)){
            _droppedPackets+=1;
            return true;
        }
        _waitingKeyFrame=false;
    }

    int64_t ptsUs=AV_NOPTS_VALUE;
    if(item.packet->pts != AV_NOPTS_VALUE){
        ptsUs=av_rescale_q(item.packet->pts,item.timebase,AV_TIME_BASE_Q);
    }


    av_packet_rescale_ts(item.packet,item.timebase,stream->time_base);
    item.packet->stream_index=stream->index;
    item.packet->pos=-1;
    const int packetBytes=item.packet->size;
    int ret=av_interleaved_write_frame(_fmt_ctx,item.packet);
    if(ret <0 ){
        return false;
    }
    _totalByteSent+=packetBytes;
    if(item.type == PacketType::Video){
        _videoPacketSent+=1;

        if(ptsUs!=AV_NOPTS_VALUE){
            _lastVideoPtsUs=ptsUs;
        }

    }else{
        _audioPacketSent+=1;
        if(ptsUs!=AV_NOPTS_VALUE){
            _lastAudioPtsUs=ptsUs;
        }
    }

    return true;

}

void NetworkPublisher::clearQueue()
{

    std::lock_guard<std::mutex> lock(_queueMutex);

    for(auto& item : _queue){

        av_packet_free(&item.packet);

    }
    _queue.clear();

}


int NetworkPublisher::InterruptCallback(void* opaque){
    auto* self=static_cast<NetworkPublisher*>(opaque);
    if(!self){
        return 1;
    }
    return self->_running?0:1;

}
