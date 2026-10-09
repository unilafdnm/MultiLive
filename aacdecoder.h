#ifndef AACDECODER_H
#define AACDECODER_H

#include<cstddef>
#include<cstdint>
#include<functional>
#include<vector>

extern"C"{
#include<libavcodec/avcodec.h>
#include<libavutil/channel_layout.h>
#include<libswresample/swresample.h>
}


class AacDecoder
{
    using PcmCallback=std::function<void(std::vector<uint8_t> pcm,uint32_t timestamp)>;

public:
    AacDecoder();
    ~AacDecoder();

    bool open(int sampleRate,int channels);
    void close();

    bool decode(const uint8_t* data,size_t size,uint32_t timestamp);

    void setPcmCallback(PcmCallback callback);

private:
    bool createAudioSpecificConfig(int sampleRate,int channels);

private:
    SwrContext* _swrCtx=nullptr;
    AVCodecContext* _codecCtx=nullptr;
    AVPacket* _packet=nullptr;
    AVFrame* _frame=nullptr;
    AVChannelLayout _outputLayout;
    int _sampleRate=0;
    int _channels=0;
    bool _opened=false;
    PcmCallback _pcmCallback;

};

#endif // AACDECODER_H
