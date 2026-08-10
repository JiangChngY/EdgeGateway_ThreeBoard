#ifndef SERIAL_DEVICE_H
#define SERIAL_DEVICE_H

#include "sensormodel.h"

#include <QObject>
#include <QSerialPort>

extern "C" {
#include "edge_protocol.h"
}

class SerialDevice : public QObject
{
    Q_OBJECT
public:
    explicit SerialDevice(QObject *parent = nullptr);
    bool open(const QString &portName, qint32 baudRate);
    void close();
    bool isOpen() const;
    QString errorString() const;

    bool setTemperatureThreshold(double celsius);
    bool resetAlarm();
    bool setLed(bool enabled);
    bool setBuzzer(bool enabled);

signals:
    void sampleReceived(const SensorSample &sample);
    void connectionChanged(bool connected, const QString &detail);
    void protocolError(const QString &detail);
    void commandAcknowledged(quint8 commandId, quint8 status);

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    bool sendCommand(quint8 commandId, qint32 value);
    void handleMessage(const eg_message_t &message);

    QSerialPort m_port;
    eg_parser_t m_parser;
    quint16 m_txSequence = 0;
};

#endif
