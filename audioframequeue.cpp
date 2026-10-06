#include "audioframequeue.h"

AudioFrameQueue::AudioFrameQueue(size_t maxSize)
    :_maxSize(maxSize),_stopped(false)
{

}

AudioFrameQueue::~AudioFrameQueue()
{
    stop();


}

bool AudioFrameQueue::push(const AVFrame *frame, int64_t timestampUs)
{
    if(!frame){
        return  false;
    }
    AVFrame *cloneFrame=av_frame_clone(frame);
    if(!cloneFrame){
        return false;
    }

    std::unique_lock<std::mutex> lock(_mutex);

    _cond.wait(lock,[this](){
       return _queue.size() < _maxSize || _stopped;
    });

    if(_stopped){
        lock.unlock();
        av_frame_free(&cloneFrame);
        return false;
    }

    AudioFrameItem item;
    item.frame=cloneFrame;
    item.timestampUs=timestampUs;

    _queue.push_back(item);

    _cond.notify_one();
    return true;

}

bool AudioFrameQueue::pop(AudioFrameItem &item)
{
    std::unique_lock<std::mutex> lock(_mutex);

    _cond.wait(lock,[this](){
       return !_queue.empty() || _stopped;
    });

    if(_stopped){
        return false;
    }

    item=_queue.front();
    _queue.pop_front();

    _cond.notify_one();
    return true;




}

void AudioFrameQueue::stop()
{
    std::lock_guard<std::mutex> lock(_mutex);

    _stopped=true;
    clearLocker();

    _cond.notify_all();

}

void AudioFrameQueue::reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _stopped=false;
    clearLocker();
}

void AudioFrameQueue::clearLocker()
{

    while(!_queue.empty()){
        AudioFrameItem item=_queue.front();
        av_frame_free(&item.frame);
        _queue.pop_front();
    }

}
