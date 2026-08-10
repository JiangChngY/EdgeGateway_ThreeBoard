#include "serialdevice.h"

#include <QDateTime>

SerialDevice::SerialDevice(QObject *parent)
    : QObject(parent)
{
    eg_parser_init(&m_parser);
    connect(&m_port, &QSerialPort::readyRead, this, &SerialDevice::onReadyRead);
    connect(&m_port, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::error),
            this, &SerialDevice::onSerialError);
}

bool SerialDevice::open(const QString &portName, qint32 baudRate)
{
    close();
    m_port.setPortName(portName);
    m_port.setBaudRate(baudRate);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);
    eg_parser_init(&m_parser);
    if (!m_port.open(QIODevice::ReadWrite)) {
        emit connectionChanged(false, m_port.errorString());
        return false;
    }
    emit connectionChanged(true, portName + QStringLiteral(" @ ") + QString::number(baudRate));
    return true;
}

void SerialDevice::close()
{
    if (m_port.isOpen()) {
        m_port.close();
        emit connectionChanged(false, QStringLiteral("串口已关闭"));
    }
}

bool SerialDevice::isOpen() const
{
    return m_port.isOpen();
}

QString SerialDevice::errorString() const
{
    return m_port.errorString();
}

bool SerialDevice::setTemperatureThreshold(double celsius)
{
    return sendCommand(EG_CMD_SET_TEMP_THRESHOLD, qRound(celsius * 100.0));
}

bool SerialDevice::resetAlarm()
{
    return sendCommand(EG_CMD_ALARM_RESET, 0);
}

bool SerialDevice::setLed(bool enabled)
{
    return sendCommand(EG_CMD_LED, enabled ? 1 : 0);
}

bool SerialDevice::setBuzzer(bool enabled)
{
    return sendCommand(EG_CMD_BUZZER, enabled ? 1 : 0);
}

bool SerialDevice::sendCommand(quint8 commandId, qint32 value)
{
    if (!m_port.isOpen()) {
        emit protocolError(QStringLiteral("串口未打开，命令未发送"));
        return false;
    }
    quint8 payload[5];
    quint8 frame[EG_MAX_FRAME_SIZE];
    size_t length = 0;
    eg_encode_command(commandId, value, payload);
    const eg_build_result_t result = eg_build_frame_ex(
        EG_MSG_COMMAND, ++m_txSequence, payload, sizeof(payload),
        frame, sizeof(frame), &length);
    if (result != EG_BUILD_OK) {
        emit protocolError(QStringLiteral("命令帧构建失败：%1").arg(int(result)));
        return false;
    }
    const qint64 accepted = m_port.write(reinterpret_cast<const char *>(frame), qint64(length));
    if (accepted != qint64(length)) {
        emit protocolError(QStringLiteral("串口发送缓冲区写入失败"));
        return false;
    }
    return true;
}

void SerialDevice::onReadyRead()
{
    const QByteArray data = m_port.readAll();
    eg_message_t message;
    for (const char raw : data) {
        const eg_parse_result_t result = eg_parser_push(
            &m_parser, static_cast<quint8>(raw), &message);
        if (result == EG_PARSE_FRAME) {
            handleMessage(message);
        } else if (result < 0) {
            emit protocolError(QStringLiteral("串口帧校验失败：%1").arg(int(result)));
        }
    }
}

void SerialDevice::handleMessage(const eg_message_t &message)
{
    if (message.type == EG_MSG_SENSOR) {
        eg_sensor_payload_t payload;
        if (!eg_decode_sensor(message.payload, message.payload_length, &payload)) {
            emit protocolError(QStringLiteral("传感器负载长度错误"));
            return;
        }
        SensorSample sample;
        sample.sourceSequence = message.sequence;
        sample.receivedAt = QDateTime::currentDateTime();
        sample.temperature = payload.temperature_centi_c / 100.0;
        sample.humidity = payload.humidity_centi_percent / 100.0;
        sample.light = payload.light_raw;
        sample.analog = payload.analog_raw;
        sample.validFlags = payload.valid_flags;
        sample.alarm = payload.alarm != 0;
        emit sampleReceived(sample);
    } else if (message.type == EG_MSG_COMMAND_ACK) {
        if (message.payload_length != 2) {
            emit protocolError(QStringLiteral("命令ACK长度错误：%1，期望2")
                                   .arg(message.payload_length));
            return;
        }
        emit commandAcknowledged(message.payload[0], message.payload[1]);
    }
}

void SerialDevice::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }
    if (error == QSerialPort::ResourceError) {
        const QString detail = m_port.errorString();
        close();
        emit connectionChanged(false, detail);
    }
}
