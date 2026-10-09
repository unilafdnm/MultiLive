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

bool AudioRtpSender::open(const std::string &ip, uint16_t port, uint32_t sampleRate)
{
    if(_opened){
        return true;
    }
    _remoteAddress=QHostAddress(QString::fromStdString(ip));
    if(!_remoteAddress.isNull() || port ==0){
        return false;
    }
    _remotePort=port;
    _sampleRate=sampleRate;

    _socket=new QUdpSocket();
    std::random_device rd;

    _sequence=static_cast<uint16_t>(rd());
    _timestampBase=static_cast<uint32_t>(rd());
    _ssrc=static_cast<uint32_t>(rd());

    _opened=true;
    return true;
}

void AudioRtpSender::close()
{
    if(_socket){
        delete  _socket;
        _socket=nullptr;
    }
    _opened=false;
}

bool AudioRtpSender::isOpen() const
{
    return _opened;
}

bool AudioRtpSender::sendAAC(const uint8_t* data, size_t size, uint32_t timestamp)
{
    if(!_opened || !data || size==0 || !_socket){
        return false;
    }
    /*
           MPEG4-GENERIC：

           RTP Payload:

           +--------------------+
           | AU-headers-length  |  2 bytes
           +--------------------+
           | AU-header          |  2 bytes
           +--------------------+
           | AAC Access Unit    |
           +--------------------+

           所以额外需要 4 bytes。
       */

    constexpr size_t AU_HEADER_SIZE=4;
    if(size + AU_HEADER_SIZE >MAX_RTP_PAYLOAD){
        qDebug()<<"AAC frame too large for one RTP packet";
        return false;
    }
    /*
           MPEG4-GENERIC 常用配置：

           sizeLength = 13
           indexLength = 3

           所以 AU-size 最大 13 bit。
     */
    if(size>0x1FFF){
        qDebug()<<"AAC AU size exceeds 13 bits";
        return false;
    }
    std::vector<uint8_t> payload;
    payload.resize(AU_HEADER_SIZE + size);

    /*
           AU-headers-length
           表示后面的 AU-header 总共有多少 bit。
           这里只有一个：
           AU-size  = 13 bit
           AU-index = 3 bit
           总共 = 16 bit
           因此：0x0010
    */
    payload[0]=0x00;
    payload[1]=0x10;

    //AU Header:13bit AU-size 3 bit AU-index
    //AU-index=0 ACC size<<3
    uint16_t auHeader=static_cast<uint16_t>(size<<3);
    payload[2]=static_cast<uint8_t>(auHeader>>8);
    payload[3]=static_cast<uint8_t>(auHeader&0xFF);
    std::memcpy(payload.data()+AU_HEADER_SIZE,data,size);

    //timestamp 参数是相对于媒体起点的时间,RTP实际时间戳增加随机base
    uint32_t rtpTimestamp=_timestampBase+timestamp;

    return sendRtpPacket(payload.data(),payload.size(),rtpTimestamp,true);




}

bool AudioRtpSender::sendRtpPacket(const uint8_t *data, size_t size, uint32_t timestamp, bool marker)
{
    if(!_opened || !_socket || !data||size==0){
        return false;
    }

    if(_socket->thread() != QThread::currentThread()){
        qWarning()<<"AudioRtpSender::sendRtpPacket called from wrong thread";
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

    qint64 ret=_socket->writeDatagram(reinterpret_cast<const char*>(packet.data()),static_cast<qint64>(packet.size()),
                                       _remoteAddress,_remotePort);
    if(ret!=static_cast<qint64>(packet.size())){
        qWarning()<<"Audio RTP send failed:"<<_socket->errorString();
        return false;
    }


   ++_sequence;

   return true;


}

