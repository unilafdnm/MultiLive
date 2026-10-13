#ifndef LANSESSION_H
#define LANSESSION_H

#include <QObject>
#include<QTcpServer>
#include<QTcpSocket>
#include<QPointer>
#include<QTimer>

struct LanSession{
    quint32 videoSsrc=0;
    quint32 videoBase;
    quint32 audioSsrc=0;
    quint32 audioBase=0;
};

class LanControl : public QObject
{
    Q_OBJECT
public:
    explicit LanControl(QObject *parent = nullptr);
    ~LanControl();

    bool listen(quint16 port=6000);
    void connectToPeer(const QString&ip,const LanSession& session,quint16 port=6000);
    void stopClient();

    QString serverError()const;

    void replyReady();
    void rejectSession();

private:
    QTcpServer _server;
    QTcpSocket _client;
    QPointer<QTcpSocket> _peer;
    QTimer _timeout;

    LanSession _session;
    bool _clientActiva=false;
    bool _ready=false;

    bool _peerSessionStarted=false;

signals:

    void sessionReceived(LanSession session);
    void senderReady();
    void peerClosed();
    void failed(QString reason);

};

#endif // LANSESSION_H
