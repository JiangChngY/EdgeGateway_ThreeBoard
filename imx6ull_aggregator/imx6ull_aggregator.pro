QT += core network sql
QT -= gui
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = edge-aggregator

SOURCES += \
    main.cpp \
    aggregatorserver.cpp \
    aggregatordatastore.cpp \
    statushttpserver.cpp \
    mqttforwarder.cpp

HEADERS += \
    aggregatorserver.h \
    aggregatordatastore.h \
    statushttpserver.h \
    mqttforwarder.h

unix:target.path = /opt/edge-gateway/bin
INSTALLS += target
