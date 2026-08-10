#include "edgeuploader.h"

extern "C" {
#include "edge_protocol.h"
}

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

EdgeUploader::EdgeUploader(DataStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
    m_reconnectTimer.setInterval(3000);
    m_ackTimer.setInterval(3000);
    m_ackTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &EdgeUploader::connectNow);
    connect(&m_ackTimer, &QTimer::timeout, this, &EdgeUploader::onAckTimeout);
    connect(&m_socket, &QTcpSocket::connected, this, &EdgeUploader::onConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &EdgeUploader::onDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &EdgeUploader::onReadyRead);
    m_nextSequence = QDateTime::currentMSecsSinceEpoch();
}

void EdgeUploader::configure(const QString &host, quint16 port, const QString &gatewayId)
{
    m_host = host;
    m_port = port;
    m_gatewayId = gatewayId;
}

void EdgeUploader::start()
{
    m_reconnectTimer.start();
    connectNow();
}

void EdgeUploader::connectNow()
{
    if (m_host.isEmpty() || m_socket.state() != QAbstractSocket::UnconnectedState) return;
    m_socket.connectToHost(m_host, m_port);
    publishStatus(QStringLiteral("正在连接 %1:%2").arg(m_host).arg(m_port));
}

void EdgeUploader::onConnected()
{
    publishStatus(QStringLiteral("汇聚节点已连接"));
    trySendNext();
}

void EdgeUploader::onDisconnected()
{
    m_ackTimer.stop();
    m_waitingSequence = -1;
    publishStatus(QStringLiteral("汇聚节点离线，数据已缓存"));
}

QByteArray EdgeUploader::sampleJson(qint64 sequence, const SensorSample &sample) const
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("sensor"));
    object.insert(QStringLiteral("gateway"), m_gatewayId);
    object.insert(QStringLiteral("seq"), double(sequence));
    object.insert(QStringLiteral("timestamp"), double(sample.receivedAt.toSecsSinceEpoch()));
    object.insert(QStringLiteral("source_seq"), int(sample.sourceSequence));
    object.insert(QStringLiteral("valid_flags"), int(sample.validFlags));
    const bool environmentValid =
        (sample.validFlags & EG_SENSOR_VALID_TEMPERATURE_HUMIDITY) != 0;
    object.insert(QStringLiteral("temperature"), environmentValid
                      ? QJsonValue(sample.temperature) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("humidity"), environmentValid
                      ? QJsonValue(sample.humidity) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("light"),
                  (sample.validFlags & EG_SENSOR_VALID_LIGHT) != 0
                      ? QJsonValue(int(sample.light)) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("analog"),
                  (sample.validFlags & EG_SENSOR_VALID_ANALOG) != 0
                      ? QJsonValue(int(sample.analog)) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("alarm"), sample.alarm);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

void EdgeUploader::submitSample(const SensorSample &sample)
{
    const qint64 sequence = ++m_nextSequence;
    QString error;
    if (!m_store->enqueue(sequence, sampleJson(sequence, sample), &error)) {
        publishStatus(QStringLiteral("上报缓存写入失败：%1").arg(error));
        return;
    }
    publishStatus(m_socket.state() == QAbstractSocket::ConnectedState
                      ? QStringLiteral("等待上报")
                      : QStringLiteral("离线缓存"));
    trySendNext();
}

void EdgeUploader::trySendNext()
{
    if (m_socket.state() != QAbstractSocket::ConnectedState || m_waitingSequence >= 0) return;
    UplinkItem item;
    if (!m_store->oldestPending(&item)) {
        publishStatus(QStringLiteral("数据已同步"));
        return;
    }
    m_waitingSequence = item.sequence;
    m_socket.write(item.payload + '\n');
    m_socket.flush();
    m_ackTimer.start();
    publishStatus(QStringLiteral("正在上报 #%1").arg(item.sequence));
}

void EdgeUploader::onReadyRead()
{
    m_rxBuffer += m_socket.readAll();
    int newline;
    while ((newline = m_rxBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_rxBuffer.left(newline).trimmed();
        m_rxBuffer.remove(0, newline + 1);
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) continue;
        const QJsonObject object = doc.object();
        const qint64 sequence = qint64(object.value(QStringLiteral("seq")).toDouble(-1));
        const bool ok = object.value(QStringLiteral("ok")).toBool(false);
        if (ok && sequence == m_waitingSequence) {
            QString error;
            if (!m_store->acknowledge(sequence, &error)) {
                publishStatus(QStringLiteral("ACK处理失败：%1").arg(error));
                m_socket.abort();
                return;
            }
            m_waitingSequence = -1;
            m_ackTimer.stop();
            trySendNext();
        }
    }
}

void EdgeUploader::onAckTimeout()
{
    m_waitingSequence = -1;
    m_socket.abort();
    publishStatus(QStringLiteral("ACK超时，等待重连"));
}

void EdgeUploader::publishStatus(const QString &detail)
{
    emit statusChanged(m_socket.state() == QAbstractSocket::ConnectedState,
                       detail, m_store->pendingCount());
}
