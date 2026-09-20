#ifndef EDGEGATEWAYBACKEND_H
#define EDGEGATEWAYBACKEND_H
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QSerialPort>
#include <QTimer>
#include <QElapsedTimer>

class QThread;
class EdgeStorageWorker;

// One instance is owned by the desktop, so returning home keeps acquisition alive.
class EdgeGatewayBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap sample READ sample NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool online READ online NOTIFY changed)
    Q_PROPERTY(QString port READ port NOTIFY changed)
    Q_PROPERTY(QStringList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QString network READ network NOTIFY changed)
    Q_PROPERTY(int errors READ errors NOTIFY changed)
    Q_PROPERTY(bool synthetic READ synthetic NOTIFY changed)
    Q_PROPERTY(QString storageStatus READ storageStatus NOTIFY changed)
    Q_PROPERTY(int pendingWrites READ pendingWrites NOTIFY changed)
    Q_PROPERTY(qulonglong droppedSamples READ droppedSamples NOTIFY changed)
    Q_PROPERTY(QString uplinkStatus READ uplinkStatus NOTIFY changed)
    Q_PROPERTY(QString uplinkHost READ uplinkHost NOTIFY changed)
    Q_PROPERTY(int uplinkPort READ uplinkPort NOTIFY changed)
    Q_PROPERTY(bool uplinkEnabled READ uplinkEnabled NOTIFY changed)
    Q_PROPERTY(int queuedUploads READ queuedUploads NOTIFY changed)
public:
    explicit EdgeGatewayBackend(QObject *parent = nullptr);
    ~EdgeGatewayBackend() override;
    QVariantMap sample() const { return m_sample; }
    QString status() const;
    bool connected() const { return m_serial.isOpen(); }
    bool online() const { return connected() && m_age.isValid() && m_age.elapsed() < 3500; }
    QString port() const { return m_port; }
    QStringList ports() const { return m_ports; }
    QVariantList history() const { return m_history; }
    QString network() const { return m_network; }
    int errors() const { return m_errors; }
    bool synthetic() const { return m_sample.value("synthetic").toBool(); }
    QString storageStatus() const { return m_storageStatus; }
    int pendingWrites() const { return m_pendingWrites; }
    qulonglong droppedSamples() const { return m_droppedSamples; }
    QString uplinkStatus() const { return m_uplinkStatus; }
    QString uplinkHost() const { return m_uplinkHost; }
    int uplinkPort() const { return m_uplinkPort; }
    bool uplinkEnabled() const { return m_uplinkEnabled; }
    int queuedUploads() const { return m_queuedUploads; }
    Q_INVOKABLE bool configureUplink(const QString &host, int port, bool enabled);
    Q_INVOKABLE void start();
    Q_INVOKABLE bool connectPort(const QString &port);
    Q_INVOKABLE void disconnectPort();
    Q_INVOKABLE void scanPorts();
    Q_INVOKABLE void refreshHistory();
    Q_INVOKABLE void refreshNetwork();
signals:
    void changed();
    void portsChanged();
    void historyChanged();
    void writeRequested(const QString &timestamp, const QString &payload);
    void historyRequested();
    void uplinkRequested(const QString &host, int port, bool enabled);
private:
    void consume(const QByteArray &bytes);
    void receiveFrame(const QByteArray &frame);
    void storeSample();
    QSerialPort m_serial;
    // Neither object is parented to the desktop: a stalled filesystem must not
    // force destruction of a running thread during desktop shutdown.
    QThread *m_storageThread = nullptr;
    EdgeStorageWorker *m_storage = nullptr;
    QTimer m_timer;
    QElapsedTimer m_age;
    QByteArray m_buffer;
    QVariantMap m_sample;
    QVariantList m_history;
    QStringList m_ports;
    QString m_status = QStringLiteral("点击连接开始采集");
    QString m_port, m_network, m_configPath, m_storageStatus;
    int m_errors = 0;
    int m_pendingWrites = 0;
    qulonglong m_droppedSamples = 0;
    bool m_started = false;
    bool m_historyPending = false;
    QString m_uplinkHost, m_uplinkStatus;
    int m_uplinkPort = 9000, m_queuedUploads = 0;
    bool m_uplinkEnabled = false;
};
#endif
