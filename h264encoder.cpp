#include "h264encoder.h"
#include"multipublisher.h"
#include <iostream>
#include<QDebug>
extern "C"
{
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}


H264Encoder::H264Encoder(QObject *parent) : QObject(parent),_codec_ctx(nullptr),_sws_ctx(nullptr)
    ,_frame(nullptr),_packet(nullptr),_lastPts(AV_NOPTS_VALUE),_initialized(false),_file(nullptr),_publisher(nullptr)
{

}

H264Encoder::~H264Encoder()
{
    close();
}

bool H264Encoder::init(int inputwidth, int inputheight, AVPixelFormat inputformat,const StreamConfig& config)
{
    if(_initialized){
        return true;
    }

    close();

    const AVCodec* encoder=avcodec_find_encoder_by_name("libx264");
    if(!encoder){
        qDebug()<<"libx264 encoder not found";
        return false;
    }
    _codec_ctx=avcodec_alloc_context3(encoder);
    if(!_codec_ctx){
        return false;
    }
    _codec_ctx->width=config.width;
    _codec_ctx->height=config.height;
    _codec_ctx->pix_fmt=AV_PIX_FMT_YUV420P;
    _codec_ctx->time_base=AVRational{1,config.fps};
    _codec_ctx->framerate=AVRational{config.fps,1};
    _codec_ctx->bit_rate=config.videoBitrate;
    _codec_ctx->gop_size=config.fps * config.gopSeconds;
    _codec_ctx->max_b_frames=0;

    //libx264低延迟设置
    if(std::string(encoder->name) == "libx264"){
        const std::string preset=config.preset.empty()?std::string("veryfast"):config.preset;
        av_opt_set(_codec_ctx->priv_data,"preset",preset.c_str(),0);
        av_opt_set(_codec_ctx->priv_data,"tune","zerolatency",0);
    }

    _codec_ctx->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;

    if(avcodec_open2(_codec_ctx,encoder,nullptr) <0){
        qDebug()<<"encoder avcodec_open2 failed";
        return false;
    }
    _frame=av_frame_alloc();
    _frame->width=config.width;
    _frame->height=config.height;
    _frame->format=AV_PIX_FMT_YUV420P;
    av_frame_get_buffer(_frame,32);

    _sws_ctx=sws_getContext(inputwidth,inputheight,inputformat,config.width,config.height,AV_PIX_FMT_YUV420P,SWS_BILINEAR,nullptr,nullptr,nullptr);
    if(!_sws_ctx){
        qDebug()<<"encoder sws_getContext failed";
        return false;
    }

    _packet=av_packet_alloc();
    _lastPts=AV_NOPTS_VALUE;
    _initialized=true;

    if(_publisher){
        _publisher->setVideoEncoder(_codec_ctx);
    }

    return true;
}

void H264Encoder::encode(AVFrame *frame,int64_t timestampUs)
{
    if(!_initialized){
        return;
    }
    if(av_frame_make_writable(_frame) <0){
        return;
    }

    sws_scale(_sws_ctx,frame->data,frame->linesize,0,frame->height,_frame->data,_frame->linesize);
    int64_t pts=av_rescale_q(timestampUs,AV_TIME_BASE_Q,_codec_ctx->time_base);
    if(_lastPts!=AV_NOPTS_VALUE && pts <=_lastPts){
        pts=_lastPts+1;
    }
    _frame->pts=pts;
    _lastPts=pts;

    int ret=avcodec_send_frame(_codec_ctx,_frame);
    if(ret < 0){
        qDebug()<<"send frame failed";
        return;
    }
    while(true){
        ret=avcodec_receive_packet(_codec_ctx,_packet);
        if(ret == AVERROR(EAGAIN) || ret==AVERROR_EOF){
            break;
        }
        if(ret <0){
            qDebug()<<"receive packet failed";
            break;
        }
        std::cout<<"H264 packet size="<<_packet->size<<" pts="<<_packet->pts<<std::endl;
        if(_publisher){
            _publisher->writeVideoPacket(_packet,_codec_ctx->time_base);
        }

        av_packet_unref(_packet);
    }

}

void H264Encoder::close()
{


    if(_initialized && _codec_ctx && _packet){
        int sendRet=avcodec_send_frame(_codec_ctx,nullptr);

        if(sendRet >=0 ){
            while(true){
                int ret=avcodec_receive_packet(_codec_ctx,_packet);
                if(ret == AVERROR(EAGAIN) || ret==AVERROR_EOF){
                    break;
                }
                if(ret <0){
                    qDebug()<<"receive packet failed";
                    break;
                }

                if(_publisher){
                    _publisher->writeVideoPacket(_packet,_codec_ctx->time_base);
                }
                av_packet_unref(_packet);
            }
        }

    }


    if(_file){
        fclose(_file);
        _file=nullptr;
    }
    sws_freeContext(_sws_ctx);
    _sws_ctx=nullptr;
    av_packet_free(&_packet);
    av_frame_free(&_frame);
    avcodec_free_context(&_codec_ctx);
    _initialized=false;
    _lastPts=AV_NOPTS_VALUE;
}

void H264Encoder::setPublisher(MultiPublisher *publisher)
{
    _publisher=publisher;

}
