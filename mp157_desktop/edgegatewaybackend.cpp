#include "edgegatewaybackend.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QTcpSocket>
#include <cmath>

namespace {
const int MaxPendingWrites = 128;
const int StorageFlushWaitMs = 700;
const int StorageCancelWaitMs = 300;
}

// This object never touches SQLite outside its owning worker thread. Signals
// carry value snapshots only; the worker never keeps a pointer to the desktop.
class EdgeStorageWorker : public QObject {
    Q_OBJECT
public:
    explicit EdgeStorageWorker(const QString &path, const QString &host, int port, bool enabled)
        : m_path(path), m_host(host), m_port(port), m_enabled(enabled) {}

signals:
    void initialized(const QString &error);
    void writeFinished(const QString &error);
    void historyLoaded(const QVariantList &rows, const QString &error);
    void stopped();
    void uplinkChanged(const QString &status, int queued);

public slots:
    void initialize() {
        m_connectionName = "edge_desktop_" + QUuid::createUuid().toString();
        m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
        m_db.setDatabaseName(m_path);
        m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=200");
        if (!QThread::currentThread()->isInterruptionRequested()) emit initialized(openDatabase());
        m_socket = new QTcpSocket(this);
        m_socket->setReadBufferSize(8192);
        m_uploadTimer = new QTimer(this);
        connect(m_socket, &QTcpSocket::connected, this, [this] { m_wait.invalidate(); pump(); });
        connect(m_socket, &QTcpSocket::readyRead, this, &EdgeStorageWorker::readAck);
        connect(m_socket, &QTcpSocket::disconnected, this, [this] {
            m_inflight = 0; m_reply.clear(); m_wait.invalidate();
        });
        connect(m_uploadTimer, &QTimer::timeout, this, &EdgeStorageWorker::pump);
        m_uploadTimer->start(1000);
    }
    void save(const QString &timestamp, const QString &payload) {
        if (QThread::currentThread()->isInterruptionRequested()) return;
        QString error = openDatabase();
        if (error.isEmpty()) {
            // History and outbox are committed together. Never upload a sample
            // which has not reached persistent storage, or ACK it in memory only.
            if (!m_db.transaction()) error = m_db.lastError().text();
            QSqlQuery q(m_db);
            if (error.isEmpty()) {
                const int count = queued();
                if (count < 0) error = QStringLiteral("无法读取待上传队列");
                else if (count >= 100000) error = QStringLiteral("待上传队列达到 100000 条，请恢复汇聚连接");
            }
            if (error.isEmpty() && !q.prepare("INSERT INTO desktop_samples(ts,payload) VALUES(?,?)")) {
                error = q.lastError().text();
            } else if (error.isEmpty()) {
                q.addBindValue(timestamp);
                q.addBindValue(payload);
                if (!q.exec()) error = q.lastError().text();
            }
            if (error.isEmpty()) {
                q.prepare("INSERT INTO desktop_outbox(payload) VALUES(?)");
                q.addBindValue(payload);
                if (!q.exec()) error = q.lastError().text();
            }
            if (error.isEmpty() && !m_db.commit()) error = m_db.lastError().text();
            if (!error.isEmpty()) m_db.rollback();
        }
        emit writeFinished(error);
        if (error.isEmpty()) pump();
    }
    void configure(const QString &host, int port, bool enabled) {
        m_host = host; m_port = port; m_enabled = enabled;
        m_inflight = 0; m_reply.clear(); m_wait.invalidate();
        m_socket->abort(); pump();
    }
    void readHistory() {
        if (QThread::currentThread()->isInterruptionRequested()) return;
        QVariantList rows;
        QString error = openDatabase();
        if (error.isEmpty()) {
            QSqlQuery q(m_db);
            if (!q.exec("SELECT payload FROM desktop_samples ORDER BY id DESC LIMIT 100")) {
                error = q.lastError().text();
            } else {
                while (q.next())
                    rows.append(QJsonDocument::fromJson(q.value(0).toByteArray()).object().toVariantMap());
                if (q.lastError().isValid()) error = q.lastError().text();
            }
        }
        emit historyLoaded(rows, error);
    }
    void shutdown() {
        m_uploadTimer->stop(); m_socket->abort();
        // Queued work is drained first, or skipped after requestInterruption.
        // Closing/removing the connection also remains on its owning thread.
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        emit stopped();
    }

private:
    QString openDatabase() {
        if (m_db.isOpen()) return QString();
        if (m_path != ":memory:" && !QDir().mkpath(QFileInfo(m_path).absolutePath()))
            return QStringLiteral("无法创建历史数据目录");
        if (!m_db.open()) return m_db.lastError().text();
        QString error;
        {
            QSqlQuery q(m_db);
            if (!q.exec("PRAGMA busy_timeout=200")) {
                error = q.lastError().text();
            } else if (!q.exec("PRAGMA journal_mode=DELETE")) {
                error = q.lastError().text();
            } else if (!q.next() || (q.value(0).toString().toLower() != "delete" &&
                       !(m_path == ":memory:" && q.value(0).toString().toLower() == "memory"))) {
                error = QStringLiteral("无法切换到 DELETE 日志模式；请关闭其他数据库访问者");
            } else if (!q.exec("CREATE TABLE IF NOT EXISTS desktop_samples("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT,ts TEXT NOT NULL,payload TEXT NOT NULL)")) {
                error = q.lastError().text();
            }
            if (error.isEmpty() && !q.exec("CREATE TABLE IF NOT EXISTS desktop_outbox("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,payload TEXT NOT NULL)")) error = q.lastError().text();
            if (error.isEmpty() && !q.exec("CREATE TABLE IF NOT EXISTS desktop_identity("
                    "singleton INTEGER PRIMARY KEY CHECK(singleton=1),gateway TEXT NOT NULL)")) error = q.lastError().text();
            if (error.isEmpty()) {
                q.prepare("INSERT OR IGNORE INTO desktop_identity VALUES(1,?)");
                q.addBindValue("mp157-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
                if (!q.exec()) error = q.lastError().text();
            }
            if (error.isEmpty()) {
                if (!q.exec("SELECT gateway FROM desktop_identity WHERE singleton=1") || !q.next())
                    error = QStringLiteral("无法读取网关身份");
                else m_gateway = q.value(0).toString();
            }
        }
        // Do not use WAL on the NFS-root default path. DELETE still requires
        // working filesystem locks; a board-local persistent volume is safer.
        if (!error.isEmpty()) m_db.close();
        return error;
    }
    int queued() {
        QSqlQuery q(m_db);
        if (!q.exec("SELECT COUNT(*) FROM desktop_outbox") || !q.next()) return -1;
        return q.value(0).toInt();
    }
    void report(const QString &status) { emit uplinkChanged(status, m_db.isOpen() ? queued() : -1); }
    void pump() {
        if (!m_socket || QThread::currentThread()->isInterruptionRequested()) return;
        const QString error = openDatabase();
        if (!error.isEmpty()) { report("上传存储不可用：" + error); return; }
        if (!m_enabled) { report(QStringLiteral("上传已暂停，采集数据继续排队")); return; }
        if (m_wait.isValid() && m_wait.elapsed() > 5000) {
            m_socket->abort(); m_inflight = 0; m_reply.clear(); m_wait.invalidate();
            report(QStringLiteral("连接或确认超时，保留数据等待重试")); return;
        }
        if (m_socket->state() == QAbstractSocket::UnconnectedState) {
            m_inflight = 0; m_reply.clear(); m_wait.start();
            m_socket->connectToHost(m_host, quint16(m_port));
            report(QStringLiteral("正在连接 %1:%2").arg(m_host).arg(m_port)); return;
        }
        if (m_socket->state() != QAbstractSocket::ConnectedState || m_inflight) return;
        QSqlQuery q(m_db);
        if (!q.exec("SELECT id,payload FROM desktop_outbox ORDER BY id LIMIT 1")) {
            report(q.lastError().text()); return;
        }
        if (!q.next()) { report(QStringLiteral("汇聚已连接，数据全部确认")); return; }
        m_inflight = q.value(0).toLongLong();
        QJsonObject message = QJsonDocument::fromJson(q.value(1).toByteArray()).object();
        message.insert("schema", message.value("board").toString() == "F103ZE" ? 2 : 1);
        message.insert("type", "sensor"); message.insert("gateway", m_gateway);
        message.insert("source_seq", message.take("sequence"));
        message.insert("valid_flags", message.take("flags"));
        message.insert("seq", double(m_inflight));
        m_socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        m_wait.start(); report(QStringLiteral("已发送，等待汇聚确认"));
    }
    void readAck() {
        m_reply += m_socket->readAll();
        if (m_reply.size() > 8192) { m_socket->abort(); return; }
        int newline;
        while ((newline = m_reply.indexOf('\n')) >= 0) {
            const QByteArray line = m_reply.left(newline); m_reply.remove(0, newline + 1);
            const QJsonObject ack = QJsonDocument::fromJson(line).object();
            const double sequence = ack.value("seq").toDouble(-1);
            if (!m_inflight || ack.value("type").toString() != "ack" ||
                    sequence != double(m_inflight) || !ack.value("ok").isBool()) continue;
            if (!ack.value("ok").toBool()) {
                report("汇聚拒收，数据保留：" + ack.value("error").toString());
                // Timeout handles retry; do not drop a rejected sample.
                return;
            }
            QSqlQuery q(m_db); q.prepare("DELETE FROM desktop_outbox WHERE id=?");
            q.addBindValue(m_inflight);
            if (!q.exec()) { report("确认保存失败：" + q.lastError().text()); return; }
            m_inflight = 0; m_wait.invalidate();
        }
        pump();
    }
    QString m_path, m_connectionName;
    QSqlDatabase m_db;
    QString m_host, m_gateway;
    int m_port;
    bool m_enabled;
    QTcpSocket *m_socket = nullptr;
    QTimer *m_uploadTimer = nullptr;
    QElapsedTimer m_wait;
    QByteArray m_reply;
    qint64 m_inflight = 0;
};

static quint16 u16(const QByteArray &b, int i) {
    return quint16(quint8(b.at(i))) | (quint16(quint8(b.at(i+1))) << 8);
}
static quint16 crc16(const QByteArray &b) {
    quint16 crc = 0xffff;
    for (char c : b) {
        crc ^= quint8(c);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return crc;
}
EdgeGatewayBackend::EdgeGatewayBackend(QObject *parent) : QObject(parent) {
    setObjectName(QStringLiteral("edgegateway.backend.v1"));
    m_configPath = qEnvironmentVariable("EDGE_DESKTOP_CONFIG",
                                       "/opt/edge-gateway/config/desktop.ini");
    QSettings settings(m_configPath, QSettings::IniFormat);
    m_port = settings.value("serial/port", "/dev/ttyUSB0").toString();
    const QString dbPath = settings.value("storage/database",
                            "/opt/edge-gateway/data/desktop.db").toString();
    m_storageThread = new QThread;
    m_uplinkHost = settings.value("uplink/host", "192.168.138.3").toString();
    m_uplinkPort = settings.value("uplink/port", 9000).toInt();
    m_uplinkEnabled = settings.value("uplink/enabled", false).toBool();
    m_storage = new EdgeStorageWorker(dbPath, m_uplinkHost, m_uplinkPort, m_uplinkEnabled);
    m_storage->moveToThread(m_storageThread);
    connect(m_storageThread, &QThread::started, m_storage, &EdgeStorageWorker::initialize);
    connect(this, &EdgeGatewayBackend::writeRequested, m_storage, &EdgeStorageWorker::save, Qt::QueuedConnection);
    connect(this, &EdgeGatewayBackend::historyRequested, m_storage, &EdgeStorageWorker::readHistory, Qt::QueuedConnection);
    connect(this, &EdgeGatewayBackend::uplinkRequested, m_storage, &EdgeStorageWorker::configure, Qt::QueuedConnection);
    connect(m_storage, &EdgeStorageWorker::uplinkChanged, this, [this](const QString &status, int queued) {
        m_uplinkStatus = status; m_queuedUploads = queued; emit changed();
    });
    connect(m_storage, &EdgeStorageWorker::initialized, this, [this](const QString &error) {
        m_storageStatus = error.isEmpty() ? QString() : QStringLiteral("数据库不可用：%1").arg(error);
        emit changed();
    });
    connect(m_storage, &EdgeStorageWorker::writeFinished, this, [this](const QString &error) {
        if (m_pendingWrites > 0) --m_pendingWrites;
        if (!error.isEmpty()) ++m_droppedSamples;
        m_storageStatus = error.isEmpty() ? QString() : QStringLiteral("历史保存失败：%1").arg(error);
        emit changed();
    });
    connect(m_storage, &EdgeStorageWorker::historyLoaded, this,
            [this](const QVariantList &rows, const QString &error) {
        m_historyPending = false;
        if (error.isEmpty()) {
            m_history = rows;
            emit historyChanged();
        } else {
            m_storageStatus = QStringLiteral("历史读取失败：%1").arg(error);
        }
        emit changed();
    });
    // quit() is thread-safe. A direct connection lets shutdown finish even
    // while the GUI thread is in the bounded wait below.
    connect(m_storage, &EdgeStorageWorker::stopped, m_storageThread, &QThread::quit, Qt::DirectConnection);
    connect(m_storageThread, &QThread::finished, m_storage, &QObject::deleteLater);
    connect(m_storageThread, &QThread::finished, m_storageThread, &QObject::deleteLater);
    m_storageThread->start();
    m_serial.setReadBufferSize(4096);
    connect(&m_serial, &QSerialPort::readyRead, this, [this] { consume(m_serial.readAll()); });
    connect(&m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError) return;
        const QString detail = m_serial.errorString();
        if (error == QSerialPort::ResourceError) {
            m_serial.close(); m_buffer.clear(); m_age.invalidate();
        }
        m_status = detail; emit changed();
    });
    connect(&m_timer, &QTimer::timeout, this, [this] { emit changed(); });
    m_timer.start(500);
    scanPorts(); refreshNetwork(); refreshHistory();
}
EdgeGatewayBackend::~EdgeGatewayBackend() {
    m_timer.stop();
    m_serial.close();
    disconnect(this, nullptr, m_storage, nullptr);
    disconnect(m_storage, nullptr, this, nullptr);
    QMetaObject::invokeMethod(m_storage, "shutdown", Qt::QueuedConnection);
    // Give healthy storage a short opportunity to commit accepted samples.
    bool finished = m_storageThread->wait(StorageFlushWaitMs);
    if (!finished) {
        m_storageThread->requestInterruption();
        finished = m_storageThread->wait(StorageCancelWaitMs);
    }
    if (finished) {
        delete m_storageThread;
    } else {
        // A hard-mounted NFS syscall can outlast any SQLite busy_timeout.
        // Never terminate the worker or destroy a running QThread. Both are
        // detached from this backend and self-delete on normal completion;
        // if the process exits first, the OS reclaims them. Queued, uncommitted
        // samples are best-effort only and are discarded during shutdown.
        qWarning("Edge storage shutdown exceeded 1000 ms; leaving isolated worker to finish without blocking the desktop");
    }
    m_storage = nullptr;
    m_storageThread = nullptr;
}
QString EdgeGatewayBackend::status() const {
    QString result = m_status;
    if (!m_storageStatus.isEmpty()) result += " · " + m_storageStatus;
    if (m_pendingWrites >= MaxPendingWrites / 2)
        result += QStringLiteral(" · 历史待写 %1 条").arg(m_pendingWrites);
    if (m_droppedSamples)
        result += QStringLiteral(" · 未保存累计 %1 条").arg(m_droppedSamples);
    return result;
}
void EdgeGatewayBackend::start() {
    if (m_started) return;
    m_started = true;
    QSettings settings(m_configPath, QSettings::IniFormat);
    if (settings.value("serial/auto_open", true).toBool()) connectPort(m_port);
}
bool EdgeGatewayBackend::configureUplink(const QString &host, int port, bool enabled) {
    const QString target = host.trimmed();
    if (target.isEmpty() || target.contains('/') || target.contains(' ') || port < 1 || port > 65535) {
        m_uplinkStatus = QStringLiteral("请输入汇聚主机 IP 或主机名，以及 1–65535 的端口");
        emit changed(); return false;
    }
    QSettings settings(m_configPath, QSettings::IniFormat);
    settings.setValue("uplink/host", target); settings.setValue("uplink/port", port);
    settings.setValue("uplink/enabled", enabled); settings.sync();
    if (settings.status() != QSettings::NoError) {
        m_uplinkStatus = QStringLiteral("上传配置保存失败"); emit changed(); return false;
    }
    m_uplinkHost = target; m_uplinkPort = port; m_uplinkEnabled = enabled;
    emit uplinkRequested(target, port, enabled); emit changed(); return true;
}
bool EdgeGatewayBackend::connectPort(const QString &portName) {
    const QString selected = portName.trimmed();
    if (selected.isEmpty()) { m_status = QStringLiteral("请先选择串口"); emit changed(); return false; }
    disconnectPort();
    m_serial.setPortName(selected);
    m_serial.setBaudRate(115200);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);
    if (!m_serial.open(QIODevice::ReadWrite)) {
        qWarning("Serial open failed: %s: %s", qPrintable(selected), qPrintable(m_serial.errorString()));
        m_status = m_serial.errorString(); emit changed(); return false;
    }
    m_port = selected;
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());
    QSettings settings(m_configPath, QSettings::IniFormat);
    settings.setValue("serial/port", m_port); settings.sync();
    m_status = settings.status() == QSettings::NoError ? QStringLiteral("串口已打开，等待数据")
                                                      : QStringLiteral("已连接，但串口配置保存失败");
    emit changed(); return true;
}
void EdgeGatewayBackend::disconnectPort() {
    m_serial.close(); m_buffer.clear(); m_age.invalidate();
    m_status = QStringLiteral("采集已暂停"); emit changed();
}
void EdgeGatewayBackend::scanPorts() {
    m_ports.clear();
    for (const auto &p : QSerialPortInfo::availablePorts()) m_ports.append(p.systemLocation());
    if (!m_ports.contains(m_port)) m_ports.append(m_port);
    emit portsChanged();
}
void EdgeGatewayBackend::refreshNetwork() {
    QStringList addresses;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const auto &entry : iface.addressEntries())
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                addresses << iface.humanReadableName() + "  " + entry.ip().toString();
    }
    m_network = addresses.isEmpty() ? QStringLiteral("暂无 IPv4 网络连接") : addresses.join("\n");
    emit changed();
}
void EdgeGatewayBackend::consume(const QByteArray &bytes) {
    m_buffer += bytes;
    while (m_buffer.size() >= 2) {
        const int start = m_buffer.indexOf(QByteArray::fromHex("aa55"));
        if (start < 0) { m_buffer = m_buffer.endsWith(char(0xaa)) ? QByteArray(1,char(0xaa)) : QByteArray(); break; }
        if (start > 0) m_buffer.remove(0,start);
        if (m_buffer.size() < 8) break;
        const int length = u16(m_buffer,6);
        if (quint8(m_buffer.at(2)) != 1 || length > 64) {
            ++m_errors; m_buffer.remove(0,1); continue;
        }
        if (m_buffer.size() < length + 10) break;
        const QByteArray frame = m_buffer.left(length + 10);
        if (crc16(frame.mid(2,length+6)) != u16(frame,length+8)) {
            ++m_errors; m_buffer.remove(0,1); continue;
        }
        m_buffer.remove(0,length+10);
        if (quint8(frame.at(3)) == 1) receiveFrame(frame);
    }
}
void EdgeGatewayBackend::receiveFrame(const QByteArray &frame) {
    const int length = u16(frame,6);
    if (length != 12 && length != 10) {
        ++m_errors; m_status = QStringLiteral("不支持的采集负载长度 %1").arg(length); emit changed(); return;
    }
    const QByteArray p = frame.mid(8,length);
    const bool ze = length == 12;
    const quint8 flags = quint8(p.at(ze ? 10 : 8));
    if (ze && ((flags & 0xf0) || ((flags & 1) &&
            (qint16(u16(p,0)) < -4000 || qint16(u16(p,0)) > 12500 || u16(p,2) > 10000)))) {
        ++m_errors; m_status = QStringLiteral("温湿度或有效标志超出范围"); emit changed(); return;
    }
    if (ze && (((flags & 2) && (u16(p,4) > 4095 || u16(p,6) > 3600)) ||
               ((flags & 8) && quint8(p.at(11)) > 1))) {
        ++m_errors; m_status = QStringLiteral("传感器字段超出范围"); emit changed(); return;
    }
    QVariantMap s;
    s["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    s["sequence"] = u16(frame,4); s["flags"] = flags;
    s["board"] = ze ? "F103ZE" : "F103C8";
    s["synthetic"] = !ze && (flags & 0x80) != 0;
    s["temperature"] = (flags & 1) ? QVariant(qint16(u16(p,0))/100.0) : QVariant();
    s["humidity"] = (flags & 1) ? QVariant(u16(p,2)/100.0) : QVariant();
    if (ze) {
        s["ntc"] = (flags & 2) ? QVariant(u16(p,4)) : QVariant();
        s["millivolts"] = (flags & 2) ? QVariant(u16(p,6)) : QVariant();
        s["distance"] = (flags & 4) ? QVariant(u16(p,8)) : QVariant();
        s["digital"] = (flags & 8) ? QVariant(quint8(p.at(11))) : QVariant();
    } else {
        s["light"] = (flags & 2) ? QVariant(u16(p,4)) : QVariant();
        s["analog"] = (flags & 4) ? QVariant(u16(p,6)) : QVariant();
        s["alarm"] = quint8(p.at(9)) != 0;
    }
    m_sample = s; m_age.restart();
    m_status = QStringLiteral("%1 · %2 · #%3").arg(s["board"].toString())
            .arg(s["synthetic"].toBool() ? QStringLiteral("合成演示数据") : QStringLiteral("正在采集"))
            .arg(u16(frame,4));
    storeSample(); emit changed();
}
void EdgeGatewayBackend::storeSample() {
    if (m_pendingWrites >= MaxPendingWrites) {
        ++m_droppedSamples;
        m_storageStatus = QStringLiteral("存储繁忙，待写队列已满；新样本未保存");
        return;
    }
    ++m_pendingWrites;
    emit writeRequested(m_sample["time"].toString(),
        QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_sample)).toJson(QJsonDocument::Compact)));
}
void EdgeGatewayBackend::refreshHistory() {
    // Coalesce repeated refresh taps so history cannot make the queue unbounded.
    if (m_historyPending) return;
    m_historyPending = true;
    emit historyRequested();
}

#include "edgegatewaybackend.moc"
