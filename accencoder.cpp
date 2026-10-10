#include "accencoder.h"
#include<QDebug>
accencoder::accencoder(QObject *parent) : QObject(parent),_codec_ctx(nullptr)
  ,_swr_ctx(nullptr),_fifo(nullptr),_packet(nullptr),_frame(nullptr),_output_ctx(nullptr),
    _pts(0),_initialized(false),_sink(nullptr),_nextpts(0),_ptsInitialized(false)
{

}

accencoder::~accencoder()
{
    close();
}

bool accencoder::init(int inputSampleRate, const AVChannelLayout &inputChannelLayout, AVSampleFormat inputFormat,const StreamConfig& config)
{
    if(_initialized){
       return false;
    }
    AVChannelLayout inputLayout={};
    if(inputChannelLayout.order == AV_CHANNEL_ORDER_UNSPEC){
        av_channel_layout_default(&inputLayout,inputChannelLayout.nb_channels);
    }else{
        av_channel_layout_copy(&inputLayout,&inputChannelLayout);
    }


    const AVCodec* encoder=avcodec_find_encoder(AV_CODEC_ID_AAC);
    if(!encoder){
        qDebug()<<"audio encoder avcodec_find_encoder failed";
        return false;
    }
    _codec_ctx=avcodec_alloc_context3(encoder);
    if(!_codec_ctx){
        qDebug()<<"audio avcodec_alloc_context3";
        return false;
    }

    constexpr int OUTPUT_SAMPLE_RATE = 48000;
    constexpr int OUTPUT_CHANNELS = 2;

    _codec_ctx->sample_rate = OUTPUT_SAMPLE_RATE;
    _codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(
        &_codec_ctx->ch_layout,
        OUTPUT_CHANNELS
    );



//    _codec_ctx->sample_rate=inputSampleRate;
//    _codec_ctx->sample_fmt=AV_SAMPLE_FMT_FLTP;
//    av_channel_layout_copy(&_codec_ctx->ch_layout,&inputLayout);
    _codec_ctx->bit_rate=config.audioBitrate;
    _codec_ctx->time_base=AVRational{1,_codec_ctx->sample_rate};

    _codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if(avcodec_open2(_codec_ctx,encoder,nullptr) < 0){
        qDebug()<<"open AAC encoder failed";
        return false;
    }

    int ret=swr_alloc_set_opts2(&_swr_ctx,&_codec_ctx->ch_layout,_codec_ctx->sample_fmt,_codec_ctx->sample_rate,
                        &inputLayout,inputFormat,inputSampleRate,0,nullptr);
    if(ret < 0){
        qDebug()<<"create swr failed";
        avcodec_free_context(&_codec_ctx);
        return false;
    }
    if(swr_init(_swr_ctx) < 0){
        qDebug()<<"swr init failed";
        avcodec_free_context(&_codec_ctx);
        return false;
    }

    _fifo=av_audio_fifo_alloc(_codec_ctx->sample_fmt,_codec_ctx->ch_layout.nb_channels,_codec_ctx->frame_size*10);
    if(!_fifo){
        qDebug()<<"create fifo failed";
        avcodec_free_context(&_codec_ctx);
        swr_free(&_swr_ctx);
        return false;
    }
    _packet=av_packet_alloc();
    _frame=av_frame_alloc();
    _frame->format=_codec_ctx->sample_fmt;
    _frame->sample_rate=_codec_ctx->sample_rate;
    _frame->nb_samples=_codec_ctx->frame_size;
    av_channel_layout_copy(&_frame->ch_layout,&_codec_ctx->ch_layout);
    av_frame_get_buffer(_frame,0);

    _pts=0;
    _initialized=true;
    if(_sink){
        _sink->onEncoderReady(MediaType::Audio,_codec_ctx);
    }

    return true;

}

