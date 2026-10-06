#ifndef STREAMCONFIG_H
#define STREAMCONFIG_H

#include<string>

struct StreamConfig{
    int width;
    int height;
    int fps;
    int videoBitrate;
    int gopSeconds=2;
    std::string preset;
    int audioBitrate;
};


#endif // STREAMCONFIG_H
