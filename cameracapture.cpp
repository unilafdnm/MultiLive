#include "cameracapture.h"
#include"encodepacket.h"
#include"liveclock.h"
#include<QDebug>
#include<iostream>
#include<system_error>
extern "C"
{
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include<libswscale/swscale.h>
}
CameraCapture::CameraCapture(QObject* parent)
    :_running(false),QObject(parent),_clock(nullptr),_sink(nullptr)
{

}

CameraCapture::~CameraCapture()
{
    stop();
}

void CameraCapture::start(const std::string &cameraName,const StreamConfig& config)
{
    if(_running){
        return;
    }

    if(_captureThread.joinable()){
        _captureThread.join();
    }
    _running.store(true);
    try {
         _captureThread=std::thread(&CameraCapture::captureLoop,this,cameraName,config);
    } catch (const std::system_error& error) {
        _running.store(false);
        qCritical() << "create camera capture thread failed:" << error.what();

    }


}

void CameraCapture::stop()
{
    _running=false;
    if(_captureThread.joinable()){
        _captureThread.join();
    }
    _encoderThread.stop();
}

void CameraCapture::setPacketSink(EncodePacketSink *sink)
{
    _sink=sink;
}


void CameraCapture::setClock(LiveClock *clock)
{
    _clock=clock;

}

bool CameraCapture::getRunning()
{
    return _running;
}

void CameraCapture::captureLoop(std::string cameraName,const StreamConfig& config)
{

    avdevice_register_all();
    const AVInputFormat* input_fmt=av_find_input_format("dshow");
    if(!input_fmt){
        _running=false;
        qDebug()<<"av_find_input_format dshow failed\n";
        return;
    }
    std::string deviceName="video="+cameraName;
    AVDictionary* options=nullptr;
    AVFormatContext* fmt_ctx=avformat_alloc_context();
    if(fmt_ctx == nullptr){
        _running=false;
        return;
    }
    fmt_ctx->interrupt_callback.callback=&CameraCapture::interruptCallback;
    fmt_ctx->interrupt_callback.opaque=this;
    av_dict_set(&options,"framerate",std::to_string(config.fps).c_str(),0);

    int ret=avformat_open_input(&fmt_ctx,deviceName.c_str(),input_fmt,&options);
    av_dict_free(&options);
    if(ret < 0){
        qDebug()<<"open camera failed";
        char errorText[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret,errorText,sizeof(errorText));
        _running=false;
        emit captureFailed(QStringLiteral("无法打开摄像头:%1").arg(QString::fromUtf8(errorText)));
        return;
    }
    if(avformat_find_stream_info(fmt_ctx,nullptr) <0){
        qDebug()<<"find stream info failed";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return;
    }

    int videoIndex=av_find_best_stream(fmt_ctx,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
    if(videoIndex<0){
        qDebug()<<"find video failed";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return ;
    }
    AVStream* video_stream=fmt_ctx->streams[videoIndex];
    const AVCodec* decoder=avcodec_find_decoder(video_stream->codecpar->codec_id);
    if(!decoder){
        qDebug()<<"avcodec_find_decoder failed";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return ;
    }
    AVCodecContext* codec_ctx=avcodec_alloc_context3(decoder);
    if(codec_ctx == nullptr){
        qDebug()<<"avcodec_alloc_context3 failed";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return;
    }

    if(avcodec_parameters_to_context(codec_ctx,video_stream->codecpar)<0){
        qDebug() << "avcodec_parameters_to_context failed";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        _running.store(false);
        return;
    }
    if(avcodec_open2(codec_ctx,decoder,nullptr)<0){
        qDebug()<<"open decoder failed";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        _running=false;
        return;
    }

    AVPacket* packet=av_packet_alloc();
    AVFrame* frame=av_frame_alloc();
    if(!packet || !frame){
        qDebug() << "allocate camera packet/frame failed";

        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);

        _running.store(false);
        return;
    }


    int frameCount=0;
    SwsContext* sws_ctx=nullptr;

    _encoderThread.start(config,_sink);

    while(_running){

        int readRet=av_read_frame(fmt_ctx,packet);

        if(readRet < 0){
            av_packet_unref(packet);
            if(!_running.load()){
                break;
            }

            if(readRet == AVERROR(EAGAIN)){
                continue;
            }

            if(readRet != AVERROR_EOF){
                char errorText[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(readRet,errorText,sizeof(errorText));
                qDebug()<<"camera av_read frame failed:"<<errorText;
            }

            break;
        }

        if(packet->stream_index != videoIndex){
            av_packet_unref(packet);
            continue;
        }
        int sendRet=avcodec_send_packet(codec_ctx,packet);

        if(sendRet<0){
            char errorText[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(sendRet,errorText,sizeof(errorText));

            qWarning()<< "camera decoder rejected packet:"<< errorText;

            avcodec_flush_buffers(codec_ctx);
            av_packet_unref(packet);
            continue;
        }

        while(true){
            int receiveRet=avcodec_receive_frame(codec_ctx,frame);
            if(receiveRet == AVERROR(EAGAIN) || receiveRet == AVERROR_EOF){
                break;
            }
            if(receiveRet<0){
                char errorText[AV_ERROR_MAX_STRING_SIZE]{};
                av_strerror(receiveRet,errorText,sizeof(errorText));

                qWarning()<< "camera decode frame failed:"<< errorText;

                av_frame_unref(frame);
                avcodec_flush_buffers(codec_ctx);
                break;
            }

            sws_ctx=sws_getCachedContext(sws_ctx,frame->width,frame->height,static_cast<AVPixelFormat>(frame->format),
                            frame->width,frame->height,AV_PIX_FMT_RGB24,SWS_BILINEAR,nullptr,nullptr,nullptr);
            if(!sws_ctx){
                std::cout<<"sws context failed\n";
                continue;
            }

            int64_t timestampUs=0;
            if(_clock){
               timestampUs=_clock->elapsedUs();
            }
            _encoderThread.submit(frame,timestampUs);


            QImage image(frame->width,frame->height,QImage::Format_RGB888);
            if(image.isNull()){
               qDebug()<<"allocate preview image failed";
               continue;
            }

            uint8_t* dstData[4]={image.bits(),nullptr,nullptr,nullptr};
            int dstLinesize[4]={image.bytesPerLine(),0,0,0};

            sws_scale(sws_ctx,frame->data,frame->linesize,0,frame->height,dstData,dstLinesize);
            emit frameReady(image);
            frameCount++;

            av_frame_unref(frame);
        }
        av_packet_unref(packet);
    }

    sws_freeContext(sws_ctx);
    sws_ctx=nullptr;

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    std::cout<<"camera stopped";
    _running=false;


}

int CameraCapture::interruptCallback(void *opaque)
{
    auto* self=static_cast<CameraCapture*>(opaque);

    if(!self){
        return 1;
    }

    return self->_running.load() ?0 :1;

}
