#ifndef DATA_STORE_H
#define DATA_STORE_H

#include "sensormodel.h"

#include <QByteArray>
#include <QList>
#include <QSqlDatabase>

struct UplinkItem
{
    qint64 sequence = 0;
    QByteArray payload;
};

class DataStore
{
public:
    DataStore();
    ~DataStore();

    bool open(const QString &path, QString *error);
    bool insertSample(const SensorSample &sample, QString *error = nullptr);
    bool insertAlarm(const QString &level, const QString &message, QString *error = nullptr);
    QList<SensorSample> recentSamples(int limit, QString *error = nullptr) const;

    bool enqueue(qint64 sequence, const QByteArray &payload, QString *error = nullptr);
    bool oldestPending(UplinkItem *item, QString *error = nullptr) const;
    bool acknowledge(qint64 sequence, QString *error = nullptr);
    int pendingCount() const;

private:
    bool execSchema(QString *error);
    QString m_connectionName;
    QSqlDatabase m_db;
};

#endif

