TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.cpp \
    log.cpp \
    packet_queue.cpp \
    util_time.cpp \
    audio_output.cpp \
    sonic.cpp

win32 {
INCLUDEPATH += $$PWD/ffmpeg-4.0.2-win32-dev/include
INCLUDEPATH += $$PWD/SDL2/include

LIBS += $$PWD/ffmpeg-4.0.2-win32-dev/lib/avformat.lib   \
        $$PWD/ffmpeg-4.0.2-win32-dev/lib/avcodec.lib    \
        $$PWD/ffmpeg-4.0.2-win32-dev/lib/avdevice.lib   \
        $$PWD/ffmpeg-4.0.2-win32-dev/lib/avfilter.lib   \
        $$PWD/ffmpeg-4.0.2-win32-dev/lib/avutil.lib     \
        $$PWD/ffmpeg-4.0.2-win32-dev/lib/postproc.lib   \
        $$PWD/ffmpeg-4.0.2-win32-dev/lib/swresample.lib \
        $$PWD/ffmpeg-4.0.2-win32-dev/lib/swscale.lib    \
        $$PWD/SDL2/lib/x86/SDL2.lib
}

HEADERS += \
    log.h \
    packet_queue.h \
    util_time.h \
    video_state.h \
    sonic.h \
    audio_output.h
