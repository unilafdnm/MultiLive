#ifndef OUTPUTTARGETPARSER_H
#define OUTPUTTARGETPARSER_H
#include<optional>
#include<string>
#include<QString>
#include"outputtypes.h"


struct OutputTargetParseResult{
    std::optional<OutputTarget> target;
    QString error;
};


OutputTargetParseResult parseOutputTarget(const QString& input);


class outputtargetparser
{
public:
    outputtargetparser();
};

#endif // OUTPUTTARGETPARSER_H
