#include "rtpjitterbuffer.h"
#include<QDebug>
#include<QTimer>
#include<algorithm>

Rtpjitterbuffer::Rtpjitterbuffer(QObject* parent)
{

    _clock.start();

}

void Rtpjitterbuffer::push(quint16 sequence, const QByteArray &data)
{
    qint64 extended;

    if(!_initialized){
        _starting=true;
        _startupUntilMs=_clock.elapsed()+_maxWaitMs;
        _nextSequence=sequence;
        _hightSequence=sequence;
        extended=sequence;
        _initialized=true;
    }else{
        extended=extendSequence(sequence);
        if(extended > _hightSequence){
            _hightSequence=extended;
        }
    }

    if(_starting){
        _nextSequence=std::min(_nextSequence,static_cast<qint64>(extended));
    }

    if(extended < _nextSequence && !_starting){
        return;
    }

    auto it=_packets.find(extended);
    if(it!=_packets.end()){
        return;
    }
    BufferPacket packet;
    packet.data=data;
    packet.arriveMs=_clock.elapsed();

    _packets.insert({extended,packet});

    drain();

}

void Rtpjitterbuffer::setPacketCallback(Rtpjitterbuffer::PacketCallback callback)
{
    _packetCallback=std::move(callback);

}

void Rtpjitterbuffer::poll()
{

    drain();

}

void Rtpjitterbuffer::reset()
{
    _packets.clear();
    _nextSequence=0;
    _gapSinceMs=-1;
    _initialized=false;
    _hightSequence=0;
    _starting=false;
    _startupUntilMs=0;

}

void Rtpjitterbuffer::drain()
{

    if(_starting){
        if(_clock.elapsed() < _startupUntilMs && _packets.size() < _maxPackets){
            return;
        }else{
            _starting=false;
        }
    }


    while(!_packets.empty()){
        auto it=_packets.find(_nextSequence);
        if(it == _packets.end()){
            auto first=_packets.begin();
            if(_gapSinceMs == -1){
                _gapSinceMs=first->second.arriveMs;
            }
            qint64 waitMs=_clock.elapsed()-_gapSinceMs;

            if(waitMs<_maxWaitMs && _packets.size()<_maxPackets){
                break;
            }
            _nextSequence=first->first;
            continue;
        }
        qDebug()<<"sequence:"<<it->first<<" data="<<it->second.data<<'\n';
        QByteArray data = it->second.data; // 先保存数据

        _packets.erase(it);
        _nextSequence++;
        _gapSinceMs=-1;

        if(_packetCallback){
            _packetCallback(data);
        }

    }

}

qint64 Rtpjitterbuffer::extendSequence(quint16 sequence) const
{

    quint16 reference=static_cast<quint16>(_hightSequence);
    int delta=static_cast<int>(sequence)-static_cast<int>(reference);

    if(delta>32767){
        delta-=65536;
    }else if(delta<-32768){
        delta+=65536;
    }
    return _hightSequence+delta;


}
