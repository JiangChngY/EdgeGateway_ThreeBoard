#ifndef SENSOR_MODEL_H
#define SENSOR_MODEL_H

#include <QDateTime>
#include <QtGlobal>

struct SensorSample
{
    quint16 sourceSequence = 0;
    QDateTime receivedAt;
    double temperature = 0.0;
    double humidity = 0.0;
    quint16 light = 0;
    quint16 analog = 0;
    quint8 validFlags = 0;
    bool alarm = false;
};

#endif

