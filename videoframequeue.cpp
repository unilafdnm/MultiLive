#include "videoframequeue.h"

VideoFrameQueue::VideoFrameQueue(size_t maxSize)
    :_maxSize(maxSize),_stopped(false)
{

}

VideoFrameQueue::~VideoFrameQueue()
{
    stop();
}

bool VideoFrameQueue::push(const AVFrame *frame, int64_t timestampUs)
{
    if(!frame){
        return false;
    }
    AVFrame* cloneFrame=av_frame_clone(frame);
    if(!cloneFrame){
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        if(_stopped){
            av_frame_free(&cloneFrame);
            return false;
        }
        while(_queue.size() >= _maxSize){
            VideoFrameItem item=_queue.front();
            _queue.pop_front();
            av_frame_free(&item.frame);
        }

        VideoFrameItem item;
        item.frame=cloneFrame;
        item.timestampUs=timestampUs;
        _queue.push_back(item);
    }
    _cond.notify_one();
    return true;

}

bool VideoFrameQueue::pop(VideoFrameItem &item)
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

void VideoFrameQueue::reset()
{
    std::lock_guard<std::mutex> lock(_mutex);

    clearLocker();
    _stopped = false;
}

void VideoFrameQueue::stop()
{

    {
        std::lock_guard<std::mutex> lock(_mutex);

        _stopped=true;
        clearLocker();
    }


     _cond.notify_all();
}

void VideoFrameQueue::clearLocker()
{

    while(!_queue.empty()){
        VideoFrameItem item=_queue.front();
        av_frame_free(&item.frame);
        _queue.pop_front();
    }


}
