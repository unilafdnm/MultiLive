#ifndef VIDEOFRAMEQUEUE_H
#define VIDEOFRAMEQUEUE_H
#include<mutex>
#include<condition_variable>
#include<deque>

extern "C"{
#include<libavutil/frame.h>

}


struct VideoFrameItem{
    AVFrame* frame=nullptr;
    int64_t timestampUs=0;
};


class VideoFrameQueue
{
public:
    VideoFrameQueue(size_t maxSize=4);
    ~VideoFrameQueue();

    bool push(const AVFrame* frame,int64_t timestampUs);
    bool pop(VideoFrameItem& item);

    void reset();
    void stop();




private:
    void clearLocker();

private:
    std::deque<VideoFrameItem> _queue;
    std::mutex _mutex;
    std::condition_variable _cond;

    size_t _maxSize;
    bool _stopped;


};

#endif // VIDEOFRAMEQUEUE_H
