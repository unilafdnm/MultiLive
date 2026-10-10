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

    bool player(const uint8_t* data,size_t size,int64_t mediaPtsUs);
    uint32_t queueBytes()const;

    //当前已经播放了多少微秒
    int64_t playedUs()const;

    bool started()const;
    void reset();

private:
    SDL_AudioStream* _stream=nullptr;
    bool _opened{false};

    int _sampleRate;
    int _channels=0;

    int64_t _lastQueueEndUs{0};
    int64_t _deviceBufferUs{0};
    bool _clockInitialized{false};
    bool _playbackStarted{false};

};

#endif // AUDIOPLAYER_H
