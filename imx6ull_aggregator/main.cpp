#include "aggregatordatastore.h"
#include "aggregatorserver.h"
#include "mqttforwarder.h"
#include "statushttpserver.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHostAddress>
#include <QSettings>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("edge-aggregator"));

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption configOption({QStringLiteral("c"), QStringLiteral("config")},
                                    QStringLiteral("配置文件路径"), QStringLiteral("path"),
                                    QStringLiteral("/opt/edge-gateway/config/imx6ull.ini"));
    parser.addOption(configOption);
    parser.process(app);

    QSettings settings(parser.value(configOption), QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    AggregatorDataStore store;
    QString error;
    if (!store.open(settings.value(QStringLiteral("storage/database"),
                                   QStringLiteral("/opt/edge-gateway/data/imx6ull.db")).toString(), &error)) {
        qCritical() << "database:" << error;
        return 2;
    }

    MqttForwarder mqtt;
    mqtt.configure(settings.value(QStringLiteral("mqtt/enabled"), false).toBool(),
                   settings.value(QStringLiteral("mqtt/program"), QStringLiteral("mosquitto_pub")).toString(),
                   settings.value(QStringLiteral("mqtt/host"), QStringLiteral("127.0.0.1")).toString(),
                   quint16(settings.value(QStringLiteral("mqtt/port"), 1883).toUInt()),
                   settings.value(QStringLiteral("mqtt/topic"), QStringLiteral("edge/gateway/data")).toString());

    AggregatorServer collector(&store, &mqtt);
    collector.setOfflineTimeout(settings.value(QStringLiteral("server/offline_timeout"), 15).toInt());
    const QHostAddress listenAddress(settings.value(QStringLiteral("server/listen"),
                                                    QStringLiteral("0.0.0.0")).toString());
    if (!collector.listen(listenAddress,
                          quint16(settings.value(QStringLiteral("server/port"), 9000).toUInt()), &error)) {
        qCritical() << "collector:" << error;
        return 3;
    }

    StatusHttpServer http(&store);
    if (!http.listen(listenAddress,
                     quint16(settings.value(QStringLiteral("http/port"), 8080).toUInt()), &error)) {
        qCritical() << "http:" << error;
        return 4;
    }

    qInfo() << "edge aggregator started";
    return app.exec();
}

