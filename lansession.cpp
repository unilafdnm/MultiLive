#include "lansession.h"
#include <QHostAddress>
#include<QNetworkProxy>
namespace
{

QByteArray makeStart(const LanSession&s){

    QByteArray msg="START ";
    msg+=QByteArray::number(s.videoSsrc)+" ";
    msg+=QByteArray::number(s.videoBase)+" ";
    msg+=QByteArray::number(s.audioSsrc)+" ";
    msg+=QByteArray::number(s.audioBase)+"\n";
    return msg;

}

bool parseStart(const QByteArray& line,LanSession& s){
    const auto fields=line.trimmed().split(' ');
    if(fields.size()!=5 || fields[0]!="START"){
        return false;
    }
    bool ok1,ok2,ok3,ok4;

    s.videoSsrc=fields[1].toUInt(&ok1);
    s.videoBase=fields[2].toUInt(&ok2);
    s.audioSsrc=fields[3].toUInt(&ok3);
    s.audioBase=fields[4].toUInt(&ok4);

    return ok1&&ok2&&ok3&&ok4;
}


}


LanControl::LanControl(QObject *parent) : QObject(parent)
{

    _client.setProxy(QNetworkProxy::NoProxy);
    _server.setProxy(QNetworkProxy::NoProxy);
    _timeout.setSingleShot(true);

    connect(&_server,&QTcpServer::newConnection,this,[this](){

        while(_server.hasPendingConnections()){
            QTcpSocket* peer=_server.nextPendingConnection();
            if(_peer){
                peer->disconnectFromHost();
                peer->deleteLater();
                continue;
            }
            _peer=peer;

            connect(peer,&QTcpSocket::readyRead,this,[this,peer]{

                while(peer->canReadLine()){
                    LanSession info;

                    const QByteArray line=peer->readLine();

                    if(_peerSessionStarted || !parseStart(line,info)){

                        peer->write("ERR\n");
                        peer->disconnectFromHost();
                        return;
                    }
                    _peerSessionStarted=true;
                    // 只通知业务层，不再自动发送 READY
                    emit sessionReceived(info);

                }

            });

            connect(peer,&QTcpSocket::disconnected,this,[this,peer](){
                _peerSessionStarted=false;
                if(_peer == peer){
                   _peer=nullptr;
                   emit peerClosed();
               }
               peer->deleteLater();
            });

        }
    });




    //发送端:TCP连接成功后，发送会话参数
    connect(&_client,&QTcpSocket::connected,this,[this](){
       _client.write(makeStart(_session));
    });

    //发送端:接收READY后才能开始采集
    connect(&_client,&QTcpSocket::readyRead,this,[this]{

        while(_client.canReadLine()){
            const QByteArray line=_client.readLine().trimmed();
            if(line == "READY"){
                _timeout.stop();
                if(!_ready){
                    _ready=true;
                    emit senderReady();
                }
            }else{
                emit failed("接收端拒绝会话");
            }

        }

    });

    connect(&_client,&QTcpSocket::disconnected,this,[this](){

        if(_clientActiva){
            _clientActiva=false;
            _timeout.stop();
            emit peerClosed();
        }
    });

    connect(&_client,QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),this,[this](){

        if(_clientActiva){
            emit failed(_client.errorString());
        }

    });

    connect(&_timeout,&QTimer::timeout,this,[this](){

        if(_clientActiva && !_ready){
            emit failed("等待接收端READY超时");
        }

    });


}

LanControl::~LanControl()
{
    stopClient();
    _server.close();
    if(_peer){
        QTcpSocket* peer=_peer.data();
        _peer=nullptr;
        QObject::disconnect(peer,nullptr,this,nullptr);
        peer->abort();
        delete peer;
    }

}

bool LanControl::listen(quint16 port)
{

    return _server.listen(QHostAddress::AnyIPv4,port);

}

void LanControl::connectToPeer(const QString &ip, const LanSession &session, quint16 port)
{

    stopClient();
    _session=session;
    _clientActiva=true;
    _ready=false;
    _timeout.start(5000);
    _client.connectToHost(ip,port);

}

void LanControl::stopClient()
{
    _timeout.stop();
    _clientActiva=false;
    _ready=false;
    _client.abort();

}

QString LanControl::serverError() const
{
    return _server.errorString();
}

void LanControl::replyReady()
{
    if(!_peer || _peer->state() != QAbstractSocket::ConnectedState){
        return;
    }
    _peer->write("READY\n");

}

void LanControl::rejectSession()
{
    if(!_peer){
        return;
    }
    _peer->write("ERR\n");
    _peer->disconnectFromHost();

}




