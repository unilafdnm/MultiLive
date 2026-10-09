#include "rtpreceiver.h"

namespace
{

uint16_t readUint16(const uint8_t* data){
    return (static_cast<uint16_t>(data[0]))<<8|static_cast<uint16_t>(data[1]);
}

uint32_t readUint32(const uint8_t* data){
    return (static_cast<uint16_t>(data[0]))<<24|(static_cast<uint16_t>(data[1]))<<16
                |(static_cast<uint16_t>(data[2]))<<8|static_cast<uint16_t>(data[3]);
}


}



RtpReceiver::RtpReceiver(QObject *parent)
    :QObject(parent)
{
    connect(&_socket,&QUdpSocket::readyRead,this,&RtpReceiver::onReadyRead);

}

RtpReceiver::~RtpReceiver()
{
    stop();
}

bool RtpReceiver::start(qint16 port)
{
    if(_running){
        return true;
    }

    bool ret=_socket.bind(QHostAddress::AnyIPv4,port);
    if(!ret){
        qDebug()<<"RTP bind failed:"<<_socket.errorString();
        return false;
    }
    _running=true;
    qDebug()<<"RTP receiver listening:"<<port;
    return true;

}

void RtpReceiver::stop()
{
    if(!_running){
        return;
    }
    _running=false;
    _socket.close();
    resetFuA();
    return;

}

bool RtpReceiver::isRunning() const
{
    return  _running;
}

void RtpReceiver::handleDatagram(const QByteArray &datagram)
{
    constexpr int RTP_FIXED_HEADER_SIZE=12;
    if(datagram.size()<RTP_FIXED_HEADER_SIZE){
        return;
    }

    const uint8_t* data=reinterpret_cast<const uint8_t*>(datagram.constData());
    const int packetSize=datagram.size();

    //Byte 0
    const uint8_t byte0=data[0];
    const uint8_t version=byte0>>6;
    const bool padding=(byte0&0x20)!=0;
    const bool extension=(byte0&0x10)!=0;
    const uint8_t csrcCount=(byte0 & 0x0F);

    if(version!=2){
        qDebug()<<"invalid RTP version:"<<version;
        return;
    }

    //Byte1
    const uint8_t byte1=data[1];
    const bool market=(byte1&0x80)!=0;
    const uint8_t payloadType=(byte1&0x7F);
    if(payloadType!=96){
        return;
    }

    //Sequence Number
    const quint16 sequence=readUint16(data+2);
    const quint32 timestamp=readUint32(data+4);
    const quint32 ssrc=readUint32(data+8);
    Q_UNUSED(ssrc);

    int headerSize=RTP_FIXED_HEADER_SIZE+csrcCount*4;
    if(headerSize > packetSize){
        return;
    }

    //RTP Extension
    if(extension){
        if(headerSize+4 > packetSize){
            return;
        }
        //extension header:16 bit profile、16 bit length
        //length单位是32bit word
        const uint16_t externsionLength=readUint16(data+headerSize+2);
        headerSize+=4+externsionLength*4;
        if(headerSize>packetSize){
            return;
        }
    }

    //RTP padding
    int payloadEnd=packetSize;
    if(padding){
        const uint8_t paddingSize=data[packetSize-1];
        if(paddingSize ==0 || paddingSize >packetSize-headerSize){
            return;
        }
        payloadEnd-=paddingSize;
    }

    const int payloadSize=payloadEnd-headerSize;
    if(payloadSize<=0){
        return;
    }

    const uint8_t* payload=data+headerSize;
    //qDebug()<<"RTP: seq="<<sequence<<" timestamp="<<timestamp<<" market="<<market<<" payload="<<payloadSize;
    handleH264Payload(payload,payloadSize,sequence,timestamp,market);


}

