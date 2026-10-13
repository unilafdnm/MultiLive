#include "mainwindow.h"
#include "ui_mainwindow.h"
#include"cameracapture.h"
#include"microphoneCapture.h"
#include"multipublisher.h"
#include"screencapture.h"
#include"outputtypes.h"
#include"outputtargetparser.h"

#include<QCoreApplication>
#include<QMessageBox>
#include<QComboBox>
#include <QDebug>
#include<QLineEdit>
#include<string>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QUrl>
#include<QCameraInfo>
#include<QAudioDeviceInfo>
#include<QAudio>
#include<QListWidgetItem>
#include<QListWidget>

extern "C"
{
#include <libavutil/avutil.h>
}

namespace  {
QString connectionStateText(PublisherConnectionState state){

    switch (state) {
    case PublisherConnectionState::Idle:
        return QStringLiteral("空闲");
    case PublisherConnectionState::Connecting:
        return QStringLiteral("连接中");
    case PublisherConnectionState::Connected:
        return QStringLiteral("已连接");
    case PublisherConnectionState::Reconnecting:
        return QStringLiteral("重连中");
    }
    return QStringLiteral("未知");

}

int64_t rtpTimestampToUs(quint32 timestamp,quint32 timestampBase,int clockRate){
    const qint32 delta=static_cast<qint32>(timestamp-timestampBase);
    return av_rescale_q(delta,AVRational{1,clockRate},AV_TIME_BASE_Q);

}


}



MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    ,_cameracapture(new CameraCapture(this))
    ,_microphonecapture(new MicrophoneCapture(this))
    ,_publishers(new MultiPublisher())
    ,_screencapture(new ScreenCapture(this))
    ,_realtimeSession(_publishers)
    ,_h264Decoder()
    ,_liveState(LiveState::Idle)
{

    ui->setupUi(this);

    const QStringList args=QCoreApplication::arguments();
    _lanReceiverMode=args.contains("--lan-recv");
    const QString prefix="--lan-send=";

    for(const QString& arg:args){
        if(arg.startsWith(prefix)){
            _lanRemoteIp=arg.mid(prefix.size()).trimmed();
        }
    }


    _cameracapture->setPacketSink(
        &_realtimeSession
    );

    _screencapture->setPacketSink(
        &_realtimeSession
    );

    _microphonecapture->setPacketSink(
        &_realtimeSession
    );





    if(!_h264Decoder.open()){
        qDebug()<<"open H264 decoder failed";
    }
    if (!_aacDecoder.open(48000, 2)) {
        qWarning() << "open AAC decoder failed";
    }

    if (!_audioPlayer.open(48000, 2)) {
        qWarning() << "open AudioPlayer failed";
    }

    _audioRtpReceiver.setAacCallback([this](std::vector<uint8_t> aac,uint32_t timestamp){

        if(!_syncSessionActive){
            return;
        }

        _aacDecoder.decode(aac.data(),aac.size(),timestamp);
    });
    _aacDecoder.setPcmCallback([this](std::vector<uint8_t> pcm,uint32_t timestamp){
        if(!_syncSessionActive){
            return ;
        }

        const int64_t audioPtsUs=rtpTimestampToUs(timestamp,_audioRtpTimestampBase,48000);
        _audioPlayer.player(pcm.data(),pcm.size(),audioPtsUs);

    });


    if(_lanRemoteIp.isEmpty()){
        if (!_audioRtpReceiver.open(5006)) {
            qWarning()<< "start Audio RTP receiver failed";
        }
    }




    connect(&_rtpReceiver,&RtpReceiver::h264NaluReady,this,
        [this](QByteArray nalu,quint32 timestamp,bool marker){
            if(!_syncSessionActive){
                return ;
            }
            if (nalu.size() < 5) {
                return;
            }
              _h264Decoder.pushNalu(nalu,timestamp,marker);

        }
    );

    connect(&_h264Decoder,&H264Decoder::frameReady,this,[this](const QImage& image,quint32 timestamp){

        if(!_syncSessionActive){
            return ;
        }

        const int64_t videoPtsUs=rtpTimestampToUs(timestamp,_videoRtpTimestampBase,90000);


        constexpr size_t MAX_VIDEO_QUEUE=5;

        while(_videoSyncQueue.size() >= MAX_VIDEO_QUEUE){
            _videoSyncQueue.pop_front();
//            qDebug() << "drop old video frame,"
//                     << "queue ="
//                     << _videoSyncQueue.size();
        }
        _videoSyncQueue.push_back({image,videoPtsUs});

    });
    _avSyncTimer.setInterval(10);


    connect(&_avSyncTimer,&QTimer::timeout,this,[this](){
        if(!_syncSessionActive){
            return ;
        }




        //这里的问题
       if(_videoSyncQueue.empty()){
           return ;
       }
       if(!_audioPlayer.started()){
           return;
       }


       SyncVideoFrame& frame=_videoSyncQueue.front();

       const int64_t videoUs=frame.ptsUs;

       int64_t audioUs=_audioPlayer.playedUs();
       int64_t diffUs=videoUs-audioUs;

       static int logCounter = 0;

       if (++logCounter >= 100) {
           logCounter = 0;

           qDebug()
               << "videoUs =" << videoUs
               << "audioUs =" << audioUs
               << "diffMs =" << diffUs / 1000.0
               << "videoQueue =" << _videoSyncQueue.size()
               << "audioQueueBytes =" << _audioPlayer.queueBytes();
       }

       //视频比音频早30ms,暂时不显示
       if(diffUs>30000){
//           qDebug() << "video early, wait:"<< diffUs / 1000.0 << "ms";
           return;
       }
       /*
         视频已经落后超过 100ms
         → 这帧没价值了
        */
       if(diffUs<-100000){
          // qDebug()<<"drop late video frame diff="<<diffUs/1000.0<<"ms";
           _videoSyncQueue.pop_front();
           return;
       }
       //qDebug() << "display video:"<< diffUs / 1000.0 << "ms";
       ui->previewLabel1->setPixmap(
                   QPixmap::fromImage(
                       frame.image
                   ).scaled(
                       ui->previewLabel1->size(),
                       Qt::KeepAspectRatio,
                       Qt::SmoothTransformation
                   )
               );

               _videoSyncQueue.pop_front();

    });



    if(_lanRemoteIp.isEmpty()){
        if (!_rtpReceiver.start(5004)) {
            qDebug()<< "start RTP receiver failed";
        }
    }




    //枚举添加摄像头
    const QList<QCameraInfo> cameras=QCameraInfo::availableCameras();
    const QCameraInfo defaultCamera=QCameraInfo::defaultCamera();
    int defaultCameraIndex=-1;

    for(const QCameraInfo& camera:cameras){
        const QString displayName=camera.description();
        ui->camerDeviceComboBox->addItem(displayName,displayName);


        if(camera.deviceName() == defaultCamera.deviceName()){
            defaultCameraIndex=ui->camerDeviceComboBox->count()-1;
        }
    }

    if(defaultCameraIndex >=0){
        ui->camerDeviceComboBox->setCurrentIndex(defaultCameraIndex);
    }
    //枚举添加麦克风
    const QList<QAudioDeviceInfo> microphones=QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    const QAudioDeviceInfo defaultMicrophoneName=QAudioDeviceInfo::defaultInputDevice();
    int defaultMicrophoneIndex=-1;

    for(const QAudioDeviceInfo& microphone:microphones){
        const QString display=microphone.deviceName();
        ui->microphoneDeviceComboBox->addItem(display,display);
        if(display == defaultMicrophoneName.deviceName()){
            defaultMicrophoneIndex=ui->microphoneDeviceComboBox->count()-1;
        }
    }
    if(defaultMicrophoneIndex >=0){
        ui->microphoneDeviceComboBox->setCurrentIndex(defaultMicrophoneIndex);
    }



    ui->statsTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    _statsTimer.setInterval(1000);
    connect(&_statsTimer,&QTimer::timeout,this,&MainWindow::updateStateTable);
    _statsTimer.start();
    _statsElapsedTimer.start();

    connect(_publishers,&MultiPublisher::connected,this,[this](){
        if(_liveState != LiveState::Starting){
            return;
        }
        setLiveState(LiveState::Live);
    });

    connect(_publishers,&MultiPublisher::connectionFailed,this,[this](){
        if(_liveState != LiveState::Starting){
            return ;
        }

        setLiveState(LiveState::Stopping);
        // 完整关闭当前推流会话
               _cameracapture->stop();
               _screencapture->stop();
               _microphonecapture->stop();
               _publishers->close();

               setLiveState(LiveState::Error);

               QMessageBox::warning(
                   this,
                   "推流失败",
                   "无法连接到输出服务器"
               );

    });


    connect(ui->screenButton,&QPushButton::clicked,this,[this](){

        _liveClock.start();
        _screencapture->setClock(&_liveClock);
        _screencapture->start(getStreamConfig());

    });

    connect(_screencapture,&ScreenCapture::frameReady,this,[this](const QImage& image){
        ui->previewLabel->setPixmap(
            QPixmap::fromImage(image).scaled(ui->previewLabel->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation)
        );
    });

    connect(ui->startButton,&QPushButton::clicked,this,[this](){
        if (_liveState != LiveState::Idle && _liveState != LiveState::Error) {
            return;
        }
        _cameracapture->stop();
        _screencapture->stop();
        _microphonecapture->stop();
        _realtimeSession.stopRtp();
        resetRealtimeSync();
        _publishers->close();

        bool outputAdded=false;



        if(!_lanRemoteIp.isEmpty()){

            setLiveState(LiveState::Starting);
            const std::string ip=_lanRemoteIp.toStdString();
            const bool videoOk=_realtimeSession.startVideoRtp(ip,5004);
            const bool audioOk=_realtimeSession.startAudioRtp(ip,5006);

            if(!videoOk || !audioOk){
                _realtimeSession.stopRtp();
                setLiveState(LiveState::Error);
                QMessageBox::warning(this,"LAN","RTP Sender启动失败");
                return;
            }
            LanSession session;
            session.videoSsrc=_realtimeSession.videoRtpSsrc();
            session.videoBase=_realtimeSession.videoRtpTimestampBase();
            session.audioSsrc=_realtimeSession.audioRtpSsrc();
            session.audioBase=_realtimeSession.audioRtpTimestampBase();
            _lanControl.connectToPeer(_lanRemoteIp,session,6000);
            return;
        }





        for(int row=0;row<ui->outputListWidget->count();row++){
            QListWidgetItem* item=ui->outputListWidget->item(row);
            if(!item){
                continue;
            }

            const QString address=item->text().trimmed();

            const OutputTargetParseResult result=parseOutputTarget(address);

            if(!result.target.has_value()){
                qWarning()<<"invalid output target:"<<result.error;
                continue;
            }

            if(_publishers->addOutput(*result.target)){
                outputAdded=true;
            }

        }

        if(!outputAdded){
            _publishers->close();
            QMessageBox::warning(this,"推流失败","没有可用的推流地址");
            return;
        }

        setLiveState(LiveState::Starting);

        const bool videoRtpStarted=_realtimeSession.startVideoRtp("127.0.0.1",5004);
        const bool audioRtpStarted=_realtimeSession.startAudioRtp("127.0.0.1",5006);

        if(!videoRtpStarted ||!audioRtpStarted){
            _realtimeSession.stopRtp();
            resetRealtimeSync();
            _publishers->close();
            QMessageBox::warning(this,"启动失败","无法启动本地RTP预览");
            return;
        }
        _videoRtpTimestampBase=_realtimeSession.videoRtpTimestampBase();
        _audioRtpTimestampBase=_realtimeSession.audioRtpTimestampBase();
        _rtpReceiver.setExpectedSsrc(_realtimeSession.videoRtpSsrc());
        _audioRtpReceiver.setExpectedSsrc(_realtimeSession.audioRtpSsrc());


        _liveClock.start();
        _syncSessionActive = true;
        _avSyncTimer.start();




    });


    connect(ui->cameraButton,&QPushButton::clicked,this,[this](){
        const QString cameraName =
            ui->camerDeviceComboBox->currentData().toString();

        if (cameraName.isEmpty()) {
            QMessageBox::warning(
                this,
                QStringLiteral("打开摄像头失败"),
                QStringLiteral("没有选择可用的摄像头")
            );
            return;
        }
        _cameracapture->start(cameraName.toStdString(),getStreamConfig());
    });
    connect(_cameracapture,&CameraCapture::frameReady,this,[this](const QImage& image){
       ui->previewLabel->setPixmap(
            QPixmap::fromImage(image).scaled(ui->previewLabel->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation)
        );
    });


    setLiveState(LiveState::Idle);


    auto handleCaptureFailed=[this](const QString& reason){
        if(_liveState != LiveState::Starting && _liveState != LiveState::Live){
            return ;
        }

        setLiveState(LiveState::Stopping);
        _cameracapture->stop();
        _microphonecapture->stop();
        _screencapture->stop();
        _publishers->close();
        setLiveState(LiveState::Error);
        QMessageBox::warning(this,QString("采集失败"),reason);

    };

    connect(ui->addOutputButton,&QPushButton::clicked,this,[this](){

        const QString address=ui->rtmpUrlEdit->text();

        OutputTargetParseResult result=parseOutputTarget(address);

        if(result.error !="OK"){
            QMessageBox::warning(this,"地址无效",result.error);
            return;
        }

        const QList<QListWidgetItem*> duplicates=ui->outputListWidget->findItems(address,Qt::MatchExactly);
        if(!duplicates.isEmpty()){
            QMessageBox::warning(this,QStringLiteral("地址已存在"),QStringLiteral("该推流地址已经在输出列表中"));

            return;
        }
        ui->outputListWidget->addItem(address);
        ui->rtmpUrlEdit->clear();



    });

    connect(ui->rtmpUrlEdit,&QLineEdit::returnPressed,ui->addOutputButton,&QPushButton::click);

    connect(ui->removeOutputButton,&QPushButton::clicked,this,[this](){
        const int row=ui->outputListWidget->currentRow();
        if(row<0){
            return;
        }
        delete ui->outputListWidget->takeItem(row);
    });

    connect(
        _cameracapture,
        &CameraCapture::captureFailed,
        this,
        handleCaptureFailed
    );

    connect(
        _screencapture,
        &ScreenCapture::captureFailed,
        this,
        handleCaptureFailed
    );

    connect(
        _microphonecapture,
        &MicrophoneCapture::captureFailed,
        this,
        handleCaptureFailed
    );

    setupLanControl();
}

