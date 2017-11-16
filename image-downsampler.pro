QT       += core
QT       += gui

TARGET    = image-downsampler

CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app

SOURCES += main.cpp

QMAKE_CXXFLAGS_RELEASE += -O3 -mavx -mavx2
QMAKE_CXXFLAGS_DEBUG += -mavx -mavx2

win32-g++* {
    QMAKE_LFLAGS += -static -static-libstdc++
}
