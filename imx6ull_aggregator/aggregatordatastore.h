#ifndef AGGREGATOR_DATA_STORE_H
#define AGGREGATOR_DATA_STORE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>

class AggregatorDataStore
{
public:
    AggregatorDataStore();
    ~AggregatorDataStore();

    bool open(const QString &path, QString *error);
    bool storeSensor(const QJsonObject &message, const QString &peer, QString *error = nullptr);
    bool touchGateway(const QString &gateway, const QString &peer, qint64 now,
                      QString *error = nullptr);
    bool markOfflineBefore(qint64 cutoff, QString *error = nullptr);

    QJsonArray gatewayStatus(QString *error = nullptr) const;
    QJsonArray latestSamples(int limit, QString *error = nullptr) const;
    QString htmlStatusPage() const;

private:
    bool createSchema(QString *error);
    QString m_connectionName;
    QSqlDatabase m_db;
};

#endif

