#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H

#include <QObject>
#include<thread>
#include<atomic>
#include"StreamConfig.h"
#include"videoencoderthread.h"
class LiveClock;
class EncodePacketSink;


class ScreenCapture : public QObject
{
    Q_OBJECT
public:
    explicit ScreenCapture(QObject *parent = nullptr);
    ~ScreenCapture();
    void start(const StreamConfig& config);
    void stop();
    void setClock(LiveClock* clock);

    void setPacketSink(EncodePacketSink* sink);
    bool getRunning();


private:
    void captureLoop(const StreamConfig& config);
    static int interruptCallback(void* opaque);

private:
    std::atomic<bool> _running;
    std::thread _captureThread;
    EncodePacketSink* _sink;
    LiveClock* _clock;
    VideoEncoderThread _encoderThread;

signals:
    void frameReady(const QImage& image);
    void captureFailed(const QString& reason);
};

#endif // SCREENCAPTURE_H
