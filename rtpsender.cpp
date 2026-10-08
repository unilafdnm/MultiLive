#include "rtpsender.h"
#include<random>

#include<cstring>
namespace {

bool startCodeAt(const uint8_t* data,size_t size,size_t position,size_t& startCodeSize){

    if(position+3<=size&&data[position]==0x00 && data[position+1]==0x00 && data[position+2]==0x01){
        startCodeSize=3;
        return true;
    }
    if(position+4<=size&&data[position]==0x00 && data[position+1]==0x00 && data[position+2]==0x00
            &&data[position+3]==0x01){
        startCodeSize=4;
        return true;
    }
    return false;
}

}


rtpsender::rtpsender():_opened(false),_sequence(0),_ssrc(0),_timestampBase(0)
{

}

rtpsender::~rtpsender()
{
    close();
}

bool rtpsender::open(const std::string &ip, uint16_t port)
{
    if(_opened){
        return true;
    }

    _remoteAddress=QHostAddress(QString::fromStdString(ip));
    if(_remoteAddress.isNull()){
        return false;
    }

    _remoteport=port;

    std::random_device rd;
    _sequence=static_cast<uint32_t>(rd());
    _ssrc=static_cast<uint32_t>(rd());
    _timestampBase=static_cast<uint32_t>(rd());
    _opened=true;
    return true;
}

void rtpsender::close()
{
    _socket.close();
    _opened=false;
}

bool rtpsender::isopen() const
{
    return _opened;
}

bool rtpsender::sendH264(const AVPacket *packet, AVRational timeBase)
{
    if(!_opened || !packet ||!packet->data||packet->size<=0){
        return false;
    }

    int64_t pts=packet->pts;

    if(pts==AV_NOPTS_VALUE){
        pts=packet->dts;
    }
    if(pts == AV_NOPTS_VALUE){
        qDebug()<<"H264 packet has no timestamp";
        return false;
    }
    //H264 RTP CLock固定90kHz;
    const int64_t mediaTimestamp=av_rescale_q(pts,timeBase,AVRational{1,90000});
    const uint32_t timestamp=_timestampBase+static_cast<uint32_t>(mediaTimestamp);

    auto nalu=splitNalus(packet->data,packet->size);
    if(nalu.empty()){
        qDebug()<<"cannot split H264 packet";
        return false;
    }

    for(int i=0;i<nalu.size();i++){
        const bool lastNalu=(i+1==nalu.size());
        if(!sendNalu(nalu[i].data,nalu[i].size,timestamp,lastNalu)){
            return false;
        }

    }
    return true;

}

std::vector<rtpsender::Nalu> rtpsender::splitNalus(const uint8_t *data, size_t size)
{
    std::vector<Nalu> result;
    if(!data || size==0){
        return result;
    }

    //先尝试Annex-B
    bool hasStartCode=false;
    for(size_t i=0;i<size;i++){
        size_t startCodeSize=0;
        if(startCodeAt(data,size,i,startCodeSize)){
            hasStartCode=true;
            break;
        }
    }

    if(hasStartCode){
        size_t position=0;
        while(position < size){
            size_t startCodeSize=0;
            while(position<size && !startCodeAt(data,size,position,startCodeSize)){
                position++;
            }
            if(position > size){
                break;
            }
            const size_t nalustart=position+startCodeSize;
            position=nalustart;
            size_t nextStart=position;

            while(nextStart < size){
                if(startCodeAt(data,size,nextStart,startCodeSize)){
                    break;
                }
                nextStart++;
            }
            if(nextStart > nalustart){
                result.push_back({data+nalustart,nextStart-nalustart});
            }
            position=nextStart;
        }
        return result;
    }


    //再尝试AVCC，4byte length
    size_t position=0;

    while(position+4<=size){
        uint32_t naluSize=(static_cast<uint32_t>(data[position])<<24)|(static_cast<uint32_t>(data[position+1])<<16)|
                (static_cast<uint32_t>(data[position+2])<<8)|static_cast<uint32_t>(data[position+3]);
        position+=4;
        if(naluSize ==0 ||position+naluSize > size){
            result.clear();
            break;
        }
        result.push_back({data+position,naluSize});
        position+=naluSize;
    }
    return result;
}

