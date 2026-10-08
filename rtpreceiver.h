#ifndef RTPRECEIVER_H
#define RTPRECEIVER_H

#include <QObject>
#include<QUdpSocket>
#include<QByteArray>

class RtpReceiver:public QObject
{
    Q_OBJECT
public:
    explicit RtpReceiver(QObject* parent=nullptr);
    ~RtpReceiver()override;

    bool start(qint16 port);
    void stop();
    bool isRunning()const;

private:
    void handleDatagram(const QByteArray& datagram);
    void handleH264Payload(const uint8_t* payload,int payloadSize,quint16 sequence,quint32 timestamp,bool market);
    void handleSingleNalu(const uint8_t* payload,int payloadSize,quint32 timestamp,bool market);
    void handleFuA(const uint8_t* payload,int payloadSize,quint16 sequence,quint32 timestamp,bool market);
    void resetFuA();

private:
    QUdpSocket _socket;
    bool _running=false;
    //FU-A重组状态
    QByteArray _fuBuffer;
    bool _assemblingFu=false;
    quint32 _fuTimestamp=0;
    quint16 _expectedFuSequence=0;

signals:
    void h264NaluReady(QByteArray nalu,quint32 timestamp,bool market);

private slots:
    void onReadyRead();
};

#endif // RTPRECEIVER_H
