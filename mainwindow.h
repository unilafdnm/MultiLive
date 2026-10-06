#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include<QTimer>
#include<QElapsedTimer>
#include<vector>
#include<string>
#include<cstdint>

#include <QMainWindow>
#include <QDebug>
#include"liveclock.h"
#include"StreamConfig.h"
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
    LiveClock _liveClock;
    ScreenCapture* _screencapture;
    LiveState _liveState;

    QTimer _statsTimer;
    QElapsedTimer _statsElapsedTimer;
    std::vector<PublisherStatesSample> _previousPublisherStats;

private slots:
    void on_stopButton_clicked();

};
#endif // MAINWINDOW_H
