#include "aggregatordatastore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QSet>

AggregatorDataStore::AggregatorDataStore()
    : m_connectionName(QStringLiteral("edge_aggregator_%1").arg(QUuid::createUuid().toString()))
{
}

AggregatorDataStore::~AggregatorDataStore()
{
    if (m_db.isValid()) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool AggregatorDataStore::open(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = QStringLiteral("无法创建数据库目录：%1").arg(info.absolutePath());
        return false;
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=500");
    if (!m_db.open()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    QSqlQuery pragma(m_db);
    if (!pragma.exec(QStringLiteral("PRAGMA journal_mode=DELETE")) || !pragma.next() ||
            (pragma.value(0).toString() != "delete" && path != ":memory:")) {
        if (error) *error = QStringLiteral("Cannot enable DELETE journal mode");
        return false;
    }
    if (!pragma.exec(QStringLiteral("PRAGMA synchronous=FULL"))) {
        if (error) *error = pragma.lastError().text();
        return false;
    }
    return createSchema(error);
}

bool AggregatorDataStore::createSchema(QString *error)
{
    const QStringList schema = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS samples("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,received_at INTEGER NOT NULL,"
                       "gateway TEXT NOT NULL,seq INTEGER,source_seq INTEGER,temperature REAL,humidity REAL,"
                       "light INTEGER,analog INTEGER,valid_flags INTEGER DEFAULT 7,"
                       "alarm INTEGER,peer TEXT,raw_json TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_agg_samples_gateway ON samples(gateway,id)"),
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_agg_samples_dedup ON samples(gateway,seq)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS gateways("
                       "gateway TEXT PRIMARY KEY,peer TEXT,last_seen INTEGER,online INTEGER,last_seq INTEGER)")
    };
    for (const QString &sql : schema) {
        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            if (error) *error = query.lastError().text();
            return false;
        }
    }

    bool hasValidFlags = false;
    QSqlQuery columns(m_db);
    if (!columns.exec(QStringLiteral("PRAGMA table_info(samples)"))) {
        if (error) *error = columns.lastError().text();
        return false;
    }
    while (columns.next()) {
        if (columns.value(1).toString() == QStringLiteral("valid_flags")) {
            hasValidFlags = true;
            break;
        }
    }
    if (!hasValidFlags) {
        QSqlQuery migrate(m_db);
        if (!migrate.exec(QStringLiteral(
                "ALTER TABLE samples ADD COLUMN valid_flags INTEGER DEFAULT 7"))) {
            if (error) *error = migrate.lastError().text();
            return false;
        }
    }
    QSet<QString> names;
    if (!columns.exec("PRAGMA table_info(samples)")) {
        if (error) *error = columns.lastError().text();
        return false;
    }
    while (columns.next()) names.insert(columns.value(1).toString());
    columns.finish();
    const QStringList additions = {"board TEXT DEFAULT 'F103C8'", "ntc INTEGER",
                                  "millivolts INTEGER", "distance INTEGER", "digital INTEGER"};
    for (const QString &definition : additions) {
        if (names.contains(definition.section(' ', 0, 0))) continue;
        QSqlQuery migrate(m_db);
        if (!migrate.exec("ALTER TABLE samples ADD COLUMN " + definition)) {
            if (error) *error = migrate.lastError().text();
            return false;
        }
    }
    return true;
}