MainWindow::~MainWindow()
{
    _microphonecapture->stop();
    _cameracapture->stop();
    _screencapture->stop();
    _realtimeSession.stopRtp();
    resetRealtimeSync();
    _publishers->close();

    delete _publishers;
    _publishers=nullptr;

    delete ui;
}

StreamConfig MainWindow::getStreamConfig()
{
    StreamConfig config;
    //分辨率
    QString resolution=ui->resolutionComboBox->currentText();
    QStringList size=resolution.split("x");
    if(size.size() == 2){
        config.width=size[0].toInt();
        config.height=size[1].toInt();
    }

    //FPS

    config.fps=ui->fpsComboBox->currentText().toInt();

    //视频码率
    QString videoBitrate=ui->videoBitrateComboBox->currentText();
    QString qianmian=videoBitrate.chopped(4);
    config.videoBitrate=qianmian.toInt()*1000;

    //音频码率
    QString audioBitrate=ui->audioBitrateComboBox->currentText();
    qianmian=audioBitrate.chopped(4);
    config.audioBitrate=qianmian.toInt()*1000;


    config.preset=ui->presetComboBox->currentText().toStdString();
    return config;

}

void MainWindow::setLiveState(LiveState state)
{
    _liveState=state;
    const bool editable= state==LiveState::Idle || state==LiveState::Error;
    const bool canStop= state==LiveState::Starting || state==LiveState::Live;

    ui->sourceComboBox->setEnabled(editable);
       ui->cameraButton->setEnabled(editable);
       ui->screenButton->setEnabled(editable);
       // 编码参数
       ui->resolutionComboBox->setEnabled(editable);
       ui->fpsComboBox->setEnabled(editable);
       ui->videoBitrateComboBox->setEnabled(editable);
       ui->audioBitrateComboBox->setEnabled(editable);

       // 会话按钮
       ui->startButton->setEnabled(editable);
       ui->stopButton->setEnabled(canStop);

       ui->presetComboBox->setEnabled(editable);
       ui->camerDeviceComboBox->setEnabled(editable);
       ui->microphoneDeviceComboBox->setEnabled(editable);

       ui->rtmpUrlEdit->setEnabled(editable);
       ui->addOutputButton->setEnabled(editable);
       ui->removeOutputButton->setEnabled(editable);
       ui->outputListWidget->setEnabled(editable);

}

