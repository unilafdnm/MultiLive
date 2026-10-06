#include "screencapture.h"

#include"multipublisher.h"
#include"liveclock.h"
#include"h264encoder.h"

#include<iostream>
#include<QDebug>
#include<QImage>
#include <system_error>
extern "C"{
#include<libavformat/avformat.h>
#include<libavdevice/avdevice.h>
#include<libavcodec/avcodec.h>
#include<libswscale/swscale.h>
}

ScreenCapture::ScreenCapture(QObject *parent) : QObject(parent)
  ,_running(false),_clock(nullptr),_publisher(nullptr)
{

}

ScreenCapture::~ScreenCapture()
{
    stop();
}

void ScreenCapture::start(const StreamConfig& config)
{
    if(_running){
        return;
    }

    if(_captureThread.joinable()){
        _captureThread.join();
    }


    _running=true;

    try {
         _captureThread=std::thread(&ScreenCapture::captureLoop,this,config);
    } catch (const std::system_error& error) {
           _running=false;
           emit captureFailed(QStringLiteral("创建屏幕采集线程失败:%1").arg(QString::fromLocal8Bit(error.what())));
    };



}

void ScreenCapture::stop()
{
    _running=false;

    if(_captureThread.joinable()){
        _captureThread.join();
    }
      _encoderThread.stop();

}

void ScreenCapture::setClock(LiveClock *clock)
{
    _clock=clock;
}

void ScreenCapture::setPublisher(MultiPublisher *publisher)
{
    _publisher=publisher;
}

bool ScreenCapture::getRunning()
{
    return _running;
}

void ScreenCapture::captureLoop(const StreamConfig& config)
{
    avdevice_register_all();

    const AVInputFormat* input_fmt=av_find_input_format("gdigrab");
    if(input_fmt==nullptr){
        qDebug()<<"av_find_input_format(gdigrab) failed";
        _running=false;
        return;
    }
    AVFormatContext* fmt_ctx=avformat_alloc_context();
    if(!fmt_ctx){
        qDebug()<<"avformat_alloc_context failed";
        _running=false;
        return;
    }

    fmt_ctx->interrupt_callback.callback=&ScreenCapture::interruptCallback;
    fmt_ctx->interrupt_callback.opaque=this;

    AVDictionary* options=nullptr;

    av_dict_set(&options,"framerate",std::to_string(config.fps).c_str(),0);
    av_dict_set(&options,"draw_mouse","1",0);

    int ret=avformat_open_input(&fmt_ctx,"desktop",input_fmt,&options);
    av_dict_free(&options);
    if(ret < 0){
        qDebug()<<"avformat_open_input failed";
        qDebug()<<"avformat_open_input failed";
        char errorText[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret,errorText,sizeof(errorText));
        emit captureFailed(QStringLiteral("无法启动屏幕采集:%1").arg(QString::fromUtf8(errorText)));

        _running=false;
        return;
    }
    if(avformat_find_stream_info(fmt_ctx,nullptr) <0){
        qDebug()<<"avformat_find_stream_info failed";
        avformat_close_input(&fmt_ctx);

        _running=false;
        return;
    }

    int video_index=av_find_best_stream(fmt_ctx,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);

    if(video_index<0){
        qDebug()<<"video stream not found";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return;
    }

    AVStream* video_stream=fmt_ctx->streams[video_index];

    const AVCodec* decoder=avcodec_find_decoder(video_stream->codecpar->codec_id);
    if(decoder == nullptr){
        qDebug()<<"avcodec_find_decoder failed";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return;
    }
    AVCodecContext* codec_ctx=avcodec_alloc_context3(decoder);
    if(codec_ctx == nullptr){
        qDebug()<<"avcodec_alloc_context3 failed";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return;
    }
    avcodec_parameters_to_context(codec_ctx,video_stream->codecpar);
    if(avcodec_open2(codec_ctx,decoder,nullptr) < 0){
        qDebug()<<"avcodec_open2 failed";
        avformat_close_input(&fmt_ctx);
        avcodec_free_context(&codec_ctx);
        _running=false;
        return;
    }

    SwsContext* sws_ctx=nullptr;

    AVPacket* packet=av_packet_alloc();
    AVFrame* frame=av_frame_alloc();
    _encoderThread.start(config,_publisher);


    while(_running){
        ret=av_read_frame(fmt_ctx,packet);
        if(ret <0 ){
            av_packet_unref(packet);
            if(!_running.load()){
                break;
            }
            if(ret==AVERROR(EAGAIN)){
                continue;
            }

            char errorText[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret,errorText,sizeof(errorText));
            emit captureFailed(QStringLiteral("屏幕采集读取失败：%1").arg(QString::fromUtf8(errorText)));
            break;
        }
        if(packet->stream_index != video_index){
            av_packet_unref(packet);
            continue;
        }

        avcodec_send_packet(codec_ctx,packet);

        while(avcodec_receive_frame(codec_ctx,frame) == 0){

            int64_t timestampUs=0;
            if(_clock){
                timestampUs=_clock->elapsedUs();
            }

            _encoderThread.submit(frame,timestampUs);

            sws_ctx=sws_getCachedContext(sws_ctx,frame->width,frame->height,static_cast<AVPixelFormat>(frame->format)
                                         ,frame->width,frame->height,AV_PIX_FMT_RGB24,SWS_BILINEAR,nullptr,nullptr,nullptr);
            if(!sws_ctx){
                continue;
            }
            QImage image(frame->width,frame->height,QImage::Format_RGB888);
            uint8_t* dstData[4]={image.bits(),nullptr,nullptr,nullptr};
            int dstLineSize[4]={image.bytesPerLine(),0,0,0};

            sws_scale(sws_ctx,frame->data,frame->linesize,0,frame->height,dstData,dstLineSize);

            emit frameReady(image);

        }
        av_packet_unref(packet);

    }

    sws_freeContext(sws_ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    _running=false;




}

int ScreenCapture::interruptCallback(void *opaque)
{
    auto* self=static_cast<ScreenCapture*>(opaque);
    if(!self){
        return 1;
    }

    return self->_running?0:1;

}
