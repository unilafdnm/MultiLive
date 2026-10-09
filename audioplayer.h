#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include<SDL3/SDL.h>
#include<cstddef>
#include<cstdint>

class AudioPlayer
{
public:
    AudioPlayer();
    ~AudioPlayer();
    bool open(int sampleRate,int channels);
    void close();

    bool player(const uint8_t* data,size_t sie);
    uint32_t queueBytes()const;


private:
    SDL_AudioStream* _stream=nullptr;
    bool _opened{false};
};

#endif // AUDIOPLAYER_H
