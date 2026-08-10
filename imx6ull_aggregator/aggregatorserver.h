#ifndef AGGREGATOR_SERVER_H
#define AGGREGATOR_SERVER_H

#include "aggregatordatastore.h"
#include "mqttforwarder.h"

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class AggregatorServer : public QObject
{
    Q_OBJECT
public:
    explicit AggregatorServer(AggregatorDataStore *store,
                              MqttForwarder *mqtt,
                              QObject *parent = nullptr);
    bool listen(const QHostAddress &address, quint16 port, QString *error);
    void setOfflineTimeout(int seconds);

private slots:
    void acceptConnections();
    void readClient();
    void removeClient();
    void checkOffline();

private:
    void processLine(QTcpSocket *client, const QByteArray &line);
    void sendAck(QTcpSocket *client, qint64 sequence, bool ok, const QString &error = QString());

    AggregatorDataStore *m_store;
    MqttForwarder *m_mqtt;
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QTimer m_offlineTimer;
    int m_offlineTimeout = 15;
};

#endif
