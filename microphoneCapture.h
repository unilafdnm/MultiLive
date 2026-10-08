#ifndef MICROPHONE_H
#define MICROPHONE_H

#include<QObject>
#include<atomic>
#include<thread>
#include<string>
#include"StreamConfig.h"
#include"audioencoderthread.h"
class EncodePacketSink;
class LiveClock;

class MicrophoneCapture:public QObject
{
    Q_OBJECT
public:
    MicrophoneCapture(QObject* parent=nullptr);
    ~MicrophoneCapture();

    void start(const std::string& microphoneName,const StreamConfig& config);
    void stop();
    void setPacketSink(EncodePacketSink* sink);
    void setClock(LiveClock* clock);

private:
    void captureLoop(std::string microphoneName,const StreamConfig& config);
    static int interruptCallback(void* opaque);

private:
    std::atomic<bool> _running;
    std::thread _captureThread;
    EncodePacketSink* _sink;
    LiveClock* _clock;
    AudioEncoderThread _encoderThread;

signals:
    void captureFailed(const QString& reason);

};


#endif // MICROPHONE_H
