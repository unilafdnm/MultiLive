#include "audioencoderthread.h"
#include"accencoder.h"
AudioEncoderThread::AudioEncoderThread()
    :_running(false),_publisher(nullptr)
{

}

AudioEncoderThread::~AudioEncoderThread()
{
    stop();


}

bool AudioEncoderThread::start(const StreamConfig &config, MultiPublisher *publisher)
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
    _thread=std::thread(&AudioEncoderThread::workerLoop,this);
    return true;
}

void AudioEncoderThread::stop()
{
    _running=false;
    _queue.stop();

    if(_thread.joinable()){
        _thread.join();
    }
}

bool AudioEncoderThread::submit(const AVFrame *frame, int64_t timestampUs)
{
    if(!_running || !frame){
        return false;
    }
    return _queue.push(frame,timestampUs);

}

void AudioEncoderThread::workerLoop()
{
    accencoder encoder;
    bool initialized=false;
    encoder.setPublisher(_publisher);
    AudioFrameItem item;

    while(_running && _queue.pop(item)){

        if(!initialized){
            initialized=encoder.init(item.frame->sample_rate,item.frame->ch_layout,static_cast<AVSampleFormat>(item.frame->format),_config);

        }
        if(!initialized){
            av_frame_free(&item.frame);
            break;
        }
        if(initialized){
            encoder.encode(item.frame,item.timestampUs);
            av_frame_free(&item.frame);
        }

    }

    _running=false;
    _queue.stop();
    encoder.close();


}
