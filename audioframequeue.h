#ifndef AUDIOFRAMEQUEUE_H
#define AUDIOFRAMEQUEUE_H

#include<queue>
#include<mutex>
#include<condition_variable>

extern "C"{
#include<libavutil/frame.h>
}

struct AudioFrameItem{
    AVFrame* frame=nullptr;
    int64_t timestampUs=0;
};

class AudioFrameQueue
{
public:
    AudioFrameQueue(size_t maxSize=32);
    ~AudioFrameQueue();
    bool push(const AVFrame* frame,int64_t timestampUs);
    bool pop(AudioFrameItem& item);

    void stop();
    void reset();


private:
    void clearLocker();




private:
    std::deque<AudioFrameItem> _queue;
    std::mutex _mutex;
    std::condition_variable _cond;
    size_t _maxSize;
    bool _stopped;

};

#endif // AUDIOFRAMEQUEUE_H
