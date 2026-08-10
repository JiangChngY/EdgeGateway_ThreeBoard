#ifndef STATUS_HTTP_SERVER_H
#define STATUS_HTTP_SERVER_H

#include "aggregatordatastore.h"

#include <QObject>
#include <QHostAddress>
#include <QTcpServer>

class StatusHttpServer : public QObject
{
    Q_OBJECT
public:
    explicit StatusHttpServer(AggregatorDataStore *store, QObject *parent = nullptr);
    bool listen(const QHostAddress &address, quint16 port, QString *error);

private slots:
    void acceptConnections();
    void respond();

private:
    AggregatorDataStore *m_store;
    QTcpServer m_server;
};

#endif