void RtpReceiver::handleH264Payload(const uint8_t *payload, int payloadSize, quint16 sequence, quint32 timestamp, bool market)
{
    if(!payload || payloadSize<=0){
        return;
    }

    const uint8_t naluType=payload[0]&0x1F;

    //Single NAL Uint
    if(naluType>=1 && naluType<=23){
        //如果之前有一个FU-A还没收完，说明它已经不能继续用了
        if(_assemblingFu){
            resetFuA();
        }
        handleSingleNalu(payload,payloadSize,timestamp,market);
        return;
    }

    //FU-A Type=28
    if(naluType==28){
        handleFuA(payload,payloadSize,sequence,timestamp,market);
        return;
    }

    qDebug()<<"unsupported H264 RTP type:"<<naluType;

}

void RtpReceiver::handleSingleNalu(const uint8_t *payload, int payloadSize, quint32 timestamp, bool market)
{
    static const char startCode[]={0x00,0x00,0x00,0x01};
    QByteArray nalu;
    nalu.reserve(4+payloadSize);
    nalu.append(startCode,4);
    nalu.append(reinterpret_cast<const char*>(payload),payloadSize);

    const uint8_t naluType=payload[0]&0x1F;

//    qDebug()<<"H264 Single Nalu: type="<<naluType<<" size="<<nalu.size();
    emit h264NaluReady(nalu,timestamp,market);


}

void RtpReceiver::handleFuA(const uint8_t *payload, int payloadSize, quint16 sequence, quint32 timestamp, bool market)
{
    if(!payload||payloadSize<3){
        return;
    }
    const uint8_t fuIndicator=payload[0];
    const uint8_t fuHeader=payload[1];

    const bool start=(fuHeader&0x80)!=0;
    const bool end=(fuHeader&0x40)!=0;
    const uint8_t naluType=(fuHeader&0x1F);

    //恢复原NALU Header,FU Indicator:F|NRI|28
    //FU Header:S|E|R|Type
    //原始Header:F|NRI|Type
    const uint8_t originalNaluHeader=(fuIndicator&0xE0)|naluType;
    const uint8_t* fragmentData=payload+2;
    const int fragmentSize=payloadSize-2;
    if(start){
        resetFuA();
        _assemblingFu=true;
        _fuTimestamp=timestamp;
        _expectedFuSequence=static_cast<quint16>(sequence+1);
        static const char startCode[]={0x00,0x00,0x00,0x01};
        _fuBuffer.append(startCode,4);
        _fuBuffer.append(static_cast<char>(originalNaluHeader));
        _fuBuffer.append(reinterpret_cast<const char*>(fragmentData),fragmentSize);
        qDebug()<<"FU-A start:seq="<<sequence<<"type="<<naluType;
        if(end){
            emit h264NaluReady(_fuBuffer,timestamp,market);
            resetFuA();
        }
        return;
    }

    if(!_assemblingFu){
        qDebug()<<"FU-A fragment without start";
        return;
    }

    if(timestamp != _fuTimestamp){
        qDebug()<<"FU-A timestamp changed";
        resetFuA();
        return;
    }

    //sequence number检查
    if(sequence!=_expectedFuSequence){
        qDebug()<<"FU-A sequence lost:expected="<<_expectedFuSequence<<" actual="<<sequence;
        resetFuA();
        return;
    }

    _expectedFuSequence=static_cast<quint16>(sequence+1);
    _fuBuffer.append(reinterpret_cast<const char*>(fragmentData),fragmentSize);

    if(end){
        qDebug()<<"FU-A complete:size="<<_fuBuffer.size()<<" timestamp"<<timestamp;
        emit h264NaluReady(_fuBuffer,timestamp,market);
        resetFuA();
    }



}

void RtpReceiver::resetFuA()
{
    _fuBuffer.clear();
    _assemblingFu=false;
    _fuTimestamp=0;
    _expectedFuSequence=0;

}

void RtpReceiver::onReadyRead()
{
    while (_socket.hasPendingDatagrams()) {
        qint64 datagramSize=_socket.pendingDatagramSize();
        if(datagramSize<=0){
            break;
        }
        QByteArray datagram;
        datagram.resize(static_cast<int>(datagramSize));

        qint64 readSize=_socket.readDatagram(datagram.data(),datagram.size());
        if(readSize<=0){
            continue;
        }

        datagram.resize(readSize);
        handleDatagram(datagram);
    }

}
