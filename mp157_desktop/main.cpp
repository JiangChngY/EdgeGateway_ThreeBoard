#include "edgegatewaybackend.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>
#include <cstdio>
int main(int argc, char **argv) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        std::fprintf(stderr, "%s\n", qPrintable(message));
    });
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    EdgeGatewayBackend backend;
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &errors) {
        for (const auto &error : errors) std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
    });
    engine.rootContext()->setContextProperty("gateway", &backend);
    engine.load(QUrl("qrc:/edgegateway/main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    const QString capture = qEnvironmentVariable("EDGE_CAPTURE");
    if (!capture.isEmpty()) QTimer::singleShot(2000, &app, [&] {
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        app.exit(window && window->grabWindow().save(capture) ? 0 : 2);
    });
    return app.exec();
}
