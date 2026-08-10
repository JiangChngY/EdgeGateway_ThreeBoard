#include "aggregatorserver.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

static const int MAX_JSON_LINE_BYTES = 64 * 1024;

AggregatorServer::AggregatorServer(AggregatorDataStore *store,
                                   MqttForwarder *mqtt,
                                   QObject *parent)
    : QObject(parent), m_store(store), m_mqtt(mqtt)
{
    connect(&m_server, &QTcpServer::newConnection, this, &AggregatorServer::acceptConnections);
    connect(&m_offlineTimer, &QTimer::timeout, this, &AggregatorServer::checkOffline);
    m_offlineTimer.start(5000);
}

bool AggregatorServer::listen(const QHostAddress &address, quint16 port, QString *error)
{
    if (!m_server.listen(address, port)) {
        if (error) *error = m_server.errorString();
        return false;
    }
    qInfo() << "collector listening" << m_server.serverAddress() << m_server.serverPort();
    return true;
}

void AggregatorServer::setOfflineTimeout(int seconds)
{
    m_offlineTimeout = qMax(5, seconds);
}

void AggregatorServer::acceptConnections()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *client = m_server.nextPendingConnection();
        m_buffers.insert(client, QByteArray());
        connect(client, &QTcpSocket::readyRead, this, &AggregatorServer::readClient);
        connect(client, &QTcpSocket::disconnected, this, &AggregatorServer::removeClient);
        qInfo() << "client connected" << client->peerAddress() << client->peerPort();
    }
}

void AggregatorServer::readClient()
{
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (!client) return;
    QByteArray &buffer = m_buffers[client];
    buffer += client->readAll();
    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (line.size() > MAX_JSON_LINE_BYTES) {
            sendAck(client, 0, false, QStringLiteral("JSON line too long"));
        } else if (!line.isEmpty()) {
            processLine(client, line);
        }
    }
    if (buffer.size() > MAX_JSON_LINE_BYTES) {
        /* No delimiter arrived within the line limit. Reply before closing;
         * MP157 keeps the unacknowledged item in SQLite and retries later. */
        sendAck(client, 0, false, QStringLiteral("unterminated JSON line too long"));
        buffer.clear();
        client->disconnectFromHost();
    }
}

void AggregatorServer::processLine(QTcpSocket *client, const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (!document.isObject()) {
        sendAck(client, 0, false, parseError.errorString());
        return;
    }
    const QJsonObject message = document.object();
    const qint64 sequence = qint64(message.value(QStringLiteral("seq")).toDouble(0));
    const QString gateway = message.value(QStringLiteral("gateway")).toString();
    if (gateway.isEmpty() || sequence <= 0) {
        sendAck(client, sequence, false, QStringLiteral("gateway/seq missing"));
        return;
    }
    const QString peer = client->peerAddress().toString();
    QString error;
    const QString type = message.value(QStringLiteral("type")).toString();
    bool ok = false;
    if (type == QStringLiteral("sensor")) {
        const int validFlags = message.value(QStringLiteral("valid_flags")).toInt(0x07);
        const bool environmentValid = (validFlags & 0x01) == 0 ||
            (message.value(QStringLiteral("temperature")).isDouble() &&
             message.value(QStringLiteral("humidity")).isDouble());
        const bool lightValid = (validFlags & 0x02) == 0 ||
            message.value(QStringLiteral("light")).isDouble();
        const bool analogValid = (validFlags & 0x04) == 0 ||
            message.value(QStringLiteral("analog")).isDouble();
        if (!environmentValid || !lightValid || !analogValid) {
            error = QStringLiteral("valid_flags conflicts with sensor fields");
        } else {
            ok = m_store->storeSensor(message, peer, &error);
        }
        if (ok) m_mqtt->publish(line);
    } else if (type == QStringLiteral("heartbeat")) {
        ok = m_store->touchGateway(gateway, peer, QDateTime::currentSecsSinceEpoch(), &error);
    } else {
        error = QStringLiteral("unsupported message type");
    }
    sendAck(client, sequence, ok, error);
}

void AggregatorServer::sendAck(QTcpSocket *client, qint64 sequence, bool ok, const QString &error)
{
    QJsonObject ack;
    ack.insert(QStringLiteral("type"), QStringLiteral("ack"));
    ack.insert(QStringLiteral("seq"), double(sequence));
    ack.insert(QStringLiteral("ok"), ok);
    if (!error.isEmpty()) ack.insert(QStringLiteral("error"), error);
    client->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n');
}

void AggregatorServer::removeClient()
{
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (!client) return;
    qInfo() << "client disconnected" << client->peerAddress();
    m_buffers.remove(client);
    client->deleteLater();
}

void AggregatorServer::checkOffline()
{
    QString error;
    m_store->markOfflineBefore(QDateTime::currentSecsSinceEpoch() - m_offlineTimeout, &error);
    if (!error.isEmpty()) qWarning() << error;
}
