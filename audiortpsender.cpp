#include "audiortpsender.h"
#include <QByteArray>
#include <QDebug>
#include <QMetaObject>
#include <QThread>

#include <cstring>
#include <random>

extern "C" {
#include <libavutil/mathematics.h>
}



AudioRtpSender::AudioRtpSender()
{

}

AudioRtpSender::~AudioRtpSender()
{
    close();
}

bool AudioRtpSender::open(const std::string &ip, uint16_t port)
{
    if(_opened){
        return true;
    }
    _remoteAddress=QHostAddress(QString::fromStdString(ip));
    if(_remoteAddress.isNull() || port ==0){
        return false;
    }
    _remotePort=port;

    std::random_device rd;

    _sequence=static_cast<uint16_t>(rd());
    _timestampBase=static_cast<uint32_t>(rd());
    _ssrc=static_cast<uint32_t>(rd());

    _opened=true;
    return true;
}

void AudioRtpSender::close()
{
    _socket.close();
    _opened=false;
}

bool AudioRtpSender::isOpen() const
{
    return _opened;
}


bool AudioRtpSender::sendAAc(const AVPacket *packet, AVRational timeBase)
{
    if(!_opened ||!packet || !packet->data||packet->size<=0){
        return false;
    }

    int64_t pts=packet->pts;
    if(pts == AV_NOPTS_VALUE){
        pts=packet->dts;
    }
    if(pts==AV_NOPTS_VALUE){
        qWarning()<<"AAC packet has no timestamp";
        return false;
    }

    int64_t mediaTimestamp=av_rescale_q(pts,timeBase,AVRational{1,static_cast<int>(_sampleRate)});
    uint32_t timestamp=_timestampBase + static_cast<uint32_t>(mediaTimestamp);

    constexpr size_t AU_HEADER_SIZE=4;
    const size_t aacSize=static_cast<size_t>(packet->size);

    if(aacSize+AU_HEADER_SIZE >MAX_RTP_PAYLOAD){
        qWarning()<<"AAC frame too large";
        return false;
    }

    if(aacSize > 0x1FFF){
        qWarning()<<"AAC AU size exceeds 13 bits";
        return false;
    }
    std::vector<uint8_t> payload(AU_HEADER_SIZE+aacSize);

    //AU-Header-length =16bits
    payload[0]=0x00;
    payload[1]=0x10;

    //AU-Header AU-size:13bit AU-Index:3bit AU-Index=0
    uint16_t auHeader = static_cast<uint16_t>(aacSize << 3);

    payload[2] = static_cast<uint8_t>(auHeader >> 8);
    payload[3] = static_cast<uint8_t>(auHeader & 0xFF);

    std::memcpy(payload.data()+AU_HEADER_SIZE,packet->data,aacSize);
    return sendRtpPacket(payload.data(),payload.size(),timestamp,true);

}

void AudioRtpSender::setSampleRate(uint32_t sampleRate)
{
    if(sampleRate){
        _sampleRate=sampleRate;
    }

}

bool AudioRtpSender::sendRtpPacket(const uint8_t *data, size_t size, uint32_t timestamp, bool marker)
{
    if(!_opened ||!data||size==0){
        return false;
    }

    constexpr size_t RTP_HEADER_SIZE=12;
    std::vector<uint8_t> packet;
    packet.resize(RTP_HEADER_SIZE+size);
    //Byte 0 V=2,P=0,X=0,CC=0
    packet[0]=0x80;
    //Byte1 M|payloadType
    packet[1]=0x80|RTP_PAYLOAD_TYPE;

    //sequence
    packet[2]=static_cast<uint8_t>(_sequence>>8);
    packet[3]=static_cast<uint8_t>(_sequence&0xFF);

    //timestamp
    packet[4]=static_cast<uint8_t>(timestamp>>24);
    packet[5]=static_cast<uint8_t>(timestamp>>16);
    packet[6]=static_cast<uint8_t>(timestamp>>8);
    packet[7]=static_cast<uint8_t>(timestamp&0xFF);

    //ssrc
    packet[8]=static_cast<uint8_t>(_ssrc>>24);
    packet[9]=static_cast<uint8_t>(_ssrc>>16);
    packet[10]=static_cast<uint8_t>(_ssrc>>8);
    packet[11]=static_cast<uint8_t>(_ssrc&0xFF);

    std::memcpy(packet.data()+RTP_HEADER_SIZE,data,size);

    /*
       和视频 RTP 一样：

       sendAAC() 是 AudioEncoderThread 调进来的，

       QUdpSocket 属于创建它的 Qt 线程。

       所以不要直接跨线程 writeDatagram。
    */

   QByteArray datagram(reinterpret_cast<const char*>(packet.data()),static_cast<int>(packet.size()));

   QHostAddress address = _remoteAddress;
   quint16 port = _remotePort;

   bool queued = QMetaObject::invokeMethod(
       &_socket,
       [this, datagram, address, port]() {
           qint64 ret =_socket.writeDatagram(datagram,address,port);
           if (ret < 0) {
               qWarning()<< "Audio RTP send failed:"<< _socket.errorString();
           }
       },
       Qt::QueuedConnection
   );
   if (!queued) {
       return false;
   }
   ++_sequence;

   return true;


}

