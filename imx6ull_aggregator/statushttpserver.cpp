#include "statushttpserver.h"

#include <QJsonDocument>
#include <QList>
#include <QTcpSocket>

StatusHttpServer::StatusHttpServer(AggregatorDataStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
    connect(&m_server, &QTcpServer::newConnection, this, &StatusHttpServer::acceptConnections);
}

bool StatusHttpServer::listen(const QHostAddress &address, quint16 port, QString *error)
{
    if (!m_server.listen(address, port)) {
        if (error) *error = m_server.errorString();
        return false;
    }
    return true;
}

void StatusHttpServer::acceptConnections()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *client = m_server.nextPendingConnection();
        connect(client, &QTcpSocket::readyRead, this, &StatusHttpServer::respond);
        connect(client, &QTcpSocket::disconnected, client, &QTcpSocket::deleteLater);
    }
}

void StatusHttpServer::respond()
{
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (!client) return;
    const QByteArray request = client->readAll();
    const QList<QByteArray> requestParts = request.split(' ');
    const QByteArray method = requestParts.value(0, "GET");
    const QByteArray path = requestParts.value(1, "/");
    QByteArray body;
    QByteArray contentType;
    QByteArray status = "200 OK";
    if (method == "OPTIONS") {
        status = "204 No Content";
        contentType = "text/plain; charset=utf-8";
    } else if (path == "/api/status") {
        body = QJsonDocument(m_store->gatewayStatus()).toJson(QJsonDocument::Compact);
        contentType = "application/json; charset=utf-8";
    } else if (path == "/api/latest") {
        body = QJsonDocument(m_store->latestSamples(50)).toJson(QJsonDocument::Compact);
        contentType = "application/json; charset=utf-8";
    } else {
        body = m_store->htmlStatusPage().toUtf8();
        contentType = "text/html; charset=utf-8";
    }
    const QByteArray response = "HTTP/1.1 " + status
        + "\r\nConnection: close\r\nContent-Type: " + contentType
        + "\r\nAccess-Control-Allow-Origin: *"
          "\r\nAccess-Control-Allow-Methods: GET, OPTIONS"
          "\r\nAccess-Control-Allow-Headers: Content-Type"
          "\r\nContent-Length: " + QByteArray::number(body.size())
        + "\r\n\r\n" + body;
    client->write(response);
    client->disconnectFromHost();
}