void MainWindow::updateStateTable()
{
    const qint64 elapsedMs=_statsElapsedTimer.restart();

    if(elapsedMs <= 0){
        return;
    }
    const double elapseSecond=static_cast<double>(elapsedMs)/1000.0;

    const auto snapshots=_publishers->stats();
    ui->statsTableWidget->setRowCount(static_cast<int>(snapshots.size()));

    if(_previousPublisherStats.size() != snapshots.size()){
       _previousPublisherStats.resize(snapshots.size());
    }

    for(std::size_t index=0;index<snapshots.size();index++){

        const auto& current=snapshots[index];
        auto& previous=_previousPublisherStats[index];
        uint64_t bytesDelta=0;
        uint64_t videoPacketDelta=0;
        const bool sameOutput=previous.vaild && previous.url ==current.url;

        if(sameOutput && current.totalBytesSent>= previous.totalBytesSend){
            bytesDelta=current.totalBytesSent-previous.totalBytesSend;
        }
        if(sameOutput && current.videoPacketSent >= previous.videoPacketSent){
            videoPacketDelta=current.videoPacketSent - previous.videoPacketSent;
        }

        const double bitrateKbps=static_cast<double>(bytesDelta)*8.0/elapseSecond/1000;
        const double outputFps=static_cast<double>(videoPacketDelta)/elapseSecond;

        QUrl outputUrl(QString::fromStdString(current.url));

        QString outputName=outputUrl.host();

        if(outputUrl.port()>0){
            outputName+=QStringLiteral(":") + QString::number(outputUrl.port());
        }
        if(outputName.isEmpty()){
            outputName=QStringLiteral("输出 %1").arg(index+1);
        }

        const int row=static_cast<int>(index);

        QString avsyncText=QStringLiteral("--");

        if(current.state == PublisherConnectionState::Connected && current.lastVideoPtsUs !=AV_NOPTS_VALUE
                && current.lastAudioPtsUs != AV_NOPTS_VALUE){
            const int64_t diffUs=current.lastVideoPtsUs- current.lastAudioPtsUs;
            const double diffMs=static_cast<double>(diffUs)/1000.0;
            avsyncText=QString::number(diffMs,'f',1)+QStringLiteral("ms");
        }


        auto setCell=[this,row](int column,const QString& text){
          QTableWidgetItem* item=ui->statsTableWidget->item(row,column);
          if(!item){
              item=new QTableWidgetItem();
              item->setTextAlignment(Qt::AlignCenter);
              ui->statsTableWidget->setItem(row,column,item);
          }
          item->setText(text);

        };

        setCell(0,outputName);
        setCell(1,connectionStateText(current.state));
        setCell(2,QString::number(bitrateKbps,'f',1)+QStringLiteral(" kbps"));
        setCell(3,QString::number(outputFps,'f',1));
        setCell(4,QString::number(static_cast<qulonglong>(current.queueSize)));
        setCell(5,QString::number(static_cast<qulonglong>(current.droppedPackets)));
        setCell(6,avsyncText);
        previous.url=current.url;
        previous.vaild=true;
        previous.totalBytesSend=current.totalBytesSent;
        previous.videoPacketSent=current.videoPacketSent;

    }
    if(snapshots.empty()){
        _previousPublisherStats.clear();
    }

}

