#ifndef H264DECODER_H
#define H264DECODER_H

#include <QObject>
#include<QByteArray>
#include<QImage>

extern"C"{
#include<libavcodec/avcodec.h>
#include<libswscale/swscale.h>
}


class H264Decoder : public QObject
{
    Q_OBJECT
public:
    explicit H264Decoder(QObject *parent = nullptr);
    ~H264Decoder()override;

    bool open();
    void close();
    void pushNalu(const QByteArray& nalu,quint32 timestamp,bool market);

private:
    void decodeAccessUnit(const QByteArray& data,quint32 timestamp);

private:
    AVCodecContext* _codecCtx=nullptr;
    AVPacket* _packet=nullptr;
    AVFrame* _frame=nullptr;
    SwsContext* _swsCtx=nullptr;
    QByteArray _accessUnitBuffer;
    quint32 _currentTimestamp=0;
    bool _hasTimestamp=false;
    bool _opened=false;

signals:
    void frameReady(const QImage& image,quint32 timestamp);
};

#endif // H264DECODER_H
