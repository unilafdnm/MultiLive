#include "videoencoderthread.h"
#include"h264encoder.h"
#include"multipublisher.h"
VideoEncoderThread::VideoEncoderThread()
    :_running(false),_publisher(nullptr)
{

}

VideoEncoderThread::~VideoEncoderThread()
{
    stop();
}

bool VideoEncoderThread::start(const StreamConfig &config, MultiPublisher *publisher)
{
    if(_running){
        return false;
    }
    if(_thread.joinable()){
        _thread.join();
    }

    _config=config;
    _publisher=publisher;
    _running=true;
    _queue.reset();
    _thread=std::thread(&VideoEncoderThread::workerLoop,this);
    return true;
}

bool VideoEncoderThread::submit(const AVFrame *frame, int64_t timestampUs)
{
    if(!_running || !frame){
        return false;
    }

    int ret=_queue.push(frame,timestampUs);
    return ret;
}

void VideoEncoderThread::stop()
{
    _running=false;

    _queue.stop();

    if(_thread.joinable()){
        _thread.join();
    }

}

void VideoEncoderThread::workerLoop()
{
    VideoFrameItem item;
    H264Encoder encoder;
    encoder.setPublisher(_publisher);
    bool initialiezd=false;


    while(_running && _queue.pop(item)){

        if(!initialiezd){
            initialiezd=encoder.init(item.frame->width,item.frame->height,static_cast<AVPixelFormat>(item.frame->format),_config);
        }
        if(initialiezd){
            encoder.encode(item.frame,item.timestampUs);
        }
        av_frame_free(&item.frame);
    }
    encoder.close();

}
