#include "datastore.h"

extern "C" {
#include "edge_protocol.h"
}

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

DataStore::DataStore()
    : m_connectionName(QStringLiteral("edge_hmi_%1").arg(QUuid::createUuid().toString()))
{
}

DataStore::~DataStore()
{
    if (m_db.isValid()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool DataStore::open(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = QStringLiteral("无法创建数据库目录：%1").arg(info.absolutePath());
        return false;
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    return execSchema(error);
}

bool DataStore::execSchema(QString *error)
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS samples("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "ts TEXT NOT NULL,source_seq INTEGER,temperature REAL,humidity REAL,"
                       "light INTEGER,analog INTEGER,valid_flags INTEGER,alarm INTEGER)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_samples_ts ON samples(ts)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS alarms("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,ts TEXT NOT NULL,level TEXT,message TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS uplink_queue("
                       "seq INTEGER PRIMARY KEY,payload TEXT NOT NULL,created_at TEXT NOT NULL)")
    };
    for (const QString &sql : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            if (error) *error = query.lastError().text();
            return false;
        }
    }
    return true;
}

bool DataStore::insertSample(const SensorSample &sample, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO samples(ts,source_seq,temperature,humidity,light,analog,valid_flags,alarm) "
                                 "VALUES(?,?,?,?,?,?,?,?)"));
    query.addBindValue(sample.receivedAt.toString(Qt::ISODate));
    query.addBindValue(sample.sourceSequence);
    const bool environmentValid =
        (sample.validFlags & EG_SENSOR_VALID_TEMPERATURE_HUMIDITY) != 0;
    const bool lightValid = (sample.validFlags & EG_SENSOR_VALID_LIGHT) != 0;
    const bool analogValid = (sample.validFlags & EG_SENSOR_VALID_ANALOG) != 0;
    query.addBindValue(environmentValid ? QVariant(sample.temperature) : QVariant());
    query.addBindValue(environmentValid ? QVariant(sample.humidity) : QVariant());
    query.addBindValue(lightValid ? QVariant(sample.light) : QVariant());
    query.addBindValue(analogValid ? QVariant(sample.analog) : QVariant());
    query.addBindValue(sample.validFlags);
    query.addBindValue(sample.alarm ? 1 : 0);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    return true;
}

bool DataStore::insertAlarm(const QString &level, const QString &message, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO alarms(ts,level,message) VALUES(?,?,?)"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(level);
    query.addBindValue(message);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    return true;
}

QList<SensorSample> DataStore::recentSamples(int limit, QString *error) const
{
    QList<SensorSample> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT ts,source_seq,temperature,humidity,light,analog,valid_flags,alarm "
                                 "FROM samples ORDER BY id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return result;
    }
    while (query.next()) {
        SensorSample sample;
        sample.receivedAt = QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
        sample.sourceSequence = quint16(query.value(1).toUInt());
        sample.temperature = query.value(2).toDouble();
        sample.humidity = query.value(3).toDouble();
        sample.light = quint16(query.value(4).toUInt());
        sample.analog = quint16(query.value(5).toUInt());
        sample.validFlags = quint8(query.value(6).toUInt());
        sample.alarm = query.value(7).toInt() != 0;
        result.prepend(sample);
    }
    return result;
}

bool DataStore::enqueue(qint64 sequence, const QByteArray &payload, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO uplink_queue(seq,payload,created_at) VALUES(?,?,?)"));
    query.addBindValue(sequence);
    query.addBindValue(QString::fromUtf8(payload));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    return true;
}

bool DataStore::oldestPending(UplinkItem *item, QString *error) const
{
    if (!item) return false;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT seq,payload FROM uplink_queue ORDER BY seq LIMIT 1"))) {
        if (error) *error = query.lastError().text();
        return false;
    }
    if (!query.next()) return false;
    item->sequence = query.value(0).toLongLong();
    item->payload = query.value(1).toString().toUtf8();
    return true;
}

bool DataStore::acknowledge(qint64 sequence, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM uplink_queue WHERE seq=?"));
    query.addBindValue(sequence);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    return true;
}

int DataStore::pendingCount() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM uplink_queue")) || !query.next()) return -1;
    return query.value(0).toInt();
}