bool rtpsender::sendNalu(const uint8_t *data, size_t size, uint32_t timestamp, bool market)
{
    if(!data || size ==0){
        return false;
    }
    if(size <= MAX_RTP_PAYLOAD){
        return sendSingleNalu(data,size,timestamp,market);
    }else{
        return sendFuA(data,size,timestamp,market);
    }

}

bool rtpsender::sendSingleNalu(const uint8_t *data, size_t size, uint32_t timestamp, bool market)
{
    return sendRtpPacket(data,size,timestamp,market);
}

bool rtpsender::sendFuA(const uint8_t *data, size_t size, uint32_t timestamp, bool market)
{
    if(!data || size<=1){
        return false;
    }
    const uint8_t naluHeader=data[0];
    const uint8_t forbiddenBit=naluHeader & 0x80;
    const uint8_t nri=naluHeader &0x60;
    const uint8_t naluType=naluHeader&0x1F;

    const uint8_t fuIndicator=forbiddenBit|nri|28;

    const uint8_t* payload=data+1;
    size_t remaining=size-1;

    // 两个字节给 FU Indicator
    // 和 FU Header
    const size_t maxFragmentSize=MAX_RTP_PAYLOAD-2;
    bool first=true;

    while(remaining >0){
        const size_t fragmentSize=std::min(remaining,maxFragmentSize);
        const bool last=fragmentSize==remaining;
        uint8_t fuheader=naluType;
        if(first){
            fuheader|=0x80;
        }
        if(last){
            fuheader|=0x40;
        }
        std::vector<uint8_t> rtpPayload;
        rtpPayload.reserve(fragmentSize+2);
        rtpPayload.push_back(fuIndicator);
        rtpPayload.push_back(fuheader);
        rtpPayload.insert(rtpPayload.end(),payload,payload+fragmentSize);
        //marker只给整个AVPacket最后一个分片
        const bool packerMaeker=last&market;
        if(!sendRtpPacket(rtpPayload.data(),rtpPayload.size(),timestamp,packerMaeker)){
            return false;
        }

        payload+=fragmentSize;
        remaining-=fragmentSize;
        first=false;
    }
    return true;
}

bool rtpsender::sendRtpPacket(const uint8_t *data, size_t size, uint32_t timestamp, bool market)
{
    if(!_opened||!data||size==0){
        return false;
    }

    constexpr size_t RTP_HEADER_SIZE=12;
    std::vector<uint8_t> packet;
    packet.resize(RTP_HEADER_SIZE+size);
    //Byte 0 V=2 P=0 X=0 CC=0
    packet[0]=0x80;

    //Byte 1 M+PayloadType

    packet[1]=static_cast<uint8_t>(RTP_PAYLOAD_TYPE&0x7F);
    if(market){
        packet[1]|=0x80;
    }

    //Sequence Number
    packet[2]=static_cast<uint8_t>(_sequence>>8);
    packet[3]=static_cast<uint8_t>(_sequence&0xFF);

    //Timestamp
    packet[4]=static_cast<uint8_t>(timestamp>>24);
    packet[5]=static_cast<uint8_t>(timestamp>>16);
    packet[6]=static_cast<uint8_t>(timestamp>>8);
    packet[7]=static_cast<uint8_t>(timestamp&0xFF);

    //SSRC
    packet[8]=static_cast<uint8_t>(_ssrc>>24);
    packet[9]=static_cast<uint8_t>(_ssrc>>16);
    packet[10]=static_cast<uint8_t>(_ssrc>>8);
    packet[11]=static_cast<uint8_t>(_ssrc&0xFF);

    std::memcpy(packet.data()+RTP_HEADER_SIZE,data,size);

    qint64 ret=_socket.writeDatagram(reinterpret_cast<const char*>(packet.data()),static_cast<qint64>(packet.size()),_remoteAddress,_remoteport);

    if(ret<0){
        qDebug()<<"send rtp failed:"<<_socket.errorString();
        return false;
    }
    ++_sequence;
    return true;
}

















