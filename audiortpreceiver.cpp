#include "audiortpreceiver.h"

#include <QDebug>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QThread>

#include <utility>

AudioRtpReceiver::AudioRtpReceiver()
{

}

AudioRtpReceiver::~AudioRtpReceiver()
{
    close();
}

bool AudioRtpReceiver::open(uint16_t port)
{

    if(_opened){
        return true;
    }
    _port=port;

    _socket=new QUdpSocket();
    bool ok=_socket->bind(QHostAddress::AnyIPv4,_port,QUdpSocket::ShareAddress|QUdpSocket::ReuseAddressHint);
    if(!ok){
        qWarning()<<"Audio RTP bind failed"<<_socket->errorString();
        delete  _socket;
        _socket=nullptr;
        return false;
    }

    QObject::connect(_socket,&QUdpSocket::readyRead,_socket,[this](){
       onReadyRead();
    });

    _opened=true;
    qDebug()<< "Audio RTP Receiver opened"<< "port =" << _port;
    return true;

}

void AudioRtpReceiver::close()
{
    if(!_socket){
        _opened=false;
        return;
    }

    if(_socket->thread() != QThread::currentThread()){
        qWarning()<< "AudioRtpReceiver::close called from wrong thread";
        return;
    }
    _socket->close();
    delete  _socket;
    _socket=nullptr;
    _opened=false;
    return;

}

void AudioRtpReceiver::setAacCallback(AudioRtpReceiver::AacCallback callback)
{
    _aacCallback=std::move(callback);

}

void AudioRtpReceiver::onReadyRead()
{
    if(!_socket){
        return;
    }

    while(_socket->hasPendingDatagrams()){
        QNetworkDatagram datagram=_socket->receiveDatagram();
        QByteArray raw=datagram.data();
        if(raw.isEmpty()){
            continue;
        }
        parseRtpPacket(reinterpret_cast<const uint8_t*>(raw.constData()),static_cast<size_t>(raw.size()));

    }



}

bool AudioRtpReceiver::parseRtpPacket(const uint8_t *data, size_t size)
{
    constexpr size_t RTP_HEADER_SIZE=12;
    if(!data || size < RTP_HEADER_SIZE){
        return false;
    }

    uint8_t version=data[0]>>6;
    if(version!=2){
        qWarning()<<"Invalid RTP version:"<<version;
        return false;
    }
    bool padding=data[0]&0x20 !=0;
    bool extension=data[0]&0x10!=0;
    uint8_t csrcCount=data[0]&0x0F;

    bool marker=data[1]&0x80!=0;
    uint8_t payloadType=data[1]&0x7F;
    if(payloadType != RTP_PAYLOAD_TYPE){
        return false;
    }

    //sequence number
    uint16_t sequence=(static_cast<uint16_t>(data[2])<<8)|static_cast<uint16_t>(data[3]);

    //timestamp
    uint32_t timestamp=(static_cast<uint32_t>(data[4])<<24)|(static_cast<uint32_t>(data[5])<<16)|
                (static_cast<uint32_t>(data[6])<<8)|(static_cast<uint32_t>(data[7]));

    uint32_t ssrc=(static_cast<uint32_t>(data[8])<<24)|(static_cast<uint32_t>(data[9])<<16)|
                (static_cast<uint32_t>(data[10])<<8)|(static_cast<uint32_t>(data[11]));

    int headerSize=RTP_HEADER_SIZE+csrcCount*4;
    if(headerSize > size){
        return false;
    }

    if(extension){
        if(headerSize+4 >size){
            return false;
        }
        const uint16_t extensionLengtn=static_cast<uint16_t>(data[headerSize+2]<<8)|static_cast<uint16_t>(data[headerSize+3]);
        headerSize+=4+extensionLengtn*4;
        if(headerSize>size){
            return false;
        }
    }

    int payloadEnd=size;
    if(padding){
        const uint8_t paddingSize=data[payloadEnd-1];
        if(paddingSize ==0 || paddingSize >size-headerSize){
            return false;
        }
        payloadEnd-=paddingSize;
    }

    const int payloadSize=payloadEnd-headerSize;

    if(payloadSize<=0){
        return false;
    }

    const uint8_t* payload=data+headerSize;


    return handleAacPayload(payload,payloadSize,sequence,timestamp,marker);




}

bool AudioRtpReceiver::handleAacPayload(const uint8_t *payload, size_t payloadSize, uint16_t sequence, uint32_t timestamp, bool marker)
{

    if(!payload || payloadSize<=0){
        return false;
    }

    constexpr size_t AU_HEADER_SIZE=4;
    if(payloadSize < AU_HEADER_SIZE){
        return false;
    }

    uint16_t auHeadersLength=(static_cast<uint16_t>(payload[0])<<8)|static_cast<uint16_t>(payload[1]);

    if(auHeadersLength!=16){
        qWarning()<<"Unsupported Au header lengtn"<<auHeadersLength;
        return false;
    }

    uint16_t auHeader=(static_cast<uint16_t>(payload[2])<<8)|static_cast<uint16_t>(payload[3]);
    size_t aacSize=auHeader>>3;
    uint8_t auIndex=static_cast<uint8_t>(auHeader&0x07);
    if(auIndex!=0){
        return false;
    }

    if(aacSize > payloadSize-AU_HEADER_SIZE){
        return false;
    }

    const uint8_t* aacData=payload+AU_HEADER_SIZE;
    std::vector<uint8_t> aacFrame(aacData,aacData+aacSize);

    if(_aacCallback){
        _aacCallback(std::move(aacFrame),timestamp);
    }
    return true;


}
