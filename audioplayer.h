#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include<SDL3/SDL.h>
#include<cstddef>
#include<cstdint>
#include<atomic>
class AudioPlayer
{
public:
    AudioPlayer();
    ~AudioPlayer();
    bool open(int sampleRate,int channels);
    void close();

    bool player(const uint8_t* data,size_t sie);
    uint32_t queueBytes()const;

    //当前已经播放了多少微秒
    int64_t playedUs()const;

    bool started()const;

private:
    SDL_AudioStream* _stream=nullptr;
    bool _opened{false};

    int _sampleRate;
    int _channels=0;

    std::atomic<uint64_t> _totalBytesWriteen{0};

};

#endif // AUDIOPLAYER_H
