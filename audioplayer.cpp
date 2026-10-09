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

    if(!SDL_ResumeAudioStreamDevice(_stream)){
        qWarning()<<"SDL resume failed:"<<SDL_GetError();
        SDL_DestroyAudioStream(_stream);
        _stream=nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    _sampleRate=sampleRate;
    _channels=channels;

    _totalBytesWriteen=0;

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

bool AudioPlayer::player(const uint8_t *data, size_t size)
{
    if(!_opened || !data || size<=0){
        return false;
    }

    if(!SDL_PutAudioStreamData(_stream,data,size)){
        qWarning() << "SDL play failed:" << SDL_GetError();
        return false;
    }
    _totalBytesWriteen+=size;

    qDebug()<<"_total"<<_totalBytesWriteen;
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

    uint64_t total=static_cast<uint64_t>(_totalBytesWriteen);
    uint64_t queueBytes=static_cast<uint64_t>(queued);

    uint64_t playedBytes=total>queueBytes?total-queueBytes:0;
    const uint64_t bytesPerSecond=static_cast<uint64_t>(_sampleRate)*_channels*sizeof (uint16_t);

    return static_cast<int64_t>(playedBytes*1000000ULL/bytesPerSecond);

}

bool AudioPlayer::started() const
{
    return _totalBytesWriteen;
}