void accencoder::encode(AVFrame *frame,int64_t timestampUs)
{
    if(!_initialized){
        return;
    }

    if(!_ptsInitialized){
        _nextpts=av_rescale_q(timestampUs,AV_TIME_BASE_Q,_codec_ctx->time_base);
        _ptsInitialized=true;
    }


    int outSamples=av_rescale_rnd(swr_get_delay(_swr_ctx,frame->sample_rate)+frame->nb_samples,
                                  _codec_ctx->sample_rate,frame->sample_rate,AV_ROUND_UP);
    uint8_t** convertData=nullptr;
    int convertedLinesize=0;
    if(av_samples_alloc_array_and_samples(&convertData,&convertedLinesize,_codec_ctx->ch_layout.nb_channels,outSamples,_codec_ctx->sample_fmt,0)<0){
        return;
    }

    AVSampleFormat inputFormat=static_cast<AVSampleFormat>(frame->format);

    int inputPlanes=av_sample_fmt_is_planar(inputFormat)?frame->ch_layout.nb_channels:1;

    std::vector<const uint8_t*> inputData(inputPlanes);
    for(int i=0;i<inputPlanes;i++){
        inputData[i]=frame->extended_data[i];
    }

    int convertedSamples=swr_convert(_swr_ctx,convertData,outSamples,frame->extended_data,frame->nb_samples);

    if(convertedSamples <=0 ){
        av_freep(&convertData[0]);
        av_freep(&convertData);
        return;
    }

    int newSize=av_audio_fifo_size(_fifo)+convertedSamples;
    av_audio_fifo_realloc(_fifo,newSize);

    av_audio_fifo_write(_fifo,reinterpret_cast<void**>(convertData),convertedSamples);
    av_freep(&convertData[0]);
    av_freep(&convertData);

    while(av_audio_fifo_size(_fifo) >= _codec_ctx->frame_size){
        encodeFrame();
    }


}

void accencoder::close()
{
    if (!_initialized) return;


    // ============================
    // Flush AAC Encoder
    // ============================

    avcodec_send_frame(_codec_ctx,nullptr);


    writePackers();

    // 写文件结尾
    if (_output_ctx)
    {
        av_write_trailer(_output_ctx);
    }


    if (_output_ctx &&
        _output_ctx->pb)
    {
        avio_closep(
            &_output_ctx->pb
        );
    }


    if (_output_ctx)
    {
        avformat_free_context(
            _output_ctx
        );

        _output_ctx = nullptr;
    }


    if (_fifo)
    {
        av_audio_fifo_free(
            _fifo
        );

        _fifo = nullptr;
    }


    swr_free(
        &_swr_ctx
    );


    av_frame_free(
        &_frame
    );


    av_packet_free(
        &_packet
    );


    avcodec_free_context(
        &_codec_ctx
    );


    _initialized = false;



}

void accencoder::setPacketSink(EncodePacketSink *sink)
{
    _sink=sink;
}

void accencoder::encodeFrame()
{
    av_frame_make_writable(_frame);
    int readSamples=av_audio_fifo_read(_fifo,reinterpret_cast<void**>(_frame->data),_codec_ctx->frame_size);
    if(readSamples != _codec_ctx->frame_size){
        return;
    }
    _frame->pts=_nextpts;
    _nextpts+=readSamples;
    int ret=avcodec_send_frame(_codec_ctx,_frame);
    if(ret <0){
        qDebug()<<"send audio frame failed";
        return;
    }
    writePackers();
}

void accencoder::writePackers()
{
    while(true){
        int ret=avcodec_receive_packet(_codec_ctx,_packet);
        if(ret == AVERROR(EAGAIN) || ret ==AVERROR_EOF){
            break;
        }
        if(ret < 0){
            qDebug()<<"receive audio packet failed";
            return;
        }

        if(_sink){
            EncodePacket packet;
            packet.type=MediaType::Audio;
            packet.packet=_packet;
            packet.timeBase=_codec_ctx->time_base;
            _sink->onEncoderPacket(packet);
        }
        av_packet_unref(_packet);
    }


}
