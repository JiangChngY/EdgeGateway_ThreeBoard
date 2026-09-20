// Linux-only UI regression harness. All measurements here are generated fixtures.
#include "edgegatewaybackend.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QSettings>
#include <QImage>
#include <QDir>
#include <QtTest>
#include <pty.h>
#include <unistd.h>

static void require(bool ok, const char *message) {
    if (!ok) qFatal("UI TEST: %s", message);
}
static QQuickItem *findItem(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name) return parent;
    for (auto child : parent->childItems())
        if (auto found = findItem(child, name)) return found;
    return nullptr;
}
static QByteArray sampleFrame() {
    QByteArray frame = QByteArray::fromHex("aa55010101000c00f609d4170008720664010f01");
    quint16 crc = 0xffff;
    for (int i = 2; i < frame.size(); ++i) {
        crc ^= quint8(frame[i]);
        for (int bit=0; bit<8; ++bit) crc = (crc & 1) ? (crc >> 1)^0xa001 : crc >> 1;
    }
    frame.append(char(crc)); frame.append(char(crc >> 8));
    return frame;
}
int main(int argc, char **argv) {
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory");
    int master = -1, slave = -1;
    char name[256];
    require(openpty(&master, &slave, name, nullptr, nullptr) == 0, "PTY creation");
    const QString configPath = temp.path() + "/desktop.ini";
    QSettings config(configPath, QSettings::IniFormat);
    config.setValue("serial/port", QString::fromLocal8Bit(name));
    config.setValue("storage/database", temp.path() + "/samples.db");
    config.sync();
    qputenv("EDGE_DESKTOP_CONFIG", configPath.toUtf8());
    EdgeGatewayBackend backend;
    QQmlApplicationEngine engine;
    bool qmlFailure = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
        [&qmlFailure](const QList<QQmlError> &warnings) {
        for (const auto &warning : warnings) {
            const auto message = warning.toString();
            // Old Connections syntax is intentionally compatible with Qt 5.12.
            if (!message.contains("deprecated")) qmlFailure = true;
            qWarning().noquote() << message;
        }
    });
    engine.rootContext()->setContextProperty("gateway", &backend);
    engine.load(QUrl("qrc:/edgegateway/main.qml"));
    require(!engine.rootObjects().isEmpty(), "QML load");
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    require(window, "window");
    auto dashboard = findItem(window->contentItem(), "edge-dashboard");
    require(dashboard, "dashboard");
    QTest::qWait(400);
    require(backend.connected(), "automatic serial open");
    const QByteArray frame = sampleFrame();
    require(write(master, frame.constData(), frame.size()) == frame.size(), "write frame");
    QTest::qWait(300);
    require(backend.online(), "online state");
    const auto temperature = findItem(window->contentItem(), "value-temperature");
    require(temperature && temperature->property("text").toString() == "25.5", "temperature rendered");
    QString output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : temp.path();
    require(QDir().mkpath(output), "capture directory");
    auto capture = [&](const QString &name) {
        require(window->grabWindow().save(output + "/" + name + ".png"), "capture image");
    };
    capture("live-fixture");
    auto clickTab = [&](int index) {
        auto button = findItem(window->contentItem(), "page-button-" + QString::number(index));
        require(button, "tab item");
        const QPointF pos = button->mapToScene(QPointF(button->width()/2, button->height()/2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pos.toPoint());
        QTest::qWait(200);
        require(dashboard->property("page").toInt() == index, "tab tap changes page");
    };
    clickTab(1);
    require(backend.history().size() == 1, "history loaded asynchronously");
    capture("history-fixture");
    clickTab(2);
    auto combo = findItem(window->contentItem(), "serial-choice");
    require(combo, "serial choices");
    // WSL may expose no physical UARTs; provide a second test-only candidate.
    combo->setProperty("model", QStringList({backend.port(), "/dev/edge-test-candidate"}));
    const int candidate = 1;
    combo->setProperty("currentIndex", candidate);
    QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, candidate));
    QTest::qWait(1100); // Multiple timer notifications must not reset selection.
    require(combo->property("currentIndex").toInt() == candidate, "candidate port remains selected");
    capture("settings-fixture");
    clickTab(3);
    auto host = findItem(window->contentItem(), "uplink-host");
    require(host, "uplink host input");
    host->setProperty("text", "127.0.0.1");
    QTest::qWait(1100);
    require(host->property("text").toString() == "127.0.0.1", "upload input survives status refresh");
    require(!backend.configureUplink("127.0.0.1", 0, true), "invalid upload port rejected");
    require(backend.configureUplink("127.0.0.1", 9000, false), "upload settings saved");
    QTest::qWait(200);
    require(backend.queuedUploads() == 1, "paused upload retains sample");
    capture("uplink-fixture");
    auto tap = [&](const QString &name) {
        auto item = findItem(window->contentItem(), name);
        require(item, "keypad item exists");
        const auto point = item->mapToScene(QPointF(item->width()/2, item->height()/2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
        QTest::qWait(80);
    };
    tap("uplink-keypad");
    tap("keypad-clear");
    for (const QChar c : QString("192.168.138.3")) tap("keypad-key-" + QString(c));
    require(host->property("text").toString() == "192.168.138.3", "touch keypad enters address");
    capture("keypad-fixture");
    auto popup = window->findChild<QObject *>("uplink-keypad-popup");
    require(popup && QMetaObject::invokeMethod(popup, "close"), "close keypad");
    QTest::qWait(200);
    clickTab(0);
    QTest::qWait(3700);
    require(!backend.online(), "offline timeout");
    require(temperature->property("text").toString() == "--", "stale values hidden");
    capture("offline");
    require(!qmlFailure, "unexpected QML warning/error");
    close(master); close(slave);
    qInfo("PASS: tab taps, sensor rendering, history, stable port selection, offline timeout, screenshots");
    return 0;
}
