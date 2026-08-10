#ifndef MQTT_FORWARDER_H
#define MQTT_FORWARDER_H

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QTimer>

class MqttForwarder : public QObject
{
    Q_OBJECT
public:
    explicit MqttForwarder(QObject *parent = nullptr);
    void configure(bool enabled, const QString &program, const QString &host,
                   quint16 port, const QString &topic);
    void publish(const QByteArray &payload);

private slots:
    void ensureStarted();
    void pumpQueue();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    bool m_enabled = false;
    QString m_program = QStringLiteral("mosquitto_pub");
    QString m_host;
    QString m_topic;
    quint16 m_port = 1883;
    QQueue<QByteArray> m_queue;
    QProcess m_process;
    QTimer m_restartTimer;
};

#endif
