#ifndef EDGE_UPLOADER_H
#define EDGE_UPLOADER_H

#include "datastore.h"
#include "sensormodel.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

class EdgeUploader : public QObject
{
    Q_OBJECT
public:
    explicit EdgeUploader(DataStore *store, QObject *parent = nullptr);
    void configure(const QString &host, quint16 port, const QString &gatewayId);
    void start();
    void submitSample(const SensorSample &sample);

signals:
    void statusChanged(bool connected, const QString &detail, int pendingCount);

private slots:
    void connectNow();
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void trySendNext();
    void onAckTimeout();

private:
    QByteArray sampleJson(qint64 sequence, const SensorSample &sample) const;
    void publishStatus(const QString &detail);

    DataStore *m_store;
    QTcpSocket m_socket;
    QTimer m_reconnectTimer;
    QTimer m_ackTimer;
    QByteArray m_rxBuffer;
    QString m_host;
    QString m_gatewayId;
    quint16 m_port = 9000;
    qint64 m_nextSequence = 0;
    qint64 m_waitingSequence = -1;
};

#endif

