QT += testlib
CONFIG += console c++11
TEMPLATE = app
TARGET = edge-ui-test
include(edgegateway.pri)
SOURCES += test_ui.cpp
LIBS += -lutil
