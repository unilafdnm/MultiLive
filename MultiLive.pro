QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

CONFIG_FILE = $$PWD/config.pri

!exists($$CONFIG_FILE) {
    error("Missing config.pri. Copy config.pri.example to config.pri and set FFMPEG_ROOT.")
}

include($$CONFIG_FILE)

isEmpty(FFMPEG_ROOT) {
    error("FFMPEG_ROOT is empty in config.pri.")
}

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
INCLUDEPATH += $$FFMPEG_ROOT/include

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

LIBS += -L$$FFMPEG_ROOT/lib \
        -lavformat \
        -lavcodec \
        -lavutil \
        -lswscale \
        -lswresample \
        -lavdevice


SOURCES += \
    accencoder.cpp \
    audioencoderthread.cpp \
    audioframequeue.cpp \
    cameracapture.cpp \
    h264encoder.cpp \
    liveclock.cpp \
    main.cpp \
    mainwindow.cpp \
    microphoneCapture.cpp \
    multipublisher.cpp \
    networkpublisher.cpp \
    outputtargetparser.cpp \
    screencapture.cpp \
    videoencoderthread.cpp \
    videoframequeue.cpp

HEADERS += \
    StreamConfig.h \
    accencoder.h \
    audioencoderthread.h \
    audioframequeue.h \
    cameracapture.h \
    h264encoder.h \
    liveclock.h \
    mainwindow.h \
    microphoneCapture.h \
    multipublisher.h \
    networkpublisher.h \
    outputtargetparser.h \
    outputtypes.h \
    screencapture.h \
    videoencoderthread.h \
    videoframequeue.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    .gitignore \
    README.md \
    config.pri.example
