#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include<QTimer>
#include<QElapsedTimer>
#include<vector>
#include<string>
#include<cstdint>
#include<queue>
#include <QMainWindow>
#include <QDebug>
#include"liveclock.h"
#include"StreamConfig.h"
#include "realtimesession.h"
#include"rtpreceiver.h"
#include"h264decoder.h"
#include "audiortpreceiver.h"
#include "aacdecoder.h"
#include "audioplayer.h"

extern "C"
{
#include <libavutil/avutil.h>
}

class CameraCapture;
class MicrophoneCapture;
class MultiPublisher;
class ScreenCapture;

enum class LiveState{
    Idle,
    Starting,
    Live,
    Stopping,
    Error

};

struct SyncVideoFrame{
    QImage image;
    int64_t ptsUs{0};
};



QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    StreamConfig getStreamConfig();
    void setLiveState(LiveState);
    void updateStateTable();
    void resetRealtimeSync();

private:
    struct PublisherStatesSample{
        std::string url;
        uint64_t totalBytesSend=0;
        uint64_t videoPacketSent=0;
        bool vaild=false;
    };

    Ui::MainWindow *ui;
    CameraCapture* _cameracapture;
    MicrophoneCapture* _microphonecapture;
    MultiPublisher* _publishers;
    realtimesession _realtimeSession;
    LiveClock _liveClock;
    ScreenCapture* _screencapture;
    LiveState _liveState;

    //RTP视频
    RtpReceiver _rtpReceiver;
    H264Decoder _h264Decoder;

    //RTP音频
    AudioRtpReceiver _audioRtpReceiver;
    AacDecoder _aacDecoder;
    AudioPlayer _audioPlayer;

    //音视频同步
    std::deque<SyncVideoFrame> _videoSyncQueue;
    QTimer _avSyncTimer;


    quint32 _videoRtpTimestampBase{0};
    quint32 _audioRtpTimestampBase{0};

    bool _syncSessionActive{false};

    QTimer _statsTimer;
    QElapsedTimer _statsElapsedTimer;
    std::vector<PublisherStatesSample> _previousPublisherStats;

private slots:
    void on_stopButton_clicked();

};
#endif // MAINWINDOW_H
