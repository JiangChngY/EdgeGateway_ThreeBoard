#include "mqttforwarder.h"

#include <QDebug>
#include <QStringList>

MqttForwarder::MqttForwarder(QObject *parent)
    : QObject(parent)
{
    m_restartTimer.setSingleShot(true);
    m_restartTimer.setInterval(3000);
    connect(&m_restartTimer, &QTimer::timeout,
            this, &MqttForwarder::ensureStarted);
    connect(&m_process, &QProcess::started,
            this, &MqttForwarder::pumpQueue);
    connect(&m_process, &QProcess::bytesWritten,
            this, [this](qint64) { pumpQueue(); });
    connect(&m_process, &QProcess::errorOccurred,
            this, &MqttForwarder::onProcessError);
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MqttForwarder::onProcessFinished);
}

void MqttForwarder::configure(bool enabled, const QString &program,
                              const QString &host, quint16 port,
                              const QString &topic)
{
    m_enabled = enabled;
    m_program = program;
    m_host = host;
    m_port = port;
    m_topic = topic;
    if (!m_enabled && m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
    }
}

void MqttForwarder::publish(const QByteArray &payload)
{
    if (!m_enabled) return;
    if (m_queue.size() >= 100) {
        qWarning() << "MQTT forward queue full; dropping oldest item. SQLite copy is retained.";
        m_queue.dequeue();
    }
    m_queue.enqueue(payload);
    ensureStarted();
    pumpQueue();
}

void MqttForwarder::ensureStarted()
{
    if (!m_enabled || m_queue.isEmpty() ||
        m_process.state() != QProcess::NotRunning || m_restartTimer.isActive()) {
        return;
    }
    if (m_program.isEmpty() || m_host.isEmpty() || m_topic.isEmpty()) {
        qWarning() << "MQTT forwarding configuration is incomplete";
        return;
    }
    const QStringList arguments = {
        QStringLiteral("-h"), m_host,
        QStringLiteral("-p"), QString::number(m_port),
        QStringLiteral("-t"), m_topic,
        QStringLiteral("-q"), QStringLiteral("1"),
        QStringLiteral("-l")
    };
    /* mosquitto_pub -l keeps one process and one broker connection alive;
     * each newline-delimited stdin record becomes one MQTT message. */
    m_process.start(m_program, arguments);
}

void MqttForwarder::pumpQueue()
{
    if (!m_enabled || m_process.state() != QProcess::Running) return;
    while (!m_queue.isEmpty() && m_process.bytesToWrite() < 64 * 1024) {
        const QByteArray line = m_queue.head() + '\n';
        if (m_process.write(line) != line.size()) {
            qWarning() << "failed to write MQTT payload to mosquitto_pub";
            return;
        }
        m_queue.dequeue();
    }
}

void MqttForwarder::onProcessError(QProcess::ProcessError error)
{
    qWarning() << "mosquitto_pub process error" << int(error)
               << m_process.errorString();
    if (m_enabled && !m_queue.isEmpty()) m_restartTimer.start();
}

void MqttForwarder::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_enabled && !m_queue.isEmpty()) {
        qWarning() << "mosquitto_pub exited" << exitCode << int(status)
                   << "; retrying queued data";
        m_restartTimer.start();
    }
}