bool MainWindow::resetRealtimeSync()
{
    //先禁止继续处理媒体数据
    _syncSessionActive=false;
    _avSyncTimer.stop();

    //取消旧RTP会话过滤参数
    _rtpReceiver.setExpectedSsrc(0);
    _audioRtpReceiver.setExpectedSsrc(0);

    //清空网络接收缓冲
    _rtpReceiver.clearPending();
    _audioRtpReceiver.clearPending();

    //清空应用层同步与播放缓存
    _videoSyncQueue.clear();
    _audioPlayer.reset();

    //清空编码器相关状态
    const bool videoOk=_h264Decoder.reset();
    const bool audioOk=_aacDecoder.reset();

    //清除上一个会话时间基准
    _videoRtpTimestampBase=0;
    _audioRtpTimestampBase=0;

    ui->previewLabel1->clear();
    return videoOk&&audioOk;

}

void MainWindow::setupLanControl()
{
    //B:接收远程会话参数
    connect(&_lanControl,&LanControl::sessionReceived,this,[this](LanSession s){
        beginLanReceiveSession(s);
    });

    //A:收到READY，开始真正采集
    connect(&_lanControl,&LanControl::senderReady,this,[this](){

        if(_lanRemoteIp.isEmpty() || _liveState != LiveState::Starting){
            return;
        }
        startMediaCapture();
        setLiveState(LiveState::Live);

    });
    //TCP断开，结束本次媒体对话
    connect(&_lanControl,&LanControl::peerClosed,this,[this](){

        if(_lanReceiverMode){
            endLanReceiveSession();
            return;
        }
        if(!_lanRemoteIp.isEmpty() && (_liveState==LiveState::Starting || _liveState==LiveState::Live)){
            on_stopButton_clicked();
        }


    });

    connect(&_lanControl,&LanControl::failed,this,[this](const QString& reason){
       qWarning()<<"LAN error"<<reason;
       if(!_lanRemoteIp.isEmpty() && (_liveState==LiveState::Starting || _liveState==LiveState::Live)){
           on_stopButton_clicked();
       }
       QMessageBox::warning(this,"局域网通信失败",reason);

    });

    if(_lanReceiverMode){

        if(!_lanControl.listen(6000)){
            QMessageBox::warning(this,"TCP监听失败",_lanControl.serverError());
        }

        ui->startButton->setEnabled(false);
        ui->stopButton->setEnabled(false);

    }



}

