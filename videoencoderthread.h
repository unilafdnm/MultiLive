#ifndef VIDEOENCODERTHREAD_H
#define VIDEOENCODERTHREAD_H

#include"StreamConfig.h"
#include"videoframequeue.h"
#include<atomic>
#include<thread>


class MultiPublisher;


class VideoEncoderThread
{
public:
    VideoEncoderThread();
    ~VideoEncoderThread();
    bool start(const StreamConfig& config,MultiPublisher* publisher);

    bool submit(const AVFrame* frame,int64_t timestampUs);
    void stop();

private:
    void workerLoop();

private:
    std::atomic<bool> _running;
    std::thread _thread;
    VideoFrameQueue _queue;
    StreamConfig _config;
    MultiPublisher* _publisher;
};

#endif // VIDEOENCODERTHREAD_H
