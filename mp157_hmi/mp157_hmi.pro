QT += core gui widgets serialport network sql
CONFIG += c++11
TEMPLATE = app
TARGET = edge-hmi

INCLUDEPATH += ../common

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    serialdevice.cpp \
    datastore.cpp \
    edgeuploader.cpp \
    trendwidget.cpp \
    ../common/edge_protocol.c

HEADERS += \
    mainwindow.h \
    serialdevice.h \
    datastore.h \
    edgeuploader.h \
    trendwidget.h \
    sensormodel.h \
    ../common/edge_protocol.h

unix:target.path = /opt/edge-gateway/bin
INSTALLS += target

