#include "mainwindow.h"

#include <QApplication>

extern "C" {
#include <libavutil/log.h>
}

int main(int argc, char *argv[])
{
    av_log_set_level(AV_LOG_ERROR);
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
