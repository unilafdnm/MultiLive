#include "audioplayer.h"
#include <QDebug>
AudioPlayer::AudioPlayer()
{

}

AudioPlayer::~AudioPlayer()
{
    close();
}

bool AudioPlayer::open(int sampleRate, int channels)
{
    if(_opened){
        return true;
    }

    if(!SDL_InitSubSystem(SDL_INIT_AUDIO)){
        qWarning()<<"SDL audio init failed:"<<SDL_GetError();
        return false;
    }


    SDL_AudioSpec wanted{};
    wanted.freq=sampleRate;
    wanted.format=SDL_AUDIO_S16;
    wanted.channels=channels;
    _stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&wanted,nullptr,nullptr);

    if(!_stream){
        qWarning()<<"SDL open failed:"<<SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

//    if(!SDL_ResumeAudioStreamDevice(_stream)){
//        qWarning()<<"SDL resume failed:"<<SDL_GetError();
//        SDL_DestroyAudioStream(_stream);
//        _stream=nullptr;
//        SDL_QuitSubSystem(SDL_INIT_AUDIO);
//        return false;
//    }

    _playbackStarted=false;

    _sampleRate=sampleRate;
    _channels=channels;

    _deviceBufferUs=0;
    SDL_AudioSpec deviceSpec{};
    int deviceSampleFrames=0;

    const SDL_AudioDeviceID device=SDL_GetAudioStreamDevice(_stream);

    if(device !=0 &&SDL_GetAudioDeviceFormat(device,&deviceSpec,&deviceSampleFrames)&&deviceSpec.freq>0
            &&deviceSampleFrames>0){
        _deviceBufferUs=static_cast<int64_t>(deviceSampleFrames)*1000000ULL/deviceSpec.freq;
    }


    _opened=true;

    return true;


}

void AudioPlayer::close()
{
    if (_stream) {
        SDL_DestroyAudioStream(_stream);
        _stream = nullptr;
    }

    if (_opened) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        _opened = false;
    }
    _sampleRate=0;
    _channels=0;

}

bool AudioPlayer::player(const uint8_t *data, size_t size,int64_t mediaPtsUs)
{
    if(!_opened || !data || size<=0){
        return false;
    }

    const int64_t bytesPerSecond =static_cast<int64_t>(_sampleRate)* _channels* sizeof(int16_t);

    if(!SDL_PutAudioStreamData(_stream,data,size)){
        qWarning() << "SDL play failed:" << SDL_GetError();
        return false;
    }




    /*
        实时音视频：
        如果已经积压超过 300ms，
        舍弃旧 PCM，重新追赶实时点。
    */
//    if (queuedUs > 300000) {

//        qDebug()
//            << "audio latency too large:"
//            << queuedUs / 1000.0
//            << "ms, clear queue";

//        //SDL_ClearAudioStream(_stream);
//    }



    const int64_t durationUs=static_cast<int64_t>(size)*1000000ULL/bytesPerSecond;
    _lastQueueEndUs=mediaPtsUs+durationUs;
    _clockInitialized=true;

    if(!_playbackStarted){
        const int queued=SDL_GetAudioStreamQueued(_stream);
        if(queued<0){
            return false;
        }
        const int64_t queuedUs =static_cast<int64_t>(queued)* 1000000LL/ bytesPerSecond;

        constexpr int64_t START_BUFFER_US=120000;
        if(queuedUs>=START_BUFFER_US){
            if(!SDL_ResumeAudioStreamDevice(_stream)){
                qWarning()<<"SDL resume failed"<<SDL_GetError();
                return false;
            }
            _playbackStarted=true;
        }


    }


    return true;

}

uint32_t AudioPlayer::queueBytes() const
{
    if(!_stream){
        return  0;
    }

    int bytes=SDL_GetAudioStreamQueued(_stream);
    return bytes?static_cast<uint32_t>(bytes):0;


}

int64_t AudioPlayer::playedUs() const
{
    if(!_stream || _sampleRate<=0 || _channels<=0){
        return  0;
    }

    int queued=SDL_GetAudioStreamQueued(_stream);
    if(queued<0){
        return 0;
    }
    const uint64_t bytesPerSecond=static_cast<uint64_t>(_sampleRate)*_channels*sizeof (uint16_t);

    const int64_t queueUs=static_cast<int64_t>(queued)*1000000ULL/bytesPerSecond;

    return _lastQueueEndUs-queueUs-_deviceBufferUs;



}

bool AudioPlayer::started() const
{
    return _clockInitialized&&_playbackStarted;
}

void AudioPlayer::reset()
{

    if(_stream){
        if(!SDL_PauseAudioStreamDevice(_stream)){
            qWarning()
                           << "SDL pause failed:"
                           << SDL_GetError();
        }

        if(!SDL_ClearAudioStream(_stream)){
            qWarning()<<"SDL clear audio stream failed:"<<SDL_GetError();
        }
    }

    _playbackStarted=false;
    _lastQueueEndUs=0;
    _clockInitialized=false;


}