bool AggregatorDataStore::storeSensor(const QJsonObject &message,
                                      const QString &peer,
                                      QString *error)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString gateway = message.value(QStringLiteral("gateway")).toString();
    const int validFlags = message.value(QStringLiteral("valid_flags")).toInt(0x07);
    const bool environmentValid = (validFlags & 0x01) != 0;
    const bool ze = message.value("schema").toInt(1) == 2;
    const bool lightValid = !ze && (validFlags & 0x02) != 0;
    const bool analogValid = !ze && (validFlags & 0x04) != 0;
    if (!m_db.transaction()) { if (error) *error = m_db.lastError().text(); return false; }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO samples(received_at,gateway,seq,source_seq,temperature,humidity,"
                                 "light,analog,valid_flags,alarm,peer,raw_json,board,ntc,millivolts,distance,digital) "
                                 "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(now);
    query.addBindValue(gateway);
    query.addBindValue(qint64(message.value(QStringLiteral("seq")).toDouble()));
    query.addBindValue(message.value(QStringLiteral("source_seq")).toInt());
    query.addBindValue(environmentValid
                           ? QVariant(message.value(QStringLiteral("temperature")).toDouble())
                           : QVariant());
    query.addBindValue(environmentValid
                           ? QVariant(message.value(QStringLiteral("humidity")).toDouble())
                           : QVariant());
    query.addBindValue(lightValid
                           ? QVariant(message.value(QStringLiteral("light")).toInt())
                           : QVariant());
    query.addBindValue(analogValid
                           ? QVariant(message.value(QStringLiteral("analog")).toInt())
                           : QVariant());
    query.addBindValue(validFlags);
    query.addBindValue(ze ? QVariant() : QVariant(message.value(QStringLiteral("alarm")).toBool() ? 1 : 0));
    query.addBindValue(peer);
    query.addBindValue(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
    query.addBindValue(ze ? "F103ZE" : "F103C8");
    for (const QString &key : {QString("ntc"), QString("millivolts"), QString("distance"), QString("digital")}) {
        const int flag = key == "distance" ? 4 : key == "digital" ? 8 : 2;
        query.addBindValue(ze && (validFlags & flag) ? QVariant(message.value(key).toInt()) : QVariant());
    }
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        m_db.rollback();
        return false;
    }
    if (!touchGateway(gateway, peer, now, error)) { m_db.rollback(); return false; }
    if (!m_db.commit()) {
        if (error) *error = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

bool AggregatorDataStore::touchGateway(const QString &gateway,
                                       const QString &peer,
                                       qint64 now,
                                       QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO gateways(gateway,peer,last_seen,online,last_seq) "
                                 "VALUES(?,?,?,1,COALESCE((SELECT last_seq FROM gateways WHERE gateway=?),0))"));
    query.addBindValue(gateway);
    query.addBindValue(peer);
    query.addBindValue(now);
    query.addBindValue(gateway);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    return true;
}

bool AggregatorDataStore::markOfflineBefore(qint64 cutoff, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE gateways SET online=0 WHERE last_seen<?"));
    query.addBindValue(cutoff);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return false;
    }
    return true;
}

QJsonArray AggregatorDataStore::gatewayStatus(QString *error) const
{
    QJsonArray result;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT gateway,peer,last_seen,online FROM gateways ORDER BY gateway"))) {
        if (error) *error = query.lastError().text();
        return result;
    }
    while (query.next()) {
        QJsonObject item;
        item.insert(QStringLiteral("gateway"), query.value(0).toString());
        item.insert(QStringLiteral("peer"), query.value(1).toString());
        item.insert(QStringLiteral("last_seen"), double(query.value(2).toLongLong()));
        item.insert(QStringLiteral("online"), query.value(3).toInt() != 0);
        result.append(item);
    }
    return result;
}

QJsonArray AggregatorDataStore::latestSamples(int limit, QString *error) const
{
    QJsonArray result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT received_at,gateway,seq,temperature,humidity,light,analog,valid_flags,alarm,"
                                 "board,ntc,millivolts,distance,digital,source_seq "
                                 "FROM samples ORDER BY id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        return result;
    }
    while (query.next()) {
        QJsonObject item;
        item.insert(QStringLiteral("received_at"), double(query.value(0).toLongLong()));
        item.insert(QStringLiteral("gateway"), query.value(1).toString());
        item.insert(QStringLiteral("seq"), double(query.value(2).toLongLong()));
        item.insert(QStringLiteral("temperature"), query.value(3).isNull()
                        ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(3).toDouble()));
        item.insert(QStringLiteral("humidity"), query.value(4).isNull()
                        ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(4).toDouble()));
        item.insert(QStringLiteral("light"), query.value(5).isNull()
                        ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(5).toInt()));
        item.insert(QStringLiteral("analog"), query.value(6).isNull()
                        ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(6).toInt()));
        item.insert(QStringLiteral("valid_flags"), query.value(7).toInt());
        item.insert(QStringLiteral("alarm"), query.value(8).isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(8).toInt() != 0));
        item.insert("board", query.value(9).toString());
        const QStringList keys = {"ntc", "millivolts", "distance", "digital", "source_seq"};
        for (int i = 0; i < keys.size(); ++i)
            item.insert(keys[i], query.value(10+i).isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(query.value(10+i).toInt()));
        result.append(item);
    }
    return result;
}

