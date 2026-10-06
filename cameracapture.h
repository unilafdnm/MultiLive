#ifndef CAMERACAPTURE_H
#define CAMERACAPTURE_H
#include <atomic>
#include <thread>
#include <string>
#include<QImage>
#include<QObject>
#include"StreamConfig.h"
#include"videoencoderthread.h"
class MultiPublisher;
class LiveClock;
class CameraCapture:public QObject
{
    Q_OBJECT
public:
    CameraCapture(QObject* parent=nullptr);
    ~CameraCapture();
    void start(const std::string& cameraName,const StreamConfig& config);
    void stop();
    void setPublisher(MultiPublisher* publisher);
    void setClock(LiveClock* clock);
    bool getRunning();

private:
    void captureLoop(std::string cameraName,const StreamConfig& config);
    static int interruptCallback(void* opaque);


private:
    std::atomic<bool> _running;
    std::thread _captureThread;
    MultiPublisher* _publisher;
    LiveClock* _clock;
    VideoEncoderThread _encoderThread;
signals:
    void frameReady(const QImage& image);
    void captureFailed(const QString& reason);
};

#endif // CAMERACAPTURE_H
