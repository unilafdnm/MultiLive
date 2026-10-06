#include "outputtargetparser.h"
#include<QUrl>
#include<QUrlQuery>
outputtargetparser::outputtargetparser()
{

}

OutputTargetParseResult parseOutputTarget(const QString &input)
{
    OutputTargetParseResult result;
    if(input.isEmpty()){
        result.error="地址为空";
        return result;
    }

    QUrl url(input.trimmed(),QUrl::StrictMode);

    if(!url.isValid()){
        result.error="无效URL";
        return result;
    }


    if(url.scheme().compare(QStringLiteral("rtmp"),Qt::CaseInsensitive)==0
            ||url.scheme().compare(QStringLiteral("rtmps"),Qt::CaseInsensitive)==0){

        if(url.host().isEmpty() || url.path().isEmpty() || url.path() == QStringLiteral("/")){
            result.error="无效URL";
            return result;
        }

        result.target.emplace();
        result.target->protocol=OutputProtocol::RTMP;
        result.target->url=input.toStdString();
        result.error="OK";
        return result;
    }else if(url.scheme().compare(QStringLiteral("srt"),Qt::CaseInsensitive)==0){

        QString path=url.path();
        QUrlQuery query(url);
        bool hasmode=query.hasQueryItem("mode");
        if(hasmode&& !(query.queryItemValue("mode")=="caller")){
            result.error="无效URL";
            return result;
        }
        if(url.host().isEmpty() || url.port()>65535 || url.port() <1){
            result.error="无效URL";
            return result;
        }
        result.target.emplace();
        result.target->protocol=OutputProtocol::SRT;
        result.target->url=input.toStdString();
        result.error="OK";
        return result;
    }else{
        result.error="无效URL";
        return result;
    }


}