void MainWindow::startMediaCapture()
{
    const StreamConfig config=getStreamConfig();
    const QString source=ui->sourceComboBox->currentText();
    const QString camera=ui->camerDeviceComboBox->currentText();
    const QString microphone=ui->microphoneDeviceComboBox->currentText();

    _liveClock.start();

    if(source == "摄像头"){
        _screencapture->stop();
        _cameracapture->setClock(&_liveClock);
        _cameracapture->start(camera.toStdString(),config);
    }else{
        _cameracapture->stop();
        _screencapture->setClock(&_liveClock);
        _screencapture->start(config);
    }
    _microphonecapture->setClock(&_liveClock);
    _microphonecapture->start(microphone.toStdString(),config);

}

void MainWindow::beginLanReceiveSession(const LanSession &session)
{
    if(!_lanReceiverMode || _lanReceiveState != LanReceiveState::Waiting){
        _lanControl.rejectSession();
        return;
    }

    if(!resetRealtimeSync()){
        qWarning()<<"Decoder reset failed";
        _lanControl.rejectSession();
        return;
    }

    _videoRtpTimestampBase=session.videoBase;
    _audioRtpTimestampBase=session.audioBase;
    _rtpReceiver.setExpectedSsrc(session.videoSsrc);

    _audioRtpReceiver.setExpectedSsrc(session.audioSsrc);

    //开始处理新的媒体数据
    _lanReceiveState=LanReceiveState::Receiving;
    _syncSessionActive=true;
    _avSyncTimer.start();

    _lanControl.replyReady();

    qDebug()<<"LAN session receiving";

}

void MainWindow::endLanReceiveSession()
{
    if(!_lanReceiverMode){
        return;
    }
    resetRealtimeSync();
    _lanReceiveState=LanReceiveState::Waiting;
    qDebug()<<"LAN session waiting";

}


void MainWindow::on_stopButton_clicked()
{
    if (_liveState == LiveState::Idle || _liveState == LiveState::Stopping) {
        return;
    }
    setLiveState(LiveState::Stopping);


    _cameracapture->stop();
    _screencapture->stop();
    _microphonecapture->stop();
    resetRealtimeSync();
    _realtimeSession.stopRtp();
    _lanControl.stopClient();
    _publishers->close();

    setLiveState(LiveState::Idle);
}
