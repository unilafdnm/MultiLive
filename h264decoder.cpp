#include "h264decoder.h"
#include<QDebug>
#include<cstring>
H264Decoder::H264Decoder(QObject *parent) : QObject(parent)
{

}

H264Decoder::~H264Decoder()
{
    close();
}

bool H264Decoder::open()
{
    if(_opened){
        return false;
    }

    const AVCodec* decoder=avcodec_find_decoder(AV_CODEC_ID_H264);
    if(!decoder){
        qDebug()<<"H264 decoder not found";
        return false;
    }

    _codecCtx=avcodec_alloc_context3(decoder);
    if(!_codecCtx){
        qDebug()<<"avcodec_alloc_context3 failed";
        return false;
    }
    //RTP H264 timebase使用90kHz
    _codecCtx->pkt_timebase=AVRational{1,90000};

    //编码器没有采用B帧，接收端也尽量减少解码缓存
    _codecCtx->flags|=AV_CODEC_FLAG_LOW_DELAY;

    //第一版为了降低frame-thread额外延迟，先使用单线程解码
    _codecCtx->thread_count=1;
    if(avcodec_open2(_codecCtx,decoder,nullptr)<0){
        qDebug()<<"avcodec_open2 H264 decoder failed";
        avcodec_free_context(&_codecCtx);
        return false;
    }

    _packet=av_packet_alloc();
    _frame=av_frame_alloc();
    if(!_packet || !_frame){
        qDebug()<<"allocate decoder packet/frame failed";
        close();
        return false;
    }
    _accessUnitBuffer.clear();
    _hasTimestamp=false;
    _opened=true;
    qDebug()<<"h264 decoder opened";
    return true;


}

void H264Decoder::close()
{
    if(_codecCtx){
        avcodec_send_packet(_codecCtx,nullptr);
    }
    if(_frame){
        av_frame_free(&_frame);
    }
    if(_packet){
        av_packet_free(&_packet);
    }
    if(_swsCtx){
        sws_free_context(&_swsCtx);
    }
    if(!_codecCtx){
        avcodec_free_context(&_codecCtx);
    }
    _accessUnitBuffer.clear();
    _hasTimestamp=false;
    _opened=false;

}

void H264Decoder::pushNalu(const QByteArray &nalu, quint32 timestamp, bool market)
{

    if(!_opened || nalu.isEmpty()){
        return;
    }

    if(!_hasTimestamp){
        _hasTimestamp=true;
        _currentTimestamp=timestamp;
    }

    if(timestamp != _currentTimestamp){

        if(!_accessUnitBuffer.isEmpty()){
            qDebug()<<"drop incomplete access unit:old timestamp"<<_currentTimestamp<<" new stamp"<<timestamp;
            _accessUnitBuffer.clear();
            _currentTimestamp=timestamp;
        }

    }

    _accessUnitBuffer.append(nalu);

    if(market){
        decodeAccessUnit(_accessUnitBuffer,_currentTimestamp);
        _accessUnitBuffer.clear();
        _hasTimestamp=false;
    }

}

void H264Decoder::decodeAccessUnit(const QByteArray &data, quint32 timestamp)
{

    if(!_opened || data.isEmpty()){
        return;
    }

    av_packet_unref(_packet);

    if(av_new_packet(_packet,data.size())<0){
        qDebug()<<"av_new_packet failed";
        return;
    }
    std::memcpy(_packet->data,data.constData(),data.size());
    _packet->pts=static_cast<int64_t>(timestamp);
    _packet->dts=static_cast<int64_t>(timestamp);

    int ret=avcodec_send_packet(_codecCtx,_packet);
    if(ret<0){
        char errorText[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret,errorText,sizeof(errorText));
        qDebug()<<"send H264 packet failed:"<<errorText;
        return;
    }

    while(true){
        ret=avcodec_receive_frame(_codecCtx,_frame);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            break;
        }
        if(ret<0){
            char errorText[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret,errorText,sizeof(errorText));
            qDebug()<<"decode H264 failed:"<<errorText;
            break;
        }
        qDebug()<<"Decoded frame:"<<_frame->width<<"x"<<_frame->height<<" format="<<_frame->format<<" pts="<<_frame->pts;
        _swsCtx=sws_getCachedContext(_swsCtx,_frame->width,_frame->height,static_cast<AVPixelFormat>(_frame->format),
                                     _frame->width,_frame->height,AV_PIX_FMT_RGB24,SWS_BILINEAR,nullptr,nullptr,nullptr);
        if(!_swsCtx){
            qDebug()<<"sws_getCachedContext failed";
            continue;
        }

        QImage image(_frame->width,_frame->height,QImage::Format_RGB888);
        if(image.isNull()){
            qDebug()<<"create QImage failed";
            continue;
        }

        uint8_t* dstData[4]={image.bits(),nullptr,nullptr,nullptr};
        int dstLinesize[4]={image.bytesPerLine(),0,0,0};

        sws_scale(_swsCtx,_frame->data,_frame->linesize,0,_frame->height,dstData,dstLinesize);

        emit frameReady(image);
        av_frame_unref(_frame);

    }



}
