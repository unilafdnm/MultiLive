#include "mainwindow.h"
#include "ui_mainwindow.h"
#include"cameracapture.h"
#include"microphoneCapture.h"
#include"multipublisher.h"
#include"screencapture.h"
#include"outputtypes.h"
#include"outputtargetparser.h"

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

    _cameracapture->setPacketSink(
        &_realtimeSession
    );

    _screencapture->setPacketSink(
        &_realtimeSession
    );

    _microphonecapture->setPacketSink(
        &_realtimeSession
    );
    _realtimeSession.startVideoRtp(
        "127.0.0.1",
        5004
    );
    if(!_h264Decoder.open()){
        qDebug()<<"open H264 decoder failed";
    }


    connect(&_rtpReceiver,&RtpReceiver::h264NaluReady,this,
        [this](QByteArray nalu,quint32 timestamp,bool marker){
            if (nalu.size() < 5) {
                return;
            }
//            uint8_t header =static_cast<uint8_t>(nalu[4]);
//            int type =header & 0x1F;
//            qDebug()<< "Receive complete NALU:"<< "type="<< type<< "size="<< nalu.size()<< "timestamp="<< timestamp<< "marker="<< marker;
              _h264Decoder.pushNalu(nalu,timestamp,marker);

        }
    );

    connect(&_h264Decoder,&H264Decoder::frameReady,this,[this](const QImage& image){
        ui->previewLabel->setPixmap(
            QPixmap::fromImage(image).scaled(ui->previewLabel->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation)
        );
    });

    if (!_rtpReceiver.start(5004)) {
        qDebug()
            << "start RTP receiver failed";
    }

    _realtimeSession.startVideoRtp(
        "127.0.0.1",
        5004
    );

    ui->setupUi(this);
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

        bool outputAdded=false;
        _publishers->close();



        QString _selectdevice=ui->sourceComboBox->currentText();
        const QString selectCamera=ui->camerDeviceComboBox->currentText();
        const QString selectMicrophone=ui->microphoneDeviceComboBox->currentText();

        if(_selectdevice == "摄像头" &&selectCamera.isEmpty()){
            QMessageBox::warning(this,"打开摄像头失败","没有选择可用的摄像头");
            return;
        }
        if(selectMicrophone.isEmpty()){
            QMessageBox::warning(this,"打开麦克风失败","没有选择可用的麦克风");
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

        if(_selectdevice=="摄像头"){
            if(_screencapture->getRunning()){
                _screencapture->stop();
            }
            _liveClock.start();
            _cameracapture->setClock(&_liveClock);
            _cameracapture->start(selectCamera.toStdString(),getStreamConfig());
        }
        else if(_selectdevice == "屏幕"){
            if(_cameracapture->getRunning()){
                _cameracapture->stop();
            }
            _liveClock.start();
            _screencapture->setClock(&_liveClock);
            _screencapture->start(getStreamConfig());
        }
       _microphonecapture->setClock(&_liveClock);
       _microphonecapture->start(selectMicrophone.toStdString(),getStreamConfig());
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


}

MainWindow::~MainWindow()
{
    _microphonecapture->stop();
    _cameracapture->stop();
    _screencapture->stop();
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


void MainWindow::on_stopButton_clicked()
{
    if (_liveState == LiveState::Idle || _liveState == LiveState::Stopping) {
        return;
    }
    setLiveState(LiveState::Stopping);


    _cameracapture->stop();
    _screencapture->stop();
    _microphonecapture->stop();
    _publishers->close();

    setLiveState(LiveState::Idle);
}
