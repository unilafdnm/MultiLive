#ifndef AUDIOENCODERTHREAD_H
#define AUDIOENCODERTHREAD_H
#include"audioframequeue.h"
#include"StreamConfig.h"
#include<thread>
#include<atomic>

extern "C"{

}

class MultiPublisher;


class AudioEncoderThread
{
public:
    AudioEncoderThread();
    ~AudioEncoderThread();
    bool start(const StreamConfig& config,MultiPublisher* publisher);
    void stop();
    bool submit(const AVFrame* frame,int64_t timestampUs);

private:
    void workerLoop();


private:
    AudioFrameQueue _queue;
    std::thread _thread;
    std::atomic<bool> _running;
    StreamConfig _config;
    MultiPublisher* _publisher;

};

#endif // AUDIOENCODERTHREAD_H
