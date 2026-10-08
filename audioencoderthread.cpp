#include "audioencoderthread.h"
#include"accencoder.h"
#include"encodepacket.h"
AudioEncoderThread::AudioEncoderThread()
    :_running(false),_sink(nullptr)
{

}

AudioEncoderThread::~AudioEncoderThread()
{
    stop();


}

bool AudioEncoderThread::start(const StreamConfig &config,EncodePacketSink* sink)
{
    if(_running){
        return false;
    }

    if(_thread.joinable()){
        _thread.join();
    }



    _config=config;
    _sink=sink;
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
    encoder.setPacketSink(_sink);
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
