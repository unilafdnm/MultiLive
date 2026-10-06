#include "microphoneCapture.h"
#include "accencoder.h"
#include"multipublisher.h"
#include"liveclock.h"
#include <iostream>
#include<QDebug>
extern "C"
{
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
}
MicrophoneCapture::MicrophoneCapture(QObject* parent)
    :_running(false),QObject(parent),_clock(nullptr),_publisher(nullptr)
{

}

MicrophoneCapture::~MicrophoneCapture()
{
    stop();
}

void MicrophoneCapture::start(const std::string &microphoneName,const StreamConfig& config)
{
    if(_running){
        return;
    }

    if(_captureThread.joinable()){
        _captureThread.join();
    }


    _running=true;

    _captureThread=std::thread(&MicrophoneCapture::captureLoop,this,microphoneName,config);

}

void MicrophoneCapture::stop()
{
    _running=false;
    if(_captureThread.joinable()){
        _captureThread.join();
    }
    _encoderThread.stop();

}

void MicrophoneCapture::setPublisher(MultiPublisher *publisher)
{
    _publisher=publisher;

}

void MicrophoneCapture::setClock(LiveClock *clock)
{
    _clock=clock;
}

void MicrophoneCapture::captureLoop(std::string microphoneName,const StreamConfig& config)
{
    avdevice_register_all();

    const AVInputFormat* input_fmx=av_find_input_format("dshow");
    if(!input_fmx){
        qDebug()<<"av_find_input_format failed";
        _running=false;
        return;
    }
    AVFormatContext* fmt_ctx=avformat_alloc_context();
    if(!fmt_ctx){
        qDebug()<<"avformat_alloc_context failed";
        _running=false;
        return;
    }
    fmt_ctx->interrupt_callback.callback=&MicrophoneCapture::interruptCallback;
    fmt_ctx->interrupt_callback.opaque=this;

    std::string device_name="audio="+microphoneName;
    int ret=avformat_open_input(&fmt_ctx,device_name.c_str(),input_fmx,nullptr);
    if(ret < 0){
        qDebug()<<"avformat_open_input failed";
        char errorText[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret,errorText,sizeof(errorText));
        emit captureFailed(QStringLiteral("无法打开麦克风:%1").arg(QString::fromUtf8(errorText)));

        _running=false;
        return;
    }
    if(avformat_find_stream_info(fmt_ctx,nullptr)<0){
        qDebug()<<"avformat_find_stream_info failed";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return;
    }
    int audio_index=av_find_best_stream(fmt_ctx,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
    if(audio_index < 0){
        qDebug()<<"find audio stream failed";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return;
    }

    AVStream* audio_stream=fmt_ctx->streams[audio_index];

    const AVCodec* decoder=avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if(!decoder){
        qDebug()<<"avcodec_find_audio_decoder failed";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return;
    }
    AVCodecContext* codec_ctx=avcodec_alloc_context3(decoder);
    if(!codec_ctx){
        qDebug()<<"avcodec_alloc_context failed";
        avformat_close_input(&fmt_ctx);
        _running=false;
        return;
    }
    avcodec_parameters_to_context(codec_ctx,audio_stream->codecpar);
    if(avcodec_open2(codec_ctx,decoder,nullptr) <0 ){
        qDebug()<<"avcodec_open2 failed";
        avformat_close_input(&fmt_ctx);
        avcodec_free_context(&codec_ctx);
        _running=false;
        return;
    }

    AVPacket* packet=av_packet_alloc();
    AVFrame* frame=av_frame_alloc();

    qDebug()<<_publisher;
    _encoderThread.start(config,_publisher);


    int frame_count=0;
    while(_running){
        ret=av_read_frame(fmt_ctx,packet);
        if(ret <0){
            av_packet_unref(packet);
            continue;
        }
        if(packet->stream_index != audio_index){
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

            frame_count++;
            const char* formatName=av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format));
            std::cout<<"audio frame:"<<frame_count<<" samples="<<frame->nb_samples<<" sample_rate"<<
                       frame->sample_rate<<" channels:"<<frame->ch_layout.nb_channels<<" format:"<<
                       formatName<<std::endl;
        }

        av_packet_unref(packet);
    }


    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);


}

int MicrophoneCapture::interruptCallback(void *opaque)
{
    auto* self=static_cast<MicrophoneCapture*>(opaque);

    if(!self){
        return 1;
    }

    return self->_running?0:1;


}
