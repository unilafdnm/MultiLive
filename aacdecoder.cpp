#include "aacdecoder.h"
#include <QDebug>

#include <algorithm>
#include <cstring>
#include <utility>
AacDecoder::AacDecoder()
{

}

AacDecoder::~AacDecoder()
{
    close();
}

bool AacDecoder::open(int sampleRate, int channels)
{
    if(_opened){
        return true;
    }
    _sampleRate=sampleRate;
    _channels=channels;

    const AVCodec* decoder=avcodec_find_decoder(AV_CODEC_ID_AAC);
    if(!decoder){
        qWarning()<<"AAC avcodec_find_decoder failed";
        return false;
    }
    _codecCtx=avcodec_alloc_context3(decoder);
    if(!_codecCtx){
        qWarning()<<"avcodec_alloc_context3 failed";
        return false;
    }
    _codecCtx->sample_rate=sampleRate;

    av_channel_layout_default(&_codecCtx->ch_layout,channels);

    _codecCtx->pkt_timebase=AVRational{1,sampleRate};

    if(!createAudioSpecificConfig(sampleRate,channels)){
        close();
        return false;
    }
    if(avcodec_open2(_codecCtx,decoder,nullptr)<0){
        qWarning()<<"open AAC decoder failed";
        return  false;
    }
    _frame=av_frame_alloc();
    _packet=av_packet_alloc();
    if(!_frame || !_packet){
        close();
        return false;
    }

    av_channel_layout_default(&_outputLayout,channels);
    int ret=swr_alloc_set_opts2(&_swrCtx,&_outputLayout,AV_SAMPLE_FMT_S16,sampleRate,
                                &_codecCtx->ch_layout,_codecCtx->sample_fmt,_codecCtx->sample_rate,0,nullptr);

    if(ret<0 || !_swrCtx){
        qWarning()<<"create SwrContext failed";
        close();
        return false;
    }

    if(swr_init(_swrCtx)<0){
        qWarning()<<"swr_init failed";
        close();
        return false;
    }
    _opened=true;

    qDebug()<< "AAC Decoder opened"<< "sampleRate =" << sampleRate<< "channels =" << channels;

    return true;

}

void AacDecoder::close()
{

    if(_swrCtx){
        swr_free(&_swrCtx);
    }
    if (_frame) {
       av_frame_free(&_frame);
   }

   if (_packet) {
       av_packet_free(&_packet);
   }

   if (_codecCtx) {
       avcodec_free_context(&_codecCtx);
   }

   av_channel_layout_uninit(
       &_outputLayout
   );

   _sampleRate = 0;
   _channels = 0;
   _opened = false;
}

bool AacDecoder::decode(const uint8_t *data, size_t size, uint32_t timestamp)
{
    if(!_opened || !data || size ==0){
        return false;
    }
    av_packet_unref(_packet);
    if(av_new_packet(_packet,size)<0){
        return false;
    }
    std::memcpy(_packet->data,data,size);

    _packet->pts=timestamp;
    _packet->dts=timestamp;
    int ret=avcodec_send_packet(_codecCtx,_packet);
    if(ret<0){
        qWarning()<<"avcodec_send_packet failed:"<<ret;
        return false;
    }

    while(true){
        ret=avcodec_receive_frame(_codecCtx,_frame);
        if(ret==AVERROR(EAGAIN) || ret==AVERROR_EOF){
            break;
        }
        if(ret<0){
            qWarning()<<"AAC decode failed:"<<ret;
            return false;
        }

        int64_t delay=swr_get_delay(_swrCtx,_codecCtx->sample_rate);
        int outSamples=static_cast<int>(av_rescale_rnd(delay+_frame->nb_samples,_sampleRate,_codecCtx->sample_rate,AV_ROUND_UP));

        constexpr int BYTES_PER_SAMPLE=sizeof(int16_t);

        std::vector<uint8_t> pcm;
        pcm.resize(static_cast<size_t>(outSamples*_channels*BYTES_PER_SAMPLE));

        uint8_t* outputData[]={pcm.data()};


        int converted=swr_convert(_swrCtx,outputData,outSamples,const_cast<const uint8_t**>(_frame->extended_data),_frame->nb_samples);

        if(converted<0){
            qWarning()<<"swr_convert failed";
            return false;
        }
        pcm.resize(static_cast<size_t>(converted*_channels*BYTES_PER_SAMPLE));
        if(_pcmCallback){
            _pcmCallback(std::move(pcm),timestamp);
        }
        av_frame_unref(_frame);

    }

    return true;

}

void AacDecoder::setPcmCallback(AacDecoder::PcmCallback callback)
{

    _pcmCallback=std::move(callback);

}

bool AacDecoder::createAudioSpecificConfig(int sampleRate, int channels)
{
    static const int sampleRates[]={
        96000,
        88200,
        64000,
        48000,
        44100,
        32000,
        24000,
        22050,
        16000,
        12000,
        11025,
        8000,
        7350
    };

    int frequencyIndex=-1;
    for(int i=0;i<13;i++){
        if(sampleRates[i]==sampleRate){
            frequencyIndex=i;
            break;
        }
    }

    if(frequencyIndex<0){
        qWarning()<<"Unsupported AAC sample rate:"<<sampleRate;
        return false;
    }

    if(channels<0 || channels>7){
        return false;
    }

    constexpr int AUDIO_OBJECT_TYPE=2;//AAC-LC
    /*
        AudioSpecificConfig:

        audioObjectType          5 bit
        samplingFrequencyIndex   4 bit
        channelConfiguration     4 bit
        最后3bit为000
    */
    uint8_t config0=(static_cast<uint8_t>(AUDIO_OBJECT_TYPE)<<3)|(static_cast<uint8_t>(frequencyIndex)>>1);
    uint8_t config1=(static_cast<uint8_t>(frequencyIndex&0x01)<<7)|(static_cast<uint8_t>(channels)<<3);

    /*
        FFmpeg 要求 extradata 后面额外有
        AV_INPUT_BUFFER_PADDING_SIZE 个 0。
    */
    _codecCtx->extradata=static_cast<uint8_t*>(av_mallocz(2+AV_INPUT_BUFFER_PADDING_SIZE));
    if(!_codecCtx->extradata){
        return false;
    }
    _codecCtx->extradata[0]=config0;
    _codecCtx->extradata[1]=config1;
    _codecCtx->extradata_size=2;

    return  true;
}
