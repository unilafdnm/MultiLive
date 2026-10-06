#ifndef LIVECLOCK_H
#define LIVECLOCK_H
#include<QDebug>

extern "C"{
#include<libavutil/time.h>
}


class LiveClock
{
public:
    LiveClock():_startTimeUs(0){

    }

    void start(){
        _startTimeUs=av_gettime_relative();
    }

    int64_t elapsedUs()const{

        return av_gettime_relative()-_startTimeUs;

    }



private:
    int64_t _startTimeUs;
};

#endif // LIVECLOCK_H