QString AggregatorDataStore::htmlStatusPage() const
{
    const QJsonArray gateways = gatewayStatus();
    const QJsonArray samples = latestSamples(20);
    QString rows;
    for (const QJsonValue &value : samples) {
        const QJsonObject s = value.toObject();
        const QString temperature = s.value(QStringLiteral("temperature")).isDouble()
                                        ? QString::number(s.value(QStringLiteral("temperature")).toDouble(), 'f', 1)
                                        : QStringLiteral("--");
        const QString humidity = s.value(QStringLiteral("humidity")).isDouble()
                                     ? QString::number(s.value(QStringLiteral("humidity")).toDouble(), 'f', 1)
                                     : QStringLiteral("--");
        const QString light = s.value(QStringLiteral("light")).isDouble()
                                  ? QString::number(s.value(QStringLiteral("light")).toInt())
                                  : QStringLiteral("--");
        auto cell = [&s](const char *key) {
            const QJsonValue v = s.value(QLatin1String(key));
            return v.isDouble() ? QString::number(v.toDouble()) : QStringLiteral("--");
        };
        rows += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td>"
                               "<td>%7</td><td>%8</td><td>%9</td><td>%10</td><td>%11</td></tr>")
                    .arg(QDateTime::fromSecsSinceEpoch(qint64(s.value(QStringLiteral("received_at")).toDouble())).toString(QStringLiteral("MM-dd hh:mm:ss")))
                    .arg(s.value(QStringLiteral("gateway")).toString().toHtmlEscaped())
                    .arg(temperature)
                    .arg(humidity)
                    .arg(light)
                    .arg(s.value("board").toString().toHtmlEscaped())
                    .arg(cell("ntc")).arg(cell("millivolts")).arg(cell("distance")).arg(cell("digital"))
                    .arg(s.value("alarm").isNull() ? QStringLiteral("--") :
                         s.value("alarm").toBool() ? QStringLiteral("报警") : QStringLiteral("正常"));
    }
    QString gatewayCards;
    for (const QJsonValue &value : gateways) {
        const QJsonObject g = value.toObject();
        const bool online = g.value(QStringLiteral("online")).toBool();
        gatewayCards += QStringLiteral("<div class='card'><b>%1</b><span class='%2'>%3</span><small>%4</small></div>")
                            .arg(g.value(QStringLiteral("gateway")).toString().toHtmlEscaped(),
                                 online ? QStringLiteral("on") : QStringLiteral("off"),
                                 online ? QStringLiteral("在线") : QStringLiteral("离线"),
                                 g.value(QStringLiteral("peer")).toString().toHtmlEscaped());
    }
    return QStringLiteral(
        "<!doctype html><meta charset='utf-8'><meta http-equiv='refresh' content='5'>"
        "<title>i.MX6ULL Edge Aggregator</title><style>body{font-family:Arial;margin:30px;background:#f1f5f9;color:#0f172a}"
        "h1{color:#0b4f6c}.cards{display:flex;gap:12px}.card{background:white;padding:16px;border-radius:8px;min-width:180px}"
        ".card span{float:right}.on{color:#15803d}.off{color:#b91c1c}small{display:block;color:#64748b;margin-top:10px}"
        "table{width:100%;border-collapse:collapse;background:white;margin-top:20px}th,td{padding:10px;border-bottom:1px solid #e2e8f0;text-align:left}</style>"
        "<h1>i.MX6ULL 二级边缘汇聚节点</h1><div class='cards'>%1</div>"
        "<table><tr><th>时间</th><th>网关</th><th>温度 ℃</th><th>湿度 %</th><th>光照</th><th>板卡</th>"
        "<th>NTC ADC</th><th>NTC mV</th><th>距离 mm</th><th>DO</th><th>报警</th></tr>%2</table>")
        .arg(gatewayCards, rows);
}
