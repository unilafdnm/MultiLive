#ifndef RTPJITTERBUFFER_H
#define RTPJITTERBUFFER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QtGlobal>
#include<functional>
#include<cstddef>
#include<map>
#include<QObject>

class Rtpjitterbuffer:public QObject
{


public:
    using PacketCallback=std::function<void(const QByteArray&)>;
    Rtpjitterbuffer(QObject* parent=nullptr);

    void push(quint16 sequence,const QByteArray& data);

    void setPacketCallback(PacketCallback callback);
    void poll();
    void reset();


private:
    void drain();

    qint64 extendSequence(quint16 sequence)const;

private:

    struct BufferPacket{
        QByteArray data;
        quint64 arriveMs;
    };

    std::map<qint64,BufferPacket> _packets;
    QElapsedTimer _clock;

    bool _initialized=false;
    qint64 _nextSequence=0;

    PacketCallback _packetCallback;

    qint64 _gapSinceMs=-1;
    qint64 _maxWaitMs=40;
    std::size_t _maxPackets=256;

    bool _starting=false;
    qint64 _startupUntilMs=0;

    qint64 _hightSequence=0;

};

#endif // RTPJITTERBUFFER_H
